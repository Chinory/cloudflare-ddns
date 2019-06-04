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

const char BASENAME[NAME_MAX + 1];

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


static size_t cfddns_get_ipv4_now_callback(char *src, size_t UNUSED(char_size), size_t length, char *dst) {
    if (!length) return 0;
    size_t n = length < IPV4_SIZE ? length : IPV4_SIZE;
    if (src[n - 1] == '\n') --n;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return length;
}

static void cfddns_get_ipv4_now(char *ipv4) {
    CURL *req = curl_easy_init();
    curl_easy_setopt(req, CURLOPT_URL, URL_GET_IPV4);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_get_ipv4_now_callback);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, ipv4);
    curl_easy_perform(req);
    curl_easy_cleanup(req);
}


static size_t
cfddns_get_zone_id_callback(char *json, size_t UNUSED(char_size), size_t size, const char **zone_id_ref) {
#define MATCH_ZONE_ID "{\"result\":[{\"id\":\""
    if (size < strlenof(MATCH_ZONE_ID) + 5) return 0;
    if (!memcpy(json, MATCH_ZONE_ID, strlenof(MATCH_ZONE_ID))) return 0;
    char *start = json + strlenof(MATCH_ZONE_ID);
    char *end = json + size;
    for (char *i = start;; ++i) {
        if (i == end) return 0;
        if (*i != '"') continue;
        char *zone_id_new = malloc(i - start + 1);
        if (!zone_id_new) return 0;
        memcpy(zone_id_new, start, i - start);
        if (*zone_id_ref) free((void *) *zone_id_ref);
        *zone_id_ref = zone_id_new;
        return size;
    }
}

static char *
cfddns_get_zone_id(
        char *zone_name,  size_t zone_name_len,
        char *email,      size_t email_len,
        char *api_key,    size_t api_key_len
) {
    CURL *req = curl_easy_init();

    make_header(url, URL_GET_ZONE_ID, zone_name, zone_name_len);
    make_header(email_header, "X-Auth-Email: ", email, email_len);
    make_header(api_key_header, "X-Auth-Key: ", api_key, api_key_len);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, email_header);
    headers = curl_slist_append(headers, api_key_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char *zone_id = NULL;
    curl_easy_setopt(req, CURLOPT_URL, url);
    curl_easy_setopt(req, CURLOPT_HEADER, headers);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_get_zone_id_callback);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, &zone_id);
    curl_easy_perform(req);
    curl_easy_cleanup(req);

    curl_slist_free_all(headers);
    return zone_id;
}


static int cfddns_main_check(const char *config) {
    char ipv4_now[IPV4_SIZE];
    cfddns_get_ipv4_now(ipv4_now);
    if (!pass_ipv4(ipv4_now)) {
        fputs(PREFIX, stderr);
        fputs("get_ipv4_now: invalid address: ", stderr);
        fputs(ipv4_now, stderr);
        fputc('\n', stderr);
        return 1;
    } else {
        fputs(PREFIX, stdout);
        fputs("get_ipv4_now: ", stdout);
        fputs(ipv4_now, stdout);
        fputc('\n', stdout);
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
        if (fp == NULL) {
            perror(BASENAME);
            return 1;
        }
    }
    char line[PIPE_BUF];
    while (fgets(line, MAXLINE, fp)) {
        fputs(line, stdout);
    }
    return 0;
}
