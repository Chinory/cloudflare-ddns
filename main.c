#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <stdlib.h>

#define UNUSED __attribute__((unused))

#define strlenof(STR) (sizeof(STR) - 1)
#define make_header(Name, PREFIX, value, length) \
char Name[length + sizeof(PREFIX)]; \
memcpy(Name, PREFIX, sizeof(PREFIX) - 1); \
memcpy(Name + sizeof(PREFIX) - 1, value, length); \
Name[sizeof(PREFIX) + length - 1] = '\0'

char BASENAME[NAME_MAX + 1];


#define MAXSTR 253
typedef struct string {
    unsigned char len;
    char data[MAXSTR + 1];
} string;

typedef struct padstring {
    unsigned char len;
    unsigned char pad;
    char data[MAXSTR + 1];
} padstring;


static bool string_compare(const string *x, const string *y) {
    return x->len == y->len && memcmp(x->data, y->data, x->len);
}

static bool string_compare_to(const string *str, const char *start, const char *end) {
    return str->len == end - start && memcmp(str->data, start, str->len);
}

static void string_copy(string *dst, const string *src) {
    memcpy(dst, src, src->len + 1);
}

static void string_copy_from(string *dst, const char *start, const char *end) {
    memcpy(dst->data, start, dst->len = end - start);
}

static char *string_cstr(string *str) {
    str->data[str->len] = '\0';
    return str->data;
}

static void string_clear(string *str) {
    str->len = 0;
}




#define IPV4_SIZE 16
#define URL_GET_IPV4 "ipv4.icanhazip.com"
#define URL_ZONE "https://api.cloudflare.com/client/v4/zones"
#define URL_GET_ZONE_ID "https://api.cloudflare.com/client/v4/zones?name="

static const char *ipv4_next_part(const char *c) {
    if (c[0] < '0' || c[0] > '9') return 0;
    if (c[1] == '.') return c + 2;
    if (c[1] < '0' || c[1] > '9') return 0;
    if (c[2] == '.') return c + 3;
    if (c[2] < '0' || c[2] > '9') return 0;
    if (c[3] != '.') return 0;
    if ((((unsigned) (c[0] - '0') * 10) +
         (unsigned) (c[1] - '0')) * 10 +
        (unsigned) (c[2] - '0') < 256)
        return c + 3;
    return 0;
}

static const char *ipv4_last_part(const char *c) {
    if (c[0] < '0' || c[0] > '9') return 0;
    if (c[1] < '0' || c[1] > '9') return c + 1;
    if (c[2] < '0' || c[2] > '9') return c + 2;
    if ((((unsigned) (c[0] - '0') * 10) +
         (unsigned) (c[1] - '0')) * 10 +
        (unsigned) (c[2] - '0') < 256)
        return c + 3;
    return 0;
}

static const char *pass_ipv4(const char *c) {
    if (!(c = ipv4_next_part(c))) return 0;
    if (!(c = ipv4_next_part(c))) return 0;
    if (!(c = ipv4_next_part(c))) return 0;
    return ipv4_last_part(c);
}

static size_t cfddns_curl_get_callback(const char *src, const size_t UNUSED(char_size), const size_t len, string *dst) {
    if (dst->len < MAXSTR) {
        const char *end = src + len;
        for (const char *s = src; s < end; ++s) {
            if (*s == '\0' || *s == '\n' || *s == ' ') continue;
            const char *e = s + 1;
            while (e < end && !(*e == '\0' || *e == '\n' || *e == ' ')) ++e;
            size_t n = e - s;
            if (n > MAXSTR - dst->len)
                n = MAXSTR - dst->len;
            memcpy(dst->data + dst->len, s, n);
            dst->len += n;
            break;
        }
    }
    return len;
}


static void cfddns_curl_get(const char *url, string *str) {
    CURL *req = curl_easy_init();
    curl_easy_setopt(req, CURLOPT_URL, url);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_curl_get_callback);
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



static void cfddns_get_zone_id(const string *email, const string *apikey, const string *zone_name, string *zone_id) {
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

static char *next_end(char *s) {
    for (++s; *s != '\0' && *s != '\t' && *s != ' ' && *s != '#'; ++s);
    return s;
}


static char *cfddns_find_start(char *i) {
    while (is_space(*i)) ++i;
    return i;
}

static char *cfddns_find_end(char *i) {
    while (*i && !is_space(*i) && !is_comment(*i)) ++i;
    return i;
}

typedef struct varible {
    struct varible *prev;
    struct string key;
    struct string value;
    bool changed;
} varible;



static int cfddns_main(FILE *fin, FILE *fout, FILE *ferr) {
    padstring str[5];
    char line[sizeof(str)];


    string user_email = {0};
    string user_apikey = {0};
    string zone_name = {0};
    string zone_id = {0};
    varible *vars = NULL;
    while (fgets(line, sizeof(line), fin)) {

        
    }
    for (varible *var = vars; var; var = var->prev) free(var);
    return 0;
}

int main(int argc, char *argv[]) {
    for (char *i = argv[1], *s = i;;) {
        if (*i) {
            if (*i != '/') ++i; else s = ++i;
        } else {
            memcpy((void*)BASENAME, s, i - s + 1);
            break;
        }
    }
    string ipv4;
    ipv4.len = 0;
    cfddns_curl_get(URL_GET_IPV4, &ipv4);
    fwrite(ipv4.data, 1, ipv4.len, stdout);

    return 0;
    FILE *fp;
    if (argc < 2) {
        fp = stdin;
    } else {
        fp = fopen(argv[1], "r");
        if (fp == NULL) {
            perror(BASENAME);
            return 1;
        }
    }
    return cfddns_main(fp, stdout);
}
