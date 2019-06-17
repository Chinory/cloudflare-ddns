#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <linux/limits.h>

#define URL_BASE "https://api.cloudflare.com/client/v4"
#define HEAD_EMAIL "X-Auth-Email: "
#define HEAD_APIKEY "X-Auth-Key: "

#define STR_MAX 254 // max=254
#define LSTR_MAX 1022 // max=65534

static bool is_value(char c) {
    return c && c != '\n' && c != ' ' && c != '\t' && c != '#';
}

static bool is_space(char c) {
    return c == ' ' || c == '\t';
}

static bool is_end(char c) {
    return c == '\0' || c == '\n';
}

static char *pass_value(char *i) {
    while (is_value(*i)) ++i;
    return i;
}

static char *pass_space(char *i) {
    while (is_space(*i)) ++i;
    return i;
}

static char *pass_line(char *i) {
    while (!is_end(*i)) ++i;
    return i;
}

#define UNUSED __attribute__((unused))
#define STRLEN(STR) (sizeof(STR) - 1)

static size_t fputss(const char *start, const char *end, FILE *file) {
    return fwrite(start, sizeof(char), end - start, file);
}

// static void fputcn(char c, size_t n, FILE *file) {
//     for (; n; --n) fputc(c, file);
// }

char BASENAME[NAME_MAX + 1];

typedef struct string {
    unsigned char len;
    char data[STR_MAX + 1];
} string;

typedef struct lstring {
    uint16_t len;
    char data[LSTR_MAX + 1];
} lstring;

static inline void string_clear(string *str) {
    str->len = 0;
}

static inline void string_set_len(string *str, uint8_t len) {
    str->len = len;
}

static inline void string_make_str(string *str) {
    str->data[str->len] = '\0';
}

static inline bool string_compare(const string *str, const string *_str) {
    return str->len == _str->len && !memcmp(str->data, _str->data, _str->len);
}

static inline bool string_compare_range(const string *str, const char *s, const char *e) {
    return str->len == e - s && !memcmp(str->data, s, e - s);
}

static inline bool string_compare_slice(const string *str, const char *s, size_t n) {
    return str->len == n && !memcmp(str->data, s, n);
}

static inline void string_copy_str(string *str, const char *s) {
    memcpy(str->data, s, str->len = strlen(s));
}

static inline void string_copy_range(string *str, const char *s, const char *e) {
    memcpy(str->data, s, str->len = e - s < STR_MAX ? e - s : STR_MAX);
}

static inline void string_concat_range(string *str, const char *s, const char *e) {
    size_t n = e - s;
    if (n > STR_MAX - str->len)
        n = STR_MAX - str->len;
    memcpy(str->data + str->len, s, n);
    str->len += n;
}

static inline size_t string_fwrite(const string *str, FILE *file) {
    return fwrite(str->data, sizeof(char), str->len, file);
}

static inline void lstring_clear(lstring *lstr) {
    lstr->len = 0;
}

static inline void lstring_make_str(lstring *lstr) {
    lstr->data[lstr->len] = '\0';
}

static inline void lstring_concat_char(lstring *lstr, char c) {
    if (lstr->len < LSTR_MAX) {
        lstr->data[lstr->len++] = c;
    }
}

static inline void lstring_concat_str(lstring *lstr, const char *s) {
    size_t n = strlen(s);
    if (n > LSTR_MAX - lstr->len)
        n = LSTR_MAX - lstr->len;
    memcpy(lstr->data + lstr->len, s, n);
    lstr->len += n;
}

static inline void lstring_concat_string(lstring *lstr, const string *str) {
    size_t n = str->len;
    if (n > LSTR_MAX - lstr->len)
        n = LSTR_MAX - lstr->len;
    memcpy(lstr->data + lstr->len, str->data, n);
    lstr->len += n;
}


static size_t
curl_string_getln_callback(
        const char *data,
        const size_t UNUSED(char_size),
        const size_t len,
        string *str
) {
    if (str->len < STR_MAX) {
        const char *end = data + len;
        for (const char *s = data; s < end; ++s) {
            if (*s != '\0' && *s != '\n' && *s != ' ') {
                const char *e = s + 1;
                while (e < end && *e != '\0' && *e != '\n' && *e != ' ') ++e;
                size_t n = e - s;
                if (n > STR_MAX - str->len)
                    n = STR_MAX - str->len;
                memcpy(str->data + str->len, s, n);
                str->len += n;
                break;
            }
        }
    }
    return len;
}

static size_t
curl_lstring_getln_callback(
        const char *data,
        const size_t UNUSED(char_size),
        const size_t len,
        lstring *lstr
) {
    if (lstr->len < LSTR_MAX) {
        const char *end = data + len;
        for (const char *s = data; s < end; ++s) {
            if (*s != '\0') {
                const char *e = s + 1;
                while (e < end && *e != '\0') ++e;
                size_t n = e - s;
                if (n > LSTR_MAX - lstr->len)
                    n = LSTR_MAX - lstr->len;
                memcpy(lstr->data + lstr->len, s, n);
                lstr->len += n;
                break;
            }
        }
    }
    return len;
}

static void curl_string_getln(const char *url, string *str) {
    string_clear(str);
    CURL *curl = curl_easy_init();
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_string_getln_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, str);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

typedef struct variable {
    struct variable *prev;
    string key;
    string value;
    bool changed;
} variable;

struct context {
    string user_email_header;
    string user_apikey_header;
    string zone_id;
    string zone_name;
    string record_id;
    string record_type;
    string record_name;
    string record_content;
    variable *vars;
};

static void
cfddns_context_init(struct context *ctx) {
    string_copy_str(&ctx->user_email_header, HEAD_EMAIL);
    string_copy_str(&ctx->user_apikey_header, HEAD_APIKEY);
    string_clear(&ctx->zone_id);
    string_clear(&ctx->zone_name);
    string_clear(&ctx->record_id);
    string_clear(&ctx->record_type);
    string_clear(&ctx->record_name);
    string_clear(&ctx->record_content);
    ctx->vars = NULL;
}

static void
cfddns_context_cleanup(struct context *ctx) {
    while (ctx->vars) {
        variable *prev = ctx->vars->prev;
        free(ctx->vars);
        ctx->vars = prev;
    }
}


static void
cfddns_get_zone_id(const struct context *ctx, string *zone_id) {
    string_clear(zone_id);

    CURL *curl = curl_easy_init();
    if (!curl) return;

    lstring url;
    lstring_clear(&url);
    lstring_concat_str(&url, URL_BASE);
    lstring_concat_str(&url, "/zones?name=");
    lstring_concat_string(&url, &ctx->zone_name);
    lstring_make_str(&url);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, ctx->user_email_header.data);
    headers = curl_slist_append(headers, ctx->user_apikey_header.data);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    lstring response;
    lstring_clear(&response);
    curl_easy_setopt(curl, CURLOPT_URL, url.data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_lstring_getln_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    lstring_make_str(&response);

    curl_slist_free_all(headers);

    char *s = strstr(response.data, "\"result\":[{\"id\":\"");
    if (!s) return;
    char *e = strchr(s += STRLEN("\"result\":[{\"id\":\""), '"');
    if (!e) return;
    string_copy_range(zone_id, s, e);
}

static bool
cfddns_get_record_id(const struct context *ctx, string *record_id, string *record_content) {
    string_clear(record_id);
    string_clear(record_content);

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    lstring url;
    lstring_clear(&url);
    lstring_concat_str(&url, URL_BASE);
    lstring_concat_str(&url, "/zones/");
    lstring_concat_string(&url, &ctx->zone_id);
    lstring_concat_str(&url, "/dns_records?type=");
    lstring_concat_string(&url, &ctx->record_type);
    lstring_concat_str(&url, "&name=");
    if (ctx->record_name.len != 1 || ctx->record_name.data[0] != '@') {
        lstring_concat_string(&url, &ctx->record_name);
        lstring_concat_char(&url, '.');
    }
    lstring_concat_string(&url, &ctx->zone_name);
    lstring_make_str(&url);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, ctx->user_email_header.data);
    headers = curl_slist_append(headers, ctx->user_apikey_header.data);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    lstring response;
    lstring_clear(&response);
    curl_easy_setopt(curl, CURLOPT_URL, url.data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_lstring_getln_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    lstring_make_str(&response);

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
cfddns_update_record(const struct context *ctx, const string *record_content) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    lstring url;
    lstring_clear(&url);
    lstring_concat_str(&url, URL_BASE);
    lstring_concat_str(&url, "/zones/");
    lstring_concat_string(&url, &ctx->zone_id);
    lstring_concat_str(&url, "/dns_records/");
    lstring_concat_string(&url, &ctx->record_id);
    lstring_make_str(&url);

    lstring request;
    lstring_clear(&request);
    lstring_concat_str(&request, "{\"type\":\"");
    lstring_concat_string(&request, &ctx->record_type);
    lstring_concat_str(&request, "\",\"name\":\"");
    lstring_concat_string(&request, &ctx->record_name);
    lstring_concat_str(&request, "\",\"content\":\"");
    lstring_concat_string(&request, record_content);
    lstring_concat_str(&request, "\"}");
    lstring_make_str(&request);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, ctx->user_email_header.data);
    headers = curl_slist_append(headers, ctx->user_apikey_header.data);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    lstring response;
    lstring_clear(&response);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_URL, url.data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.data);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_lstring_getln_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    lstring_make_str(&response);

    curl_slist_free_all(headers);

    return strstr(response.data, "\"success\":true");
}

static int cfddns_main(FILE *fin, FILE *fout, FILE *flog) {
    struct context ctx;
    cfddns_context_init(&ctx);
    char line[PIPE_BUF];
    while (fgets(line, PIPE_BUF, fin)) {
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
                    variable *var = ctx.vars;
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
                    curl_string_getln(s, &var->value);
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
                    var->prev = ctx.vars;
                    ctx.vars = var;
                    break;
                }
                case ':': {
                    // user_email_header
                    string_set_len(&ctx.user_email_header, STRLEN(HEAD_EMAIL));
                    string_concat_range(&ctx.user_email_header, s, e - 1);
                    string_make_str(&ctx.user_email_header);
                    fputss(s, e, fout);
                    fputss(s, e, flog);
                    // user_apikey_header
                    s = pass_space(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    e = pass_value(s);
                    if (s == e) {
                        string_set_len(&ctx.user_email_header, STRLEN(HEAD_EMAIL));
                        string_set_len(&ctx.user_apikey_header, STRLEN(HEAD_APIKEY));
                        s = pass_line(e);
                        fputss(e, s, fout);
                        fputss(e, s, flog);
                        fputs(" #need_apikey", flog);
                        break;
                    }
                    string_set_len(&ctx.user_apikey_header, STRLEN(HEAD_APIKEY));
                    string_concat_range(&ctx.user_apikey_header, s, e);
                    string_make_str(&ctx.user_apikey_header);
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
                    string_copy_range(&ctx.zone_name, s, e - 1);
                    string_fwrite(&ctx.zone_name, fout);
                    string_fwrite(&ctx.zone_name, flog);
                    fputc('/', fout);
                    fputc('/', flog);
                    // zone_id
                    s = pass_space(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    e = pass_value(s);
                    if (s == e) {
                        fflush(flog);
                        cfddns_get_zone_id(&ctx, &ctx.zone_id);
                    } else {
                        string_copy_range(&ctx.zone_id, s, e);
                    }
                    if (!is_space(s[-1])) {
                        fputc(' ', fout);
                        fputc(' ', flog);
                    }
                    string_fwrite(&ctx.zone_id, fout);
                    // string_fwrite(&ctx.zone_id, flog);
                    // fputcn(' ', ctx.zone_id.len, flog);
                    // tails
                    s = pass_line(e);
                    fputss(e, s, fout);
                    fputss(e, s, flog);
                    break;
                }
                default: {
                    // type
                    string_copy_range(&ctx.record_type, s, e);
                    string_fwrite(&ctx.record_type, fout);
                    string_fwrite(&ctx.record_type, flog);
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
                    string_copy_range(&ctx.record_name, s, e);
                    string_fwrite(&ctx.record_name, fout);
                    string_fwrite(&ctx.record_name, flog);
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
                    variable *var = ctx.vars;
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
                    string_clear(&ctx.record_content);
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
                        cfddns_get_record_id(&ctx, &ctx.record_id, &ctx.record_content);
                    } else if (e[-1] == '!') {
                        force_update = true;
                        may_expired = true;
                        if (e - s == 1) {
                            cfddns_get_record_id(&ctx, &ctx.record_id, &ctx.record_content);
                        } else {
                            string_copy_range(&ctx.record_id, s, e - 1);
                        }
                    } else {
                        force_update = false;
                        may_expired = true;
                        string_copy_range(&ctx.record_id, s, e);
                    }
                    // do update
                    bool success;
                    if (!ctx.record_id.len) {
                        success = false;
                    } else if (!force_update && !var->changed) {
                        success = true;
                    } else if (string_compare(&ctx.record_content, &var->value) ||
                               cfddns_update_record(&ctx, &var->value)) {
                        success = true;
                    } else if (may_expired) {
                        cfddns_get_record_id(&ctx, &ctx.record_id, &ctx.record_content);
                        success = string_compare(&ctx.record_content, &var->value) ||
                                  cfddns_update_record(&ctx, &var->value);
                    } else {
                        success = false;
                    }
                    if (!is_space(s[-1])) {
                        fputc(' ', fout);
                        fputc(' ', flog);
                    }
                    string_fwrite(&ctx.record_id, fout);
                    // string_fwrite(&ctx.record_id, flog);
                    // fputcn(' ', ctx.record_id.len, flog);
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
    cfddns_context_cleanup(&ctx);
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
