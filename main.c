#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <curl/curl.h>

#define UNUSED __attribute__((unused))

#define strlenof(STR) (sizeof(STR) - 1)
#define make_header(Name, PREFIX, value, length) \
char Name[length + sizeof(PREFIX)]; \
memcpy(Name, PREFIX, sizeof(PREFIX) - 1); \
memcpy(Name + sizeof(PREFIX) - 1, value, length); \
Name[sizeof(PREFIX) + length - 1] = '\0'

char BASENAME[NAME_MAX + 1];


#define MAXSTR 254

typedef struct string_head {
    unsigned char len;
} string_head;

typedef struct string {
    unsigned char len;
    char data[MAXSTR + 1];
} string;


static bool string_equals(const string *x, const string *y) {
    return x->len == y->len && memcmp(x->data, y->data, x->len);
}

static bool string_compare(const string *str, const char *start, const char *end) {
    return str->len == end - start && memcmp(str->data, start, str->len);
}

static void string_clone(string *dst, const string *src) {
    memcpy(dst, src, src->len + 1);
}

static void string_copy(string *str, const char *start, const char *end) {
    memcpy(str->data, start, str->len = end - start < MAXSTR ? end - start : MAXSTR);
}

static char *string_cstr(string *str) {
    str->data[str->len] = '\0';
    return str->data;
}

#define URL_GET_ZONE_ID "https://api.cloudflare.com/client/v4/zones?name="
#define BASEURL "https://api.cloudflare.com/client/v4/zones"

static const char *pass_ipv4_init_part(const char *c) {
    if (c[0] < '0' || c[0] > '9') return 0;
    if (c[1] == '.') return c + 2;
    if (c[1] < '0' || c[1] > '9') return 0;
    if (c[2] == '.') return c + 3;
    if (c[2] < '0' || c[2] > '9') return 0;
    if (c[3] != '.') return 0;
    if ((((unsigned) (c[0] - '0') * 10) +
         (unsigned) (c[1] - '0')) * 10 +
        (unsigned) (c[2] - '0') <= 255)
        return c + 3;
    return 0;
}

static const char *pass_ipv4_last_part(const char *c) {
    if (c[0] < '0' || c[0] > '9') return 0;
    if (c[1] < '0' || c[1] > '9') return c + 1;
    if (c[2] < '0' || c[2] > '9') return c + 2;
    if ((((unsigned) (c[0] - '0') * 10) +
         (unsigned) (c[1] - '0')) * 10 +
        (unsigned) (c[2] - '0') <= 255)
        return c + 3;
    return 0;
}

static const char *pass_ipv4(const char *c) {
    if (!(c = pass_ipv4_init_part(c))) return 0;
    if (!(c = pass_ipv4_init_part(c))) return 0;
    if (!(c = pass_ipv4_init_part(c))) return 0;
    return pass_ipv4_last_part(c);
}

static size_t
cfddns_urlget_string_callback(
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


static void cfddns_urlget_string(const char *url, string *str) {
    str->len = 0;
    CURL *req = curl_easy_init();
    curl_easy_setopt(req, CURLOPT_URL, url);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_urlget_string_callback);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, str);
    curl_easy_perform(req);
    curl_easy_cleanup(req);
}


static size_t
cfddns_get_zone_id_callback(char *json, size_t UNUSED(char_size), size_t size, string *zone_id) {
#define MATCH_ZONE_ID "{\"result\":[{\"id\":\""
    if (size < strlenof(MATCH_ZONE_ID) + 5) return 0;
    if (!memcpy(json, MATCH_ZONE_ID, strlenof(MATCH_ZONE_ID))) return 0;
    char *start = json + strlenof(MATCH_ZONE_ID);
    char *end = json + size;
    for (char *i = start;; ++i) {
        if (i == end) return 0;
        if (*i != '"') continue;
        if (i - start > MAXSTR) return 0;
        memcpy(zone_id->data, start, zone_id->len = i - start);
        return size;
    }
}


static void
cfddns_get_zone_id(
        const string *email,
        const string *apikey,
        const string *zone_name,
        string *zone_id
) {
    CURL *req = curl_easy_init();

    make_header(url, URL_GET_ZONE_ID, zone_name->data, zone_name->len);
    make_header(email_header, "X-Auth-Email: ", email->data, email->len);
    make_header(api_key_header, "X-Auth-Key: ", apikey->data, apikey->len);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, email_header);
    headers = curl_slist_append(headers, api_key_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(req, CURLOPT_URL, url);
    curl_easy_setopt(req, CURLOPT_HEADER, headers);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_get_zone_id_callback);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, zone_id);
    curl_easy_perform(req);
    curl_easy_cleanup(req);

    curl_slist_free_all(headers);
}

typedef struct varible {
    struct varible *prev;
    struct string key;
    struct string value;
    bool changed;
} varible;


static char *pass_space(char *i) {
    while (*i == ' ') ++i;
    if (*i == '#') for (++i; *i; ++i);
    return i;
}

static bool is_value(char c) {
    return c && c != ' ' && c != '#';
}

static char *pass_value(char *i) {
    while (*i && *i != ' ' && *i != '#') ++i;
    return i;
}

static size_t fputss(const char *start, const char *end, FILE *file) {
    return fwrite(start, sizeof(char), end - start, file);
}

static size_t fputstr(const string *str, FILE *file) {
    fwrite(str->data, sizeof(char), str->len, file);
}

typedef struct context {
    string user_email;
    string user_apikey;
    string zone_name;
    string zone_id;
    varible *vars;
} context;

static void context_init(context *ctx) {
    ctx->user_email.len = 0;
    ctx->user_apikey.len = 0;
    ctx->zone_name.len = 0;
    ctx->zone_id.len = 0;
    ctx->vars = NULL;
}

static void
cfddns_update_record(
        const string *user_email,
        const string *user_apikey,
        const string *zone_id,
        const string *record_id,
        const string *type,
        const string *name,
        const string *content
) {
    CURL *req = curl_easy_init();

    char url[strlenof(BASEURL) + 1 + zone_id->len + strlenof("/dns_records/") + record_id->len + 1];
    {
        size_t len = 0;
        memcpy(url + len, BASEURL, strlenof(BASEURL));
        len += strlenof(BASEURL);
        url[len] = '/';
        len += 1;
        memcpy(url + len, zone_id->data, zone_id->len);
        len += zone_id->len;
        memcpy(url + len, "/dns_records/", strlenof("/dns_records/"));
        len += strlenof("/dns_records/");
        memcpy(url + len, record_id->data, record_id->len);
        len += record_id->len;
        url[len] = '\0';
    }


    make_header(email_header, "X-Auth-Email: ", user_email->data, user_email->len);
    make_header(api_key_header, "X-Auth-Key: ", user_apikey->data, user_apikey->len);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, email_header);
    headers = curl_slist_append(headers, api_key_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    string json;

    string result;

    curl_easy_setopt(req, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(req, CURLOPT_URL, url);
    curl_easy_setopt(req, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_update_record_callback);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(req, CURLOPT_POSTFIELDS, json_struct); /* data goes here */
    curl_easy_perform(req);
    curl_slist_free_all(headers);
    curl_easy_cleanup(req);
}

static int cfddns_main(FILE *fin, FILE *fout, FILE *ferr) {
    context ctx;
    context_init(&ctx);
    for (char line[PIPE_BUF]; fgets(line, PIPE_BUF, fin); fputc('\n', fout)) {
        char *s, *e = line;
        fputss(e, s = pass_space(e), fout);
        if (s == (e = pass_value(s))) continue;
        switch (e[-1]) {
            case '?': {
                varible *var = malloc(sizeof(varible));
                if (!var) {
                    fputss(s, e, fout);
                    fputs(e, fout);
                    fputs(" #need_memory", ferr);
                    break;
                }
                // var_key
                string_copy(&var->key, s, e - 1);
                fputstr(&var->key, fout);
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
                {
                    char _e = *e;
                    *e = '\0';
                    if (memcmp(s, "ipv4", 4)) {
                        cfddns_urlget_ipv4(s, &var->value);
                    } else {
                        cfddns_urlget_string(s, &var->value);
                    }
                    *e = _e;
                }
                // value_old => var_changed
                fputss(e, s = pass_space(e), fout);
                e = pass_value(s);
                var->changed = !string_compare(&var->value, s, e);
                // var_value
                fputstr(&var->value, fout);
                // tails
                fputs(e, fout);
                // vars
                var->prev = ctx.vars;
                ctx.vars = var;
                break;
            }
            case ':': {
                // user_email
                string_copy(&ctx.user_email, s, e - 1);
                fputstr(&ctx.user_email, fout);
                fputc(':', fout);
                // user_apikey
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    ctx.user_email.len = 0;
                    ctx.user_apikey.len = 0;
                    fputs(e, fout);
                    fputs(" #need_apikey", ferr);
                    break;
                }
                string_copy(&ctx.user_apikey, s, e);
                fputstr(&ctx.user_apikey, fout);
                fputs(e, fout);
                break;
            }
            case '\\': {
                // zone_name
                string_copy(&ctx.zone_name, s, e - 1);
                fputstr(&ctx.zone_name, fout);
                fputc('\\', fout);
                // zone_id
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    cfddns_get_zone_id(&ctx);
                } else {
                    string_copy(&ctx.zone_id, s, e);
                }
                fputstr(&ctx.zone_id, fout);
                // tails
                fputs(e, fout);
                break;
            }
            default: {
                // var_key
                varible *var = ctx.vars;
                {
                    string var_key;
                    string_copy(&var_key, s, e);
                    fputstr(&var_key, fout);
                    while (var && !string_equals(&var_key, &var->key)) {
                        var = var->prev;
                    }
                }
                if (!var) {
                    fputs(e, fout);
                    fputs(" #var_undefined", ferr);
                    break;
                }
                // record_type
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    fputs(e, fout);
                    fputs(" #need_type", ferr);
                    break;
                }
                string record_type;
                string_copy(&record_type, s, e);
                // record_name
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    fputs(e, fout);
                    fputs(" #need_name", ferr);
                    break;
                }
                string record_name;
                string_copy(&record_name, s, e);
                // record_id
                string record_id;
                bool force_update, may_expired;
                fputss(e, s = pass_space(e), fout);
                if (s == (e = pass_value(s))) {
                    force_update = true;
                    may_expired = false;
                    cfddns_get_record_id(&record_id);
                } else if (e[-1] == '!') {
                    force_update = true;
                    may_expired = true;
                    string_copy(&record_id, s, e - 1);
                } else {
                    force_update = false;
                    may_expired = true;
                    string_copy(&record_id, s, e);
                }
                // do update
                bool success;
                if (!force_update && !var->changed) {
                    success = true;
                } else if (cfddns_update_record()) {
                    success = true;
                } else if (may_expired) {
                    cfddns_get_record_id(&record_id);
                    success = cfddns_update_record();
                } else {
                    success = false;
                }
                fputstr(&record_id, fout);
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
    for (varible *var = ctx.vars; var; var = var->prev) {
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
