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

#define UNUSED __attribute__((unused))
#define STRLEN(STR) (sizeof(STR) - 1)
#define STARTS_WITH(STR, str, len) (len < sizeof(STR) && memcmp(STR, str, len))

// #define declare_header(Identifier, HEAD, str) \
// char Identifier[STRLEN(HEAD) + str.len + 1]; \
// memcpy(Identifier, HEAD, STRLEN(HEAD)); \
// memcpy(Identifier + STRLEN(HEAD), str.data, str.len); \
// Identifier[STRLEN(HEAD) + str.len] = '\0'

#define MAXSTR 254
#define MAXLINE PIPE_BUF
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

typedef struct string {
    unsigned char len;
    char data[MAXSTR + 1];
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
    memcpy(str->data, s, str->len = e - s < MAXSTR ? e - s : MAXSTR);
}

static void string_concat(string *str, const char *s, const char *e) {
    size_t n = e - s < MAXSTR - str->len ? e - s : MAXSTR - str->len;
    memcpy(str->data + str->len, s, n);
    str->len += n;
}

static char *string_tostr(string *str) {
    str->data[str->len] = '\0';
    return str->data;
}

static size_t string_fwrite(const string *str, FILE *file) {
    return fwrite(str->data, sizeof(char), str->len, file);
}

static size_t fputss(const char *start, const char *end, FILE *file) {
    return fwrite(start, sizeof(char), end - start, file);
}

static size_t
curl_get_string_callback(
        const char *data,
        const size_t UNUSED(char_size),
        const size_t len,
        string *str
) {
    if (str->len < MAXSTR) {
        const char *end = data + len;
        for (const char *s = data; s < end; ++s) {
            if (*s != '\0' && *s != '\n' && *s != ' ') {
                const char *e = s + 1;
                while (e < end && *e != '\0' && *e != '\n' && *e != ' ') ++e;
                size_t n = e - s;
                if (n > MAXSTR - str->len)
                    n = MAXSTR - str->len;
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
        char *start = json + STRLEN(JSON_RESULT_HEAD_OF_GET_ZONE_ID);
        char *end = json + (size < MAXSTR ? size : MAXSTR);
        for (char *i = start; i < end; ++i) {
            if (*i == '"') {
                string_copy(&zone_id, start, i);
                return size;
            }
        }
    }
    return 0;
}

static void string_curl(string *str, const char *url) {
    string_clear(*str);
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_get_string_callback);
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

typedef struct context {
    string user_email_header;
    string user_apikey_header;
    string zone_id;
    string zone_name;
    string record_id;
    string record_name;
    string record_type;
    variable *vars;
    struct curl_slist *headers;
} context;

static void cfddns_context_init(context *ctx) {
    string_init(ctx->user_email_header, HEAD_EMAIL);
    string_init(ctx->user_apikey_header, HEAD_APIKEY);
    string_clear(ctx->zone_name);
    string_clear(ctx->zone_id);
    string_clear(ctx->record_name);
    string_clear(ctx->record_type);
    string_clear(ctx->record_id);
    ctx->vars = NULL;
    ctx->headers = NULL;
    ctx->headers = curl_slist_append(ctx->headers, ctx->user_email_header.data);
    ctx->headers = curl_slist_append(ctx->headers, ctx->user_apikey_header.data);
    ctx->headers = curl_slist_append(ctx->headers, "Content-Type: application/json");
}


static void
cfddns_get_zone_id(const context *ctx, string *zone_id) {
    string_clear(*zone_id);

    CURL *curl = curl_easy_init();
    if (!curl) return;

    char url[255];
    // TODO: make url

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HEADER, ctx->headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_get_zone_id_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, zone_id);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

static void
cfddns_get_record_id(const context *ctx, string *record_id) {
    // TODO
}

static bool
cfddns_update_record(const context *ctx, const string *content) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char url[STRLEN(URL_BASE) + 1 + ctx->zone_id.len + STRLEN(URL_DNS_RECORDS) + id->len + 1];
    size_t url_len = 0;

    memcpy(url + url_len, URL_BASE, STRLEN(URL_BASE));
    url_len += STRLEN(URL_BASE);

    *(url + url_len) = '/';
    url_len += 1;

    memcpy(url + url_len, ctx->zone_id.data, ctx->zone_id.len);
    url_len += ctx->zone_id.len;

    memcpy(url + url_len, URL_DNS_RECORDS, STRLEN(URL_DNS_RECORDS));
    url_len += STRLEN(URL_DNS_RECORDS);

    memcpy(url + url_len, id->data, id->len);
    url_len += id->len;

    *(url + url_len) = '\0';

    string request;
    // TODO: make json

    string response;

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, ctx->headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_get_string_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    // TODO: check result
    return strstr(string_tostr(&response), "success");
}

static char *pass_space(char *i) {
    while (*i == ' ') ++i;
    if (*i == '#') for (++i; *i; ++i);
    return i;
}

static bool is_value(char c) {
    return c && c != ' ' && c != '#';
}

static char *pass_value(char *i) {
    while (is_value(*i)) ++i;
    return i;
}

static int cfddns_main(FILE *fin, FILE *fout, FILE *ferr) {
    context ctx;
    cfddns_context_init(&ctx);
    for (char line[MAXLINE]; fgets(line, MAXLINE, fin); fputc('\n', fout)) {
        char *s, *e = line;
        fputss(e, s = pass_space(e), fout);
        if (s == (e = pass_value(s))) continue;
        switch (e[-1]) {
            case '?': {
                variable *var = malloc(sizeof(variable));
                if (!var) {
                    fputss(s, e, fout);
                    fputs(e, fout);
                    fputs(" #need_memory", ferr);
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
                    fputs(e, fout);
                    fputs(" #need_url", ferr);
                    break;
                }
                fputss(s, e, fout);
                // url => var_value
                char _e = *e;
                *e = '\0';
                string_curl(&var->value, s);
                *e = _e;
                // value_old => var_changed
                fputss(e, s = pass_space(e), fout);
                e = pass_value(s);
                var->changed = !string_compare(&var->value, s, e - s);
                // var_value
                string_fwrite(&var->value, fout);
                // tails
                fputs(e, fout);
                // vars
                var->prev = ctx.vars;
                ctx.vars = var;
                break;
            }
            case ':': {
                // user_email_header
                string_cut(ctx.user_apikey_header, STRLEN(HEAD_EMAIL));
                string_concat(&ctx.user_email_header, s, e - 1);
                string_tostr(&ctx.user_email_header);
                fputss(s, e, fout);
                // user_apikey_header
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    string_cut(ctx.user_apikey_header, STRLEN(HEAD_EMAIL));
                    string_cut(ctx.user_apikey_header, STRLEN(HEAD_APIKEY));
                    fputs(e, fout);
                    fputs(" #need_apikey", ferr);
                    break;
                }
                string_cut(ctx.user_apikey_header, STRLEN(HEAD_APIKEY));
                string_concat(&ctx.user_apikey_header, s, e);
                string_tostr(&ctx.user_email_header);
                fputss(s, e, fout);
                // tails
                fputs(e, fout);
                break;
            }
            case '\\': {
                // zone_name
                string_copy(&ctx.zone_name, s, e - 1);
                string_fwrite(&ctx.zone_name, fout);
                fputc('\\', fout);
                // zone_id
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    cfddns_get_zone_id(&ctx, &ctx.zone_id);
                } else {
                    string_copy(&ctx.zone_id, s, e);
                }
                string_fwrite(&ctx.zone_id, fout);
                // tails
                fputs(e, fout);
                break;
            }
            default: {
                // var_key
                variable *var = ctx.vars;
                size_t var_key_len = e - s;
                fwrite(s, 1, var_key_len, fout);
                while (var && !string_compare(&var->key, s, var_key_len)) {
                    var = var->prev;
                }
                if (!var) {
                    fputs(e, fout);
                    fputs(" #var_undefined", ferr);
                    break;
                }
                // type
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    fputs(e, fout);
                    fputs(" #need_type", ferr);
                    break;
                }
                string_copy(&ctx.record_type, s, e);
                // name
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    fputs(e, fout);
                    fputs(" #need_name", ferr);
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
                    cfddns_get_record_id(&ctx);
                    success = cfddns_update_record(&ctx, &var->value);
                } else {
                    success = false;
                }
                string_fwrite(&ctx.record_id, fout);
                // tails
                if (!success) {
                    fputc('!', fout);
                    if (*e == ' ' && !is_value(e[1])) ++e;
                } else if (e[-1] == '!') {
                    fputc(' ', fout);
                }
                fputs(e, fout);
            }
        }
    }
    for (variable *var = ctx.vars; var; var = var->prev) {
        free(var);
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
