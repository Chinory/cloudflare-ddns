#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <curl/curl.h>

#define STR_MAX 254 // max=254
#define BUF_MAX 1022 // max=65534
#define NAME_MAX 255  // usually 255 in linux
#define BUFF_SIZE 2016 // in x86_64 if set to 2016, the context struct will be just 4K size

// #define LOG_SECRETS // show apikey, zone_id, record_id in stdout

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

#define URL_BASE "https://api.cloudflare.com/client/v4"
#define HEAD_EMAIL "X-Auth-Email: "
#define HEAD_APIKEY "X-Auth-Key: "

#define UNUSED __attribute__((unused))
#define STRLEN(STR) (sizeof(STR) - 1)

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

static inline bool string_equals_slice(const string *str, const char *s, size_t n) {
    return str->len == n && !memcmp(str->data, s, n);
}

static inline bool string_equals_string(const string *str, const string *_str) {
    return string_equals_slice(str, _str->data, _str->len);
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
    FILE *fin;
    FILE *fout;
    FILE *flog;
    variable *vars;
    string user_email_header;
    string user_apikey_header;
    string zone_id;
    string zone_name;
    string record_id;
    string record_type;
    string record_name;
    string record_content;
    char buff[BUFF_SIZE];
} cfddns;

static void
cfddns_init() {
    cfddns.vars = NULL;
    string_copy_str(&cfddns.user_email_header, HEAD_EMAIL);
    string_copy_str(&cfddns.user_apikey_header, HEAD_APIKEY);
    string_clear(&cfddns.zone_id);
    string_clear(&cfddns.zone_name);
    string_clear(&cfddns.record_id);
    string_clear(&cfddns.record_type);
    string_clear(&cfddns.record_name);
    string_clear(&cfddns.record_content);
}

static void
cfddns_cleanup() {
    while (cfddns.vars) {
        variable *prev = cfddns.vars->prev;
        free(cfddns.vars);
        cfddns.vars = prev;
    }
}

static struct curl_slist *
cfddns_make_headers() {
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, cfddns.user_email_header.data);
    headers = curl_slist_append(headers, cfddns.user_apikey_header.data);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    return headers;
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

    buffer response;
    buffer_clear(&response);
    struct curl_slist *headers = cfddns_make_headers();
    curl_easy_setopt(curl, CURLOPT_URL, url.data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_from_curl_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    buffer_c_str(&response);

    char *s, *e;

    if (!(s = strstr(response.data, "\"result\":[{\"id\":\""))) return;
    if (!(e = strchr(s += STRLEN("\"result\":[{\"id\":\""), '"'))) return;
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

    buffer response;
    buffer_clear(&response);
    struct curl_slist *headers = cfddns_make_headers();
    curl_easy_setopt(curl, CURLOPT_URL, url.data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_from_curl_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    buffer_c_str(&response);

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

    buffer response;
    buffer_clear(&response);
    struct curl_slist *headers = cfddns_make_headers();
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_URL, url.data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, buffer_from_curl_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    buffer_c_str(&response);

    return strstr(response.data, "\"success\":true");
}

static char *
cfddns_line_next(char *e) {
    char *s = pass_space(e);
    size_t n = s - e;
    fwrite(e, sizeof(char), n ,cfddns.fout);
    fwrite(e, sizeof(char), n ,cfddns.flog);
    return s;
}

static void
cfddns_line_done(char *e, const char *log) {
    size_t n = pass_line(e) - e;
    fwrite(e, sizeof(char), n ,cfddns.fout);
    fwrite(e, sizeof(char), n ,cfddns.flog);
    fputs(log, cfddns.flog);
    fputc('\n', cfddns.fout);
    fputc('\n', cfddns.flog);
    fflush(cfddns.flog);
}

static void
cfddns_proc_line_var(char *s, char *e) {
    // key
    size_t n = e - s;
    variable *var = cfddns.vars;
    for (; var != NULL; var = var->prev) {
        if (string_equals_slice(&var->key, s, n - 1)) {
            fwrite(s, sizeof(char), n, cfddns.fout);
            fwrite(s, sizeof(char), n, cfddns.flog);
            return cfddns_line_done(e, " #var_already_binded");
        }
    }
    var = malloc(sizeof(variable));
    if (var == NULL) {
        fwrite(s, sizeof(char), n, cfddns.fout);
        fwrite(s, sizeof(char), n, cfddns.flog);
        return cfddns_line_done(e, " #var_need_memory");
    }
    string_copy_slice(&var->key, s, n - 1);
    fwrite(s, sizeof(char), n, cfddns.fout);
    fwrite(s, sizeof(char), n, cfddns.flog);
    // url
    s = cfddns_line_next(e);
    e = pass_value(s);
    n = e - s;
    if (!n) {
        free(var);
        return cfddns_line_done(e, " #need_url");
    }
    fwrite(s, sizeof(char), n, cfddns.fout);
    fwrite(s, sizeof(char), n, cfddns.flog);
    fflush(cfddns.flog);
    // url => value_now
    char _e = *e;
    *e = '\0';
    string_from_curl(&var->value, s);
    *e = _e;
    if (!var->value.len) {
        free(var);
        return cfddns_line_done(e, " #request_failed");
    }
    // value_last => changed
    s = cfddns_line_next(e);
    e = pass_value(s);
    n = e - s;
    if (!n) {
        var->changed = true;
        fputc(' ', cfddns.fout);
        fputc(' ', cfddns.flog);
    } else {
        var->changed = !string_equals_slice(&var->value, s, n);
    }
    // value_now
    string_fwrite(&var->value, cfddns.fout);
    string_fwrite(&var->value, cfddns.flog);
    var->prev = cfddns.vars;
    cfddns.vars = var;
    // <tails>
    return cfddns_line_done(e, var->changed ? " #changed" : "");
}

static void
cfddns_proc_line_user(char *s, char *e) {
    // user_email
    size_t n = e - s;
    string_setlen(&cfddns.user_email_header, STRLEN(HEAD_EMAIL));
    string_concat_slice(&cfddns.user_email_header, s, n - 1);
    string_c_str(&cfddns.user_email_header);
    fwrite(s, sizeof(char), n, cfddns.fout);
    fwrite(s, sizeof(char), n, cfddns.flog);
    // user_apikey
    s = cfddns_line_next(e);
    e = pass_value(s);
    n = e - s;
    if (!n) {
        string_setlen(&cfddns.user_email_header, STRLEN(HEAD_EMAIL));
        string_setlen(&cfddns.user_apikey_header, STRLEN(HEAD_APIKEY));
        return cfddns_line_done(e, " #need_apikey");
    }
    string_setlen(&cfddns.user_apikey_header, STRLEN(HEAD_APIKEY));
    string_concat_slice(&cfddns.user_apikey_header, s, n);
    string_c_str(&cfddns.user_apikey_header);
    fwrite(s, sizeof(char), n, cfddns.fout);
    #ifdef LOG_SECRETS
    fwrite(s, sizeof(char), n, cfddns.flog);
    #endif
    return cfddns_line_done(e, "");
}

static void
cfddns_proc_line_zone(char *s, char *e) {
    // zone_name
    size_t n = e - s;
    string_copy_slice(&cfddns.zone_name, s, n - 1);
    fwrite(s, sizeof(char), n, cfddns.fout);
    fwrite(s, sizeof(char), n, cfddns.flog);
    // zone_id
    s = cfddns_line_next(e);
    e = pass_value(s);
    n = e - s;
    if (!n) {
        fputc(' ', cfddns.fout);
        fputc(' ', cfddns.flog);
        fflush(cfddns.flog);
        cfddns_get_zone_id(&cfddns.zone_id);
    } else {
        string_copy_slice(&cfddns.zone_id, s, n);
    }
    string_fwrite(&cfddns.zone_id, cfddns.fout);
    #ifdef LOG_SECRETS
    string_fwrite(&cfddns.zone_id, cfddns.flog);
    #endif
    return cfddns_line_done(e, n ? "" : " #got_zone_id");
}

static void
cfddns_proc_line_record(char *s, char *e) {
    // type
    size_t n = e - s;
    string_copy_slice(&cfddns.record_type, s, n);
    fwrite(s, sizeof(char), n, cfddns.fout);
    fwrite(s, sizeof(char), n, cfddns.flog);
    // name
    s = cfddns_line_next(e);
    e = pass_value(s);
    n = e - s;
    if (!n) {
        return cfddns_line_done(e, " #need_name");
    }
    string_copy_slice(&cfddns.record_name, s, n);
    fwrite(s, sizeof(char), n, cfddns.fout);
    fwrite(s, sizeof(char), n, cfddns.flog);
    // var_key => var
    s = cfddns_line_next(e);
    e = pass_value(s);
    n = e - s;
    if (!n) {
        return cfddns_line_done(e, " #need_var_key");
    }
    fwrite(s, sizeof(char), n, cfddns.fout);
    fwrite(s, sizeof(char), n, cfddns.flog);
    variable *var = cfddns.vars;
    for (;; var = var->prev) {
        if (var == NULL) {
            return cfddns_line_done(e, " #var_not_bind");
        } else if (string_equals_slice(&var->key, s, n)) {
            break;
        }
    }
    // content
    string_clear(&cfddns.record_content);
    // record_id => (update)
    s = cfddns_line_next(e);
    e = pass_value(s);
    n = e - s;
    bool success;
    char *log;
    if (n == 0 || (n == 1 && e[-1] == '!')) {
        success = false;
    } else if (e[-1] == '!') {
        string_copy_slice(&cfddns.record_id, s, n - 1);
        success = cfddns_update_record(&var->value);
        if (success) log = " #updated";
    } else {
        string_copy_slice(&cfddns.record_id, s, n);
        success = !var->changed;
        if (success) log = " #var_not_changed";
    }
    if (!success) {
        if (!cfddns_get_record_id(&cfddns.record_id, &cfddns.record_content)) {
            log = " #get_record_id_failed";
        } else if (string_equals_string(&var->value, &cfddns.record_content)) {
            log = " #already_up_to_date";
            success = true;
        } else if (cfddns_update_record(&var->value)) {
            log = " #updated";
            success = true;
        } else {
            log = " #update_failed";
        }
    }
    if (!is_space(s[-1])) {
        fputc(' ', cfddns.fout);
        fputc(' ', cfddns.flog);
    }
    string_fwrite(&cfddns.record_id, cfddns.fout);
    #ifdef LOG_SECRETS
    string_fwrite(&cfddns.record_id, cfddns.flog);
    #endif
    if (!success) {
        fputc('!', cfddns.fout);
        fputc('!', cfddns.flog);
        if (e[0] == ' ' && !is_value(e[1])) ++e;
    } else if (e[-1] == '!') {
        fputc(' ', cfddns.fout);
        fputc(' ', cfddns.flog);
    }
    return cfddns_line_done(e, log);
}

static void
cfddns_proc() {
    while (fgets(cfddns.buff, BUFF_SIZE, cfddns.fin)) {
        char *s, *e;
        s = cfddns_line_next(cfddns.buff);
        e = pass_value(s);
        if (s == e) {
            cfddns_line_done(e, "");
            continue;
        }
        switch (e[-1]) {
            case '?': {
                cfddns_proc_line_var(s, e);
                break;
            }
            case ':': {
                cfddns_proc_line_user(s, e);
                break;
            }
            case '/': {
                cfddns_proc_line_zone(s, e);
                break;
            }
            default: {
                cfddns_proc_line_record(s, e);
            }
        }
    }
}

void usage(FILE *file) {
    fputs("Usage: ", file);
    fputs(BASENAME, file);
    fputs(" [config_file]\n", file);
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
        usage(stderr);
        return 1;
    } else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        usage(stdout);
        return 0;
    }
    cfddns.fin = fopen(argv[1], "r");
    if (!cfddns.fin) {
        perror(BASENAME);
        return 1;
    }
    cfddns.fout = tmpfile();
    if (!cfddns.fout) {
        perror(BASENAME);
        fclose(cfddns.fin);
        return 1;
    }
    cfddns.flog = stdout;
    cfddns_init();
    cfddns_proc();
    cfddns_cleanup();
    fclose(cfddns.fin);
    cfddns.fin = cfddns.fout;
    cfddns.fout = fopen(argv[1], "w");
    if (!cfddns.fout) {
        perror(BASENAME);
        fclose(cfddns.fin);
        return 1;
    }
    rewind(cfddns.fin);
    for (size_t n; (n = fread(cfddns.buff, sizeof(char), BUFF_SIZE, cfddns.fin));) {
        fwrite(cfddns.buff, sizeof(char), n, cfddns.fout);
    }
    fclose(cfddns.fin);
    fclose(cfddns.fout);
    return 0;
}
