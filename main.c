#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <curl/curl.h>

#define URL_BASE "https://api.cloudflare.com/client/v4/zones"
#define URL_DNS_RECORDS "/dns_records/"
#define HEAD_EMAIL "X-Auth-Email: "
#define HEAD_APIKEY "X-Auth-Key: "
#define JSON_RESULT_HEAD_OF_GET_ZONE_ID "{\"result\":[{\"id\":\""

#define STR_MAX 254 // max=254
#define URL_MAX 2048
#define JSON_MAX 2048

static bool is_value(char c) {
    return c && c != '\n' && c != ' ' && c != '\t' && c != '#';
}

static bool is_end(char c) {
    return c == '\0' || c == '\n';
}

static bool is_space(char c) {
    return c == ' ' || c == '\t';
}

static char *pass_value(char *i) {
    while (is_value(*i)) ++i;
    return i;
}

static char *pass_space(char *i) {
    while (is_space(*i)) ++i;
//    if (*i == '#') for (++i; !is_end(*i); ++i);
    return i;
}

static char *pass_line(char *i) {
    while (!is_end(*i)) ++i;
    return i;
}

#define UNUSED __attribute__((unused))
#define STRLEN(STR) (sizeof(STR) - 1)
#define STARTS_WITH(STR, str, len) (len < STRLEN(STR) && memcmp(STR, str, len))

static size_t fputss(const char *start, const char *end, FILE *file) {
    return fwrite(start, sizeof(char), end - start, file);
}

#define strcat_literal(var, STR) memcpy(var + var##_len, STR, STRLEN(STR)); var##_len += STRLEN(STR)
#define strcat_char(var, c) var[var##_len++] = c;
#define strcat_string(var, str) memcpy(var + var##_len, str.data, str.len); var##_len += str.len

char BASENAME[NAME_MAX + 1];

// static const char *pass_ipv4_init_part(const char *c) {
//     if (c[0] < '0' || c[0] > '9') return 0;
//     if (c[1] == '.') return c + 2;
//     if (c[1] < '0' || c[1] > '9') return 0;
//     if (c[2] == '.') return c + 3;
//     if (c[2] < '0' || c[2] > '9') return 0;
//     if (c[3] != '.') return 0;
//     if ((((unsigned) (c[0] - '0') * 10) +
//          (unsigned) (c[1] - '0')) * 10 +
//         (unsigned) (c[2] - '0') <= 255)
//         return c + 3;
//     return 0;
// }

// static const char *pass_ipv4_last_part(const char *c) {
//     if (c[0] < '0' || c[0] > '9') return 0;
//     if (c[1] < '0' || c[1] > '9') return c + 1;
//     if (c[2] < '0' || c[2] > '9') return c + 2;
//     if ((((unsigned) (c[0] - '0') * 10) +
//          (unsigned) (c[1] - '0')) * 10 +
//         (unsigned) (c[2] - '0') <= 255)
//         return c + 3;
//     return 0;
// }

// static const char *pass_ipv4(const char *c) {
//     if (!(c = pass_ipv4_init_part(c))) return 0;
//     if (!(c = pass_ipv4_init_part(c))) return 0;
//     if (!(c = pass_ipv4_init_part(c))) return 0;
//     return pass_ipv4_last_part(c);
// }

typedef struct {
    unsigned char len;
    char data[255];
} string;

#define string_clear(str) (str).len = 0

#define string_init(str, STR) memcpy((str).data, STR, (str).len = STRLEN(STR))

#define string_cut(str, _len) (str).len = (_len)

// static bool string_equals(const string *str, const string *_str) {
//     return str->len == _str->len && memcmp(str->data, _str->data, str->len);
// }

static bool string_compare(const string *str, const char *s, const size_t n) {
    return str->len == n && memcmp(str->data, s, n);
}

// static void string_clone(string *dst, const string *src) {
//     memcpy(dst, src, src->len + 1);
// }

static void string_copy(string *str, const char *s, const char *e) {
    memcpy(str->data, s, str->len = e - s < STR_MAX ? e - s : STR_MAX);
}

static void string_concat(string *str, const char *s, const char *e) {
    size_t n = e - s < STR_MAX - str->len ? e - s : STR_MAX - str->len;
    memcpy(str->data + str->len, s, n);
    str->len += n;
}

static void string_c_str(string *str) {
    str->data[str->len] = '\0';
}

static size_t string_fwrite(const string *str, FILE *file) {
    return fwrite(str->data, sizeof(char), str->len, file);
}

static size_t
curl_get_string_callback(
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
curl_get_zone_id_callback(
        const char *json,
        const size_t UNUSED(char_size),
        const size_t size,
        string *zone_id
) {
    if (STARTS_WITH(JSON_RESULT_HEAD_OF_GET_ZONE_ID, json, size)) {
        const char *const start = json + STRLEN(JSON_RESULT_HEAD_OF_GET_ZONE_ID);
        const char *const end = json + (size < STR_MAX ? size : STR_MAX);
        for (const char *i = start; i < end; ++i) {
            if (*i == '"') {
                string_copy(zone_id, start, i);
                return size;
            }
        }
    }
    return 0;
}

static void string_curl(string *str, const char *url) {
    string_clear(*str);
    CURL *curl = curl_easy_init();
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_get_string_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, str);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

struct variable {
    struct variable *prev;
    string key;
    string value;
    bool changed;
};

struct context {
    string user_email_header;
    string user_apikey_header;
    string zone_id;
    string zone_name;
    string record_id;
    string record_name;
    string record_type;
    struct variable *vars;
    struct curl_slist *headers;
};

static void cfddns_context_init(struct context *ctx) {
    string_init(ctx->user_email_header, HEAD_EMAIL);
    string_init(ctx->user_apikey_header, HEAD_APIKEY);
    string_clear(ctx->zone_id);
    string_clear(ctx->zone_name);
    string_clear(ctx->record_id);
    string_clear(ctx->record_name);
    string_clear(ctx->record_type);
    ctx->vars = NULL;
    ctx->headers = NULL;
    ctx->headers = curl_slist_append(ctx->headers, ctx->user_email_header.data);
    ctx->headers = curl_slist_append(ctx->headers, ctx->user_apikey_header.data);
    ctx->headers = curl_slist_append(ctx->headers, "Content-Type: application/json");
}


static void
cfddns_get_zone_id(const struct context *ctx, string *zone_id) {
    string_clear(*zone_id);

    CURL *curl = curl_easy_init();
    if (!curl) return;

    char url[URL_MAX];
    // TODO: make url

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HEADER, ctx->headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_get_zone_id_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, zone_id);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

static void
cfddns_get_record_id(const struct context *ctx, string *record_id) {
    // TODO
}

static bool
cfddns_update_record(const struct context *ctx, const string *record_content) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char url[URL_MAX];
    size_t url_len = 0;
    strcat_literal(url, URL_BASE);
    strcat_char   (url, '/');
    strcat_string (url, ctx->zone_id);
    strcat_literal(url, URL_DNS_RECORDS);
    strcat_string (url, ctx->record_id);
    url[url_len] = '\0';

    char request[JSON_MAX];
    size_t request_len = 0;
    // TODO: make json

    string response;
    string_clear(response);

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, ctx->headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_get_string_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    // TODO: check result
    string_c_str(&response);
    return strstr(response.data, "success");
}

static int cfddns_main(FILE *fin, FILE *fout, FILE *ferr) {
    struct context ctx;
    cfddns_context_init(&ctx);
    for (char line[LINE_MAX]; fgets(line, LINE_MAX, fin); fputc('\n', fout)) {
        char *s, *e = line;
        fputss(e, s = pass_space(e), fout);
        if (s == (e = pass_value(s))) {
            fputss(e, pass_line(e), fout);
            continue;
        }
        switch (e[-1]) {
            case '?': {
                struct variable *var = malloc(sizeof(struct variable));
                if (!var) {
                    fputss(s, e, fout);
                    fputss(e, pass_line(e), fout);
                    fflush(fout);
                    fputs(" #need_memory", ferr);
                    fflush(ferr);
                    break;
                }
                // var_key
                string_copy(&var->key, s, e - 1);
                string_fwrite(&var->key, fout);
                fputc('?', fout);
                // url
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    free(var);
                    fputss(e, pass_line(e), fout);
                    fflush(fout);
                    fputs(" #need_url", ferr);
                    fflush(ferr);
                    break;
                }
                fputss(s, e, fout);
                // url => var_value
                char _e = *e;
                *e = '\0';
                string_curl(&var->value, s);
                *e = _e;
                if (!var->value.len) {
                    fputss(e, pass_line(e), fout);
                    fflush(fout);
                    fputs(" #request_failed", ferr);
                    fflush(ferr);
                    break;
                }
                // value_old => var_changed
                fputss(e, s = pass_space(e), fout);
                e = pass_value(s);
                var->changed = !string_compare(&var->value, s, e - s);
                // var_value
                if (!is_space(s[-1])) fputc(' ', fout);
                string_fwrite(&var->value, fout);
                // tails
                fputss(e, pass_line(e), fout);
                // vars
                var->prev = ctx.vars;
                ctx.vars = var;
                break;
            }
            case ':': {
                // user_email_header
                string_cut(ctx.user_apikey_header, STRLEN(HEAD_EMAIL));
                string_concat(&ctx.user_email_header, s, e - 1);
                string_c_str(&ctx.user_email_header);
                fputss(s, e, fout);
                // user_apikey_header
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    string_cut(ctx.user_apikey_header, STRLEN(HEAD_EMAIL));
                    string_cut(ctx.user_apikey_header, STRLEN(HEAD_APIKEY));
                    fputss(e, pass_line(e), fout);
                    fflush(fout);
                    fputs(" #need_apikey", ferr);
                    fflush(ferr);
                    break;
                }
                string_cut(ctx.user_apikey_header, STRLEN(HEAD_APIKEY));
                string_concat(&ctx.user_apikey_header, s, e);
                string_c_str(&ctx.user_email_header);
                fputss(s, e, fout);
                // tails
                fputss(e, pass_line(e), fout);
                break;
            }
            case '/': {
                // zone_name
                string_copy(&ctx.zone_name, s, e - 1);
                string_fwrite(&ctx.zone_name, fout);
                fputc('/', fout);
                // zone_id
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    cfddns_get_zone_id(&ctx, &ctx.zone_id);
                } else {
                    string_copy(&ctx.zone_id, s, e);
                }
                if (!is_space(s[-1])) fputc(' ', fout);
                string_fwrite(&ctx.zone_id, fout);
                // tails
                fputss(e, pass_line(e), fout);
                break;
            }
            default: {
                // var_key
                struct variable *var = ctx.vars;
                size_t var_key_len = e - s;
                fwrite(s, 1, var_key_len, fout);
                while (var && !string_compare(&var->key, s, var_key_len)) {
                    var = var->prev;
                }
                if (!var) {
                    fputss(e, pass_line(e), fout);
                    fflush(fout);
                    fputs(" #var_undefined", ferr);
                    fflush(ferr);
                    break;
                }
                // type
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    fputss(e, pass_line(e), fout);
                    fflush(fout);
                    fputs(" #need_type", ferr);
                    fflush(ferr);
                    break;
                }
                string_copy(&ctx.record_type, s, e);
                // name
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    fputss(e, pass_line(e), fout);
                    fflush(fout);
                    fputs(" #need_name", ferr);
                    fflush(ferr);
                    break;
                }
                string_copy(&ctx.record_name, s, e);
                // id
                bool force_update, may_expired;
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    force_update = true;
                    may_expired = false;
                    cfddns_get_record_id(&ctx, &ctx.record_id);
                } else if (e[-1] == '!') {
                    force_update = true;
                    may_expired = true;
                    string_copy(&ctx.record_id, s, e - 1);
                } else {
                    force_update = false;
                    may_expired = true;
                    string_copy(&ctx.record_id, s, e);
                }
                // do update
                bool success;
                if (!force_update && !var->changed) {
                    success = true;
                } else if (cfddns_update_record(&ctx, &var->value)) {
                    success = true;
                } else if (may_expired) {
                    cfddns_get_record_id(&ctx, &ctx.record_id);
                    success = cfddns_update_record(&ctx, &var->value);
                } else {
                    success = false;
                }
                if (!is_space(s[-1])) fputc(' ', fout);
                string_fwrite(&ctx.record_id, fout);
                // tails
                if (!success) {
                    fputc('!', fout);
                    if (*e == ' ' && !is_value(e[1])) ++e;
                } else if (e[-1] == '!') {
                    fputc(' ', fout);
                }
                fputss(e, pass_line(e), fout);
                break;
            }
        }
    }
    while (ctx.vars) {
        struct variable *prev = ctx.vars->prev;
        free(ctx.vars);
        ctx.vars = prev;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    for (char *i = argv[1], *s = i;;) {
        if (*i) {
            if (*i != '/') ++i; else s = ++i;
        } else {
            memcpy((void *) BASENAME, s, i - s + 1);
            break;
        }
    }
    FILE *fp;
    if (argc < 2) {
        fp = stdin;
    } else {
        fp = fopen(argv[1], "r");
        if (!fp) {
            perror(BASENAME);
            return 1;
        }
    }
    return cfddns_main(fp, stdout, stderr);
}
