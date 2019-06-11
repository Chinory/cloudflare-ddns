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

#define STRING_SIZE 255

typedef struct string {
    unsigned char len;
    char data[STRING_SIZE];
} string;

static bool stringcmp(const string *a, const string *b) {
    return a->len != b->len ? false : memcmp(a->data, b->data, a->len);
}

static void stringcpy(string *dst, const string *src) {
    memcpy(dst->data, src->data, dst->len = src->len);
}

static void string_write(string *str, const char *s, uint8_t len) {
    memcpy(str->data, s, str->len = len);
}

typedef struct kvnode {
    struct kvnode *prev;
    struct string key;
    struct string value;
} kvnode;


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
    if (dst->len < STRING_SIZE) {
        const char *end = src + len;
        for (const char *s = src; s < end; ++s) {
            if (*s == '\0' || *s == '\n' || *s == ' ') continue;
            const char *e = s + 1;
            while (e < end && !(*e == '\0' || *e == '\n' || *e == ' ')) ++e;
            size_t n = e - s;
            if (n > STRING_SIZE - dst->len)
                n = STRING_SIZE - dst->len;
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
        if (i - start > STRING_SIZE) return 0;
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

#define cfddns_main_find_end() for (e = s + 1; *e != ' ' && *e != '\t' && *e != '#' && *e != '\0'; ++e)
static int cfddns_main(FILE *fin, FILE *fout, FILE *ferr) {
    char line[PIPE_BUF];
    string user_email = {0}, user_apikey = {0}, zone_name = {0}, zone_id = {0};
    kvnode *vars = NULL;
    while (fgets(line, PIPE_BUF, fin)) {
        char *s = line, *e;
        while (*s == ' ' || *s == '\t') ++s; 
        if (*s == '#') for (++s; *s; ++s);
        if (*s == '\0') { 
            *s = '\n';
            fwrite(line, 1, s - line + 1, fout);
            continue;
        }
        cfddns_main_find_end();
        // got first token
        if (e[-1] == '?') { // var
            // var_key
            string var_key;
            string_write(&var_key, s, e - s);
            // var_url -> var_value_now
            for (s = e + 1; *s == ' ' || *s == '\t'; ++s);
            if (*s == '#' || *s == '\0') {
                fputs(" # need url", ferr);
                continue;
            }
            e[-1] = '\0';
            string var_value_now = {0};
            cfddns_curl_get(s, &var_value_now);
            // var_value
            for (s = e + 1; *s == ' ' || *s == '\t'; ++s);
            if (*s != '#' && *s != '\0') {
                cfddns_main_find_end();
                if (e - s == var_value_now.len && memcmp(s, var_value_now.data, var_value_now.len)) {
                    continue;
                }
            }
            kvnode *var = malloc(sizeof(kvnode));
            stringcpy(&var->key, &var_key);
            stringcpy(&var->value, &var_value_now);
            var->prev = vars; vars = var;
        } else if (s[0] == '/') { // user
            // user_email
            string_write(&user_email, s + 1, e - (s + 1));
            // user_apikey
            user_apikey.len = 0;
            for (s = e + 1; *s == ' ' || *s == '\t'; ++s);
            if (*s == '#' || *s == '\0') {
                fputs(" # need apikey", ferr);
                continue;
            }
            for (e = s + 1; *e != ' ' && *e != '\t' && *e != '#' && *e != '\0'; ++e);
            string_write(&user_apikey, s, e - s);
        } else if (e[-1] == '/') { // zone
            // zone_name
            string_write(&zone_name, s, e - s - 1);
            // zone_id
            zone_id.len = 0;
            for (s = e + 1; *s == ' ' || *s == '\t'; ++s);
            if (*s == '#' || *s == '\0') {
                cfddns_get_zone_id(&user_email, &user_apikey, &zone_name, &zone_id);
            } else {
                for (e = s + 1; *e != ' ' && *e != '\t' && *e != '#' && *e != '\0'; ++e);
                string_write(&zone_id, s, e - s);
            }
        } else { // record
            
        }
    }
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
