#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <curl/curl.h>

#define URL_BASE "https://api.cloudflare.com/client/v4"
#define HEAD_EMAIL "X-Auth-Email: "
#define HEAD_APIKEY "X-Auth-Key: "

#define STR_MAX 254 // max=254
#define BUF_MAX 1022 // max=65534
#define NAME_MAX 255
#define LINE_MAX 2048

static inline bool is_value(char c) {
    return c && c != '\n' && c != ' ' && c != '\t' && c != '#';
}

static inline bool is_space(char c) {
    return c == ' ' || c == '\t';
}

static inline bool is_end(char c) {
    return c == '\0' || c == '\n';
}

static inline char *pass_value(char *i) {
    while (is_value(*i)) ++i;
    return i;
}

static inline char *pass_space(char *i) {
    while (is_space(*i)) ++i;
    return i;
}

static inline char *pass_line(char *i) {
    while (!is_end(*i)) ++i;
    return i;
}

#define UNUSED __attribute__((unused))
#define STRLEN(STR) (sizeof(STR) - 1)

static inline void fputss(const char *start, const char *end, FILE *file) {
    fwrite(start, sizeof(char), end - start, file);
}

// static inline void fputcn(char c, size_t n, FILE *file) {
//     for (; n; --n) fputc(c, file);
// }

char BASENAME[NAME_MAX + 1];

typedef struct string {
    unsigned char len;
    char data[STR_MAX + 1];
} string;

static inline void string_clear(string *str) {
    str->len = 0;
}

static inline void string_setlen(string *str, unsigned char len) {
    str->len = len;
}

static inline void string_c_str(string *str) {
    str->data[str->len] = '\0';
}

static inline bool string_compare_slice(const string *str, const char *s, size_t n) {
    return str->len == n && !memcmp(str->data, s, n);
}

static inline bool string_compare_range(const string *str, const char *s, const char *e) {
    return string_compare_slice(str, s, e - s);
}

static inline bool string_compare(const string *str, const string *_str) {
    return string_compare_slice(str, _str->data, _str->len);
}

static inline void string_copy_slice(string *str, const char *s, size_t n) {
    memcpy(str->data, s, str->len = n < STR_MAX ? n : STR_MAX);
}

static inline void string_copy_str(string *str, const char *s) {
    string_copy_slice(str, s, strlen(s));
}

static inline void string_copy_range(string *str, const char *s, const char *e) {
    string_copy_slice(str, s, e - s);
}

static inline void string_concat_slice(string *str, const char *s, size_t n) {
    if (n > STR_MAX - str->len)
        n = STR_MAX - str->len;
    memcpy(str->data + str->len, s, n);
    str->len += n;
}

static inline void string_concat_range(string *str, const char *s, const char *e) {
    string_concat_slice(str, s, e - s);
}

static inline void string_fwrite(const string *str, FILE *file) {
    fwrite(str->data, sizeof(char), str->len, file);
}

static size_t
string_from_curl_callback(
        const char *data,
        const size_t UNUSED(char_size),
        const size_t len,
        string *str
) {
    const char *n = data + len;
    const char *s, *e;
    for (s = data; s < n; ++s) {
        if (*s != '\0' && *s != '\n' && *s != ' ') {
            for (e = s + 1; e < n && *e != '\0' && *e != '\n' && *e != ' '; ++e);
            string_concat_range(str, s, e);
            break;
        }
    }
    return len;
}

static void
string_from_curl(string *str, const char *url) {
    string_clear(str);
    CURL *curl = curl_easy_init();
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, string_from_curl_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, str);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

typedef struct buffer {
    unsigned short len;
    char data[BUF_MAX + 1];
} buffer;

static inline void buffer_clear(buffer *buf) {
    buf->len = 0;
}

static inline void buffer_c_str(buffer *buf) {
    buf->data[buf->len] = '\0';
}

static inline void buffer_concat_char(buffer *buf, char c) {
    if (buf->len < BUF_MAX) {
        buf->data[buf->len++] = c;
    }
}

static inline void buffer_concat_slice(buffer *buf, const char *s, size_t n) {
    if (n > BUF_MAX - buf->len)
        n = BUF_MAX - buf->len;
    memcpy(buf->data + buf->len, s, n);
    buf->len += n;
}

static inline void buffer_concat_range(buffer *buf, const char *s, const char *e) {
    buffer_concat_slice(buf, s, e - s);
}

static inline void buffer_concat_str(buffer *buf, const char *s) {
    buffer_concat_slice(buf, s, strlen(s));
}

static inline void buffer_concat_string(buffer *buf, const string *str) {
    buffer_concat_slice(buf, str->data, str->len);
}

static size_t
buffer_from_curl_callback(
        const char *data,
        const size_t UNUSED(char_size),
        const size_t len,
        buffer *buf
) {
    const char *n = data + len;
    const char *s, *e;
    for (s = data; s < n; ++s) {
        if (*s != '\0') {
            for (e = s + 1; e < n && *e != '\0'; ++e);
            buffer_concat_range(buf, s, e);
            break;
        }
    }
    return len;
}

typedef struct variable {
    struct variable *prev;
    string key;
    string value;
    bool changed;
} variable;

struct cfddns_context {
    string user_email_header;
    string user_apikey_header;
    string zone_id;
    string zone_name;
    string record_id;
    string record_type;
    string record_name;
    string record_content;
    variable *vars;
} cfddns;

static void
cfddns_init() {
    string_copy_str(&cfddns.user_email_header, HEAD_EMAIL);
    string_copy_str(&cfddns.user_apikey_header, HEAD_APIKEY);
    string_clear(&cfddns.zone_id);
    string_clear(&cfddns.zone_name);
    string_clear(&cfddns.record_id);
    string_clear(&cfddns.record_type);
    string_clear(&cfddns.record_name);
    string_clear(&cfddns.record_content);
    cfddns.vars = NULL;
}

static void
cfddns_cleanup() {
    while (cfddns.vars) {
        variable *prev = cfddns.vars->prev;
        free(cfddns.vars);
        cfddns.vars = prev;
    }
}

static void
cfddns_get_zone_id(string *zone_id) {
    string_clear(zone_id);

    CURL *curl = curl_easy_init();
    if (!curl) return;

    buffer url;
    buffer_clear(&url);
    buffer_concat_str(&url, URL_BASE);
    buffer_concat_str(&url, "/zones?name=");
    buffer_concat_string(&url, &cfddns.zone_name);
    buffer_c_str(&url);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, cfddns.user_email_header.data);
    headers = curl_slist_append(headers, cfddns.user_apikey_header.data);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    buffer response;
    buffer_clear(&response);
    curl_easy_setopt(curl, CURLOPT_URL, url.data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_from_curl_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    buffer_c_str(&response);

    curl_slist_free_all(headers);

    char *s = strstr(response.data, "\"result\":[{\"id\":\"");
    if (!s) return;
    char *e = strchr(s += STRLEN("\"result\":[{\"id\":\""), '"');
    if (!e) return;
    string_copy_range(zone_id, s, e);
}

static bool
cfddns_get_record_id(string *record_id, string *record_content) {
    string_clear(record_id);
    string_clear(record_content);

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    buffer url;
    buffer_clear(&url);
    buffer_concat_str(&url, URL_BASE);
    buffer_concat_str(&url, "/zones/");
    buffer_concat_string(&url, &cfddns.zone_id);
    buffer_concat_str(&url, "/dns_records?type=");
    buffer_concat_string(&url, &cfddns.record_type);
    buffer_concat_str(&url, "&name=");
    if (cfddns.record_name.len != 1 || cfddns.record_name.data[0] != '@') {
        buffer_concat_string(&url, &cfddns.record_name);
        buffer_concat_char(&url, '.');
    }
    buffer_concat_string(&url, &cfddns.zone_name);
    buffer_c_str(&url);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, cfddns.user_email_header.data);
    headers = curl_slist_append(headers, cfddns.user_apikey_header.data);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    buffer response;
    buffer_clear(&response);
    curl_easy_setopt(curl, CURLOPT_URL, url.data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_from_curl_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    buffer_c_str(&response);

    curl_slist_free_all(headers);

    char *s, *e;

    if (!(s = strstr(response.data, "\"id\":\""))) return false;
    if (!(e = strchr(s += STRLEN("\"id\":\""), '"'))) return false;
    string_copy_range(record_id, s, e);

    if (!(s = strstr(response.data, "\"content\":\""))) return false;
    if (!(e = strchr(s += STRLEN("\"content\":\""), '"'))) return false;
    string_copy_range(record_content, s, e);

    return true;
}

static bool
cfddns_update_record(const string *record_content) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    buffer url;
    buffer_clear(&url);
    buffer_concat_str(&url, URL_BASE);
    buffer_concat_str(&url, "/zones/");
    buffer_concat_string(&url, &cfddns.zone_id);
    buffer_concat_str(&url, "/dns_records/");
    buffer_concat_string(&url, &cfddns.record_id);
    buffer_c_str(&url);

    buffer request;
    buffer_clear(&request);
    buffer_concat_str(&request, "{\"type\":\"");
    buffer_concat_string(&request, &cfddns.record_type);
    buffer_concat_str(&request, "\",\"name\":\"");
    buffer_concat_string(&request, &cfddns.record_name);
    buffer_concat_str(&request, "\",\"content\":\"");
    buffer_concat_string(&request, record_content);
    buffer_concat_str(&request, "\"}");
    buffer_c_str(&request);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, cfddns.user_email_header.data);
    headers = curl_slist_append(headers, cfddns.user_apikey_header.data);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    buffer response;
    buffer_clear(&response);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_URL, url.data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_from_curl_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    buffer_c_str(&response);

    curl_slist_free_all(headers);

    return strstr(response.data, "\"success\":true");
}

static int cfddns_main(FILE *fin, FILE *fout, FILE *flog) {
    cfddns_init();
    char line[LINE_MAX];
    while (fgets(line, LINE_MAX, fin)) {
        char *s, *e = line;
        s = pass_space(e);
        fputss(e, s, fout);
        fputss(e, s, flog);
        e = pass_value(s);
        if (s == e) {
            s = pass_line(e);
            fputss(e, s, fout);
            fputss(e, s, flog);
        } else {
            switch (e[-1]) {
                case '?': {
                    variable *var = cfddns.vars;
                    for (size_t n = e - 1 - s; var; var = var->prev) {
                        if (string_compare_slice(&var->key, s, n)) {
                            break;
                        }
                    }
                    if (var) {
                        fputss(s, e, fout);
                        fputss(s, e, flog);
                        s = pass_line(e);
                        fputss(e, s, fout);
                        fputss(e, s, flog);
                        fputs(" #already_binded", flog);
                        break;
                    }
                    var = malloc(sizeof(variable));
                    if (!var) {
                        fputss(s, e, fout);
                        fputss(s, e, flog);
                        s = pass_line(e);
                        fputss(e, s, fout);
                        fputss(e, s, flog);
                        fputs(" #need_memory", flog);
                        break;
                    }
                    // var_key
                    string_copy_range(&var->key, s, e - 1);
                    string_fwrite(&var->key, fout);
                    string_fwrite(&var->key, flog);
                    fputc('?', fout);
                    fputc('?', flog);
                    // url
                    s = pass_space(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    e = pass_value(s);
                    if (s == e) {
                        free(var);
                        s = pass_line(e);
                        fputss(e, s, fout);
                        fputss(e, s, flog);
                        fputs(" #need_url", flog);
                        fflush(flog);
                        break;
                    }
                    fputss(s, e, fout);
                    fputss(s, e, flog);
                    // url => var_value
                    fflush(flog);
                    char _e = *e;
                    *e = '\0';
                    string_from_curl(&var->value, s);
                    *e = _e;
                    if (!var->value.len) {
                        s = pass_line(e);
                        fputss(e, s, fout);
                        fputss(e, s, flog);
                        fputs(" #request_failed", flog);
                        fflush(flog);
                        break;
                    }
                    // value_old => var_changed
                    s = pass_space(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    e = pass_value(s);
                    var->changed = !string_compare_range(&var->value, s, e);
                    // var_value
                    if (!is_space(s[-1])) {
                        fputc(' ', fout);
                        fputc(' ', flog);
                    }
                    string_fwrite(&var->value, fout);
                    string_fwrite(&var->value, flog);
                    // tails
                    s = pass_line(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    // var_changed
                    if (var->changed) {
                        fputs(" #changed", flog);
                    }
                    // vars
                    var->prev = cfddns.vars;
                    cfddns.vars = var;
                    break;
                }
                case ':': {
                    // user_email_header
                    string_setlen(&cfddns.user_email_header, STRLEN(HEAD_EMAIL));
                    string_concat_range(&cfddns.user_email_header, s, e - 1);
                    string_c_str(&cfddns.user_email_header);
                    fputss(s, e, fout);
                    fputss(s, e, flog);
                    // user_apikey_header
                    s = pass_space(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    e = pass_value(s);
                    if (s == e) {
                        string_setlen(&cfddns.user_email_header, STRLEN(HEAD_EMAIL));
                        string_setlen(&cfddns.user_apikey_header, STRLEN(HEAD_APIKEY));
                        s = pass_line(e);
                        fputss(e, s, fout);
                        fputss(e, s, flog);
                        fputs(" #need_apikey", flog);
                        break;
                    }
                    string_setlen(&cfddns.user_apikey_header, STRLEN(HEAD_APIKEY));
                    string_concat_range(&cfddns.user_apikey_header, s, e);
                    string_c_str(&cfddns.user_apikey_header);
                    fputss(s, e, fout);
                    // fputss(s, e, flog);
                    // fputcn(' ', e - s, flog);
                    // tails
                    s = pass_line(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    break;
                }
                case '/': {
                    // zone_name
                    string_copy_range(&cfddns.zone_name, s, e - 1);
                    string_fwrite(&cfddns.zone_name, fout);
                    string_fwrite(&cfddns.zone_name, flog);
                    fputc('/', fout);
                    fputc('/', flog);
                    // zone_id
                    s = pass_space(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    e = pass_value(s);
                    if (s == e) {
                        fflush(flog);
                        cfddns_get_zone_id(&cfddns.zone_id);
                    } else {
                        string_copy_range(&cfddns.zone_id, s, e);
                    }
                    if (!is_space(s[-1])) {
                        fputc(' ', fout);
                        fputc(' ', flog);
                    }
                    string_fwrite(&cfddns.zone_id, fout);
                    // string_fwrite(&cfddns.zone_id, flog);
                    // fputcn(' ', cfddns.zone_id.len, flog);
                    // tails
                    s = pass_line(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    break;
                }
                default: {
                    // type
                    string_copy_range(&cfddns.record_type, s, e);
                    string_fwrite(&cfddns.record_type, fout);
                    string_fwrite(&cfddns.record_type, flog);
                    // name
                    s = pass_space(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    e = pass_value(s);
                    if (s == e) {
                        s = pass_line(e);
                        fputss(e, s, fout);
                        fputss(e, s, flog);
                        fputs(" #need_name", flog);
                        break;
                    }
                    string_copy_range(&cfddns.record_name, s, e);
                    string_fwrite(&cfddns.record_name, fout);
                    string_fwrite(&cfddns.record_name, flog);
                    // var_key
                    s = pass_space(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    e = pass_value(s);
                    if (s == e) {
                        s = pass_line(e);
                        fputss(e, s, fout);
                        fputss(e, s, flog);
                        fputs(" #need_var", flog);
                        break;
                    }
                    variable *var = cfddns.vars;
                    size_t var_key_len = e - s;
                    fwrite(s, 1, var_key_len, fout);
                    fwrite(s, 1, var_key_len, flog);
                    while (var && !string_compare_slice(&var->key, s, var_key_len)) {
                        var = var->prev;
                    }
                    if (!var) {
                        s = pass_line(e);
                        fputss(e, s, fout);
                        fputss(e, s, flog);
                        fputs(" #var_undefined", flog);
                        break;
                    }
                    // content
                    string_clear(&cfddns.record_content);
                    // id
                    bool force_update, may_expired;
                    s = pass_space(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    fflush(flog);
                    e = pass_value(s);
                    if (s == e) {
                        force_update = true;
                        may_expired = false;
                        cfddns_get_record_id(&cfddns.record_id, &cfddns.record_content);
                    } else if (e[-1] == '!') {
                        force_update = true;
                        may_expired = true;
                        if (e - s == 1) {
                            cfddns_get_record_id(&cfddns.record_id, &cfddns.record_content);
                        } else {
                            string_copy_range(&cfddns.record_id, s, e - 1);
                        }
                    } else {
                        force_update = false;
                        may_expired = true;
                        string_copy_range(&cfddns.record_id, s, e);
                    }
                    // do update
                    bool success;
                    if (!cfddns.record_id.len) {
                        success = false;
                    } else if (!force_update && !var->changed) {
                        success = true;
                    } else if (string_compare(&cfddns.record_content, &var->value) ||
                               cfddns_update_record(&var->value)) {
                        success = true;
                    } else if (may_expired) {
                        cfddns_get_record_id(&cfddns.record_id, &cfddns.record_content);
                        success = string_compare(&cfddns.record_content, &var->value) ||
                                  cfddns_update_record(&var->value);
                    } else {
                        success = false;
                    }
                    if (!is_space(s[-1])) {
                        fputc(' ', fout);
                        fputc(' ', flog);
                    }
                    string_fwrite(&cfddns.record_id, fout);
                    // string_fwrite(&cfddns.record_id, flog);
                    // fputcn(' ', cfddns.record_id.len, flog);
                    // tails
                    if (!success) {
                        fputc('!', fout);
                        fputc('!', flog);
                        if (*e == ' ' && !is_value(e[1])) ++e;
                    } else if (e[-1] == '!') {
                        fputc(' ', fout);
                        fputc(' ', flog);
                    }
                    s = pass_line(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    break;
                }
            }
        }
        fputc('\n', fout);
        fputc('\n', flog);
        fflush(flog);
    }
    cfddns_cleanup();
    return 0;
}

int main(int argc, char *argv[]) {
    for (char *i = argv[0], *s = i;;) {
        if (*i) {
            if (*i != '/') ++i; else s = ++i;
        } else {
            memcpy((void *) BASENAME, s, i - s + 1);
            break;
        }
    }
    if (argc < 2) {
        fputs("Usage: ", stderr);
        fputs(BASENAME, stderr);
        fputs(" [config_file]\n", stderr);
        return 1;
    }
    FILE *fin = fopen(argv[1], "r");
    if (!fin) {
        perror(BASENAME);
        return 1;
    }
    FILE *tmp = tmpfile();
    if (!tmp) {
        perror(BASENAME);
        fclose(fin);
        return 1;
    }
    cfddns_main(fin, tmp, stdout);
    fclose(fin);
    FILE *fout = fopen(argv[1], "wb");
    if (!fout) {
        perror(BASENAME);
        fclose(tmp);
        return 1;
    }
    rewind(tmp);
    int ch;
    while ((ch = fgetc(tmp)) != EOF) {
        fputc(ch, fout);
    }
    fclose(fout);
    return 0;
}
