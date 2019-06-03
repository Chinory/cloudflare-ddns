#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <stdlib.h>

#define UNUSED __attribute__((unused))
#define STRLEN(STR) (sizeof(STR) - 1)

#define CONFIG_ALLOC_STACK
//#define CONFIG_ALLOC_HEAP

#define IPV4_LEN 15
#define IPV4_SIZE 16

#define BASENAME "cfddns"
#define PREFIX BASENAME ": "
#define URL_GET_IPV4 "ipv4.icanhazip.com"
#define URL_GET_ZONE_ID "https://api.cloudflare.com/client/v4/zones?name="

static const char *
pass_uint8_dec(const char *c) {
    if (*c < '0' || *c > '9') return 0;
    for (unsigned n = *c++ - '0';; ++c) {
        if (*c < '0' || *c > '9') return c;
        if ((n = n * 10 + *c - '0') > 255) return 0;
    }
}

static bool
is_ipv4(const char *c) {
    return (c = pass_uint8_dec(c)) && *c == '.' &&
           (c = pass_uint8_dec(c + 1)) && *c == '.' &&
           (c = pass_uint8_dec(c + 1)) && *c == '.' &&
           (c = pass_uint8_dec(c + 1)) && *c == '\0';
}

static size_t
cfddns_get_ipv4_now_callback(char *src, size_t UNUSED(char_size), size_t length, char *dst) {
    if (!length) return 0;
    size_t n = length > IPV4_LEN ? IPV4_LEN : length;
    if (src[n - 1] == '\n') --n;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return length;
}

static void
cfddns_get_ipv4_now(char *ipv4) {
    CURL *req = curl_easy_init();
    curl_easy_setopt(req, CURLOPT_URL, URL_GET_IPV4);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_get_ipv4_now_callback);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, ipv4);
    curl_easy_perform(req);
    curl_easy_cleanup(req);
}

static size_t
cfddns_config_scan(const char **config) {
    const char *c = *config;
    for (;; *config = ++c) {
        switch (*c) {
            case '\0':
                return 0;
            case '\n':
            case ' ':
            case '#': {
                if (c != *config) {
                    return c - *config;
                }
            }
        }
    }
}


static bool
cfddns_config_next(const char **config) {
    const char *c = *config;
    for (;; *config = ++c) {
        switch (*c) {
            case '\0':
                return false;
            case '\n':
            case ' ':
                break;
            case '#': {
                for (++c; *c != '\n'; ++c) {
                    if (*c == '\0') {
                        *config = c;
                        return false;
                    }
                }
                break;
            }
            default:
                return true;
        }
    }
}


static size_t
cfddns_get_zone_id_callback(char *json, size_t UNUSED(char_size), size_t size, const char **zone_id_ref) {
#define MATCH_ZONE_ID "{\"result\":[{\"id\":\""
    if (size < STRLEN(MATCH_ZONE_ID) + 5) return 0;
    if (!memcmp(json, MATCH_ZONE_ID, STRLEN(MATCH_ZONE_ID))) return 0;
    char *start = json + STRLEN(MATCH_ZONE_ID);
    char *end = json + size;
    for (char *i = start;; ++i) {
        if (i == end) return 0;
        if (*i != '"') continue;
        char *zone_id_new = malloc(i - start + 1);
        if (!zone_id_new) return 0;
        memcpy(zone_id_new, start, i - start);
        if (*zone_id_ref) free(*zone_id_ref);
        *zone_id_ref = zone_id_new;
        return size;
    }
}

static char *
cfddns_get_zone_id(
        char *zone_name,
        char *email,
        char *api_key,
        size_t zone_name_len,
        size_t email_len,
        size_t api_key_len
) {
    CURL *req = curl_easy_init();
    if (!req) return NULL;

    const size_t url_size = STRLEN(URL_GET_ZONE_ID) + zone_name_len + 1;
    char url[url_size];
    memcpy(url, URL_GET_ZONE_ID, STRLEN(URL_GET_ZONE_ID));
    memcpy(url + STRLEN(URL_GET_ZONE_ID), zone_name, zone_name_len);
    url[url_size] = '\0';

    const size_t header_email_size = STRLEN("X-Auth-Email: ") + email_len + 1;
    char header_email[header_email_size];
    memcpy(header_email, "X-Auth-Email: ", STRLEN("X-Auth-Email: "));
    memcpy(header_email + STRLEN("X-Auth-Email: "), email, email_len);
    header_email[header_email_size] = '\0';

    const size_t header_api_key_size = STRLEN("X-Auth-Key: ") + email_len + 1;
    char header_api_key[header_api_key_size];
    memcpy(header_api_key, "X-Auth-Key: ", STRLEN("X-Auth-Key: "));
    memcpy(header_api_key + STRLEN("X-Auth-Key: "), api_key, api_key_len);
    header_api_key[header_api_key_size] = '\0';

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, header_email);
    headers = curl_slist_append(headers, header_api_key);
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
    char ipv4_now[IPV4_LEN + 1];
    cfddns_get_ipv4_now(ipv4_now);
    if (!is_ipv4(ipv4_now)) {
        fputs(PREFIX "get_ipv4_now: invalid address: ", stderr);
        fputs(ipv4_now, stderr);
        fputc('\n', stderr);
        return 1;
    } else {
        fputs(PREFIX "get_ipv4_now: ", stdout);
        fputs(ipv4_now, stdout);
        fputc('\n', stdout);
    }
    while (cfddns_config_next(&config)) {
        // email
        size_t email_len = cfddns_config_scan(&config);
        if (!email_len) continue;
        const char *email = config;
        config += email_len;
        // email: log
        fputs(PREFIX "email: ", stdout);
        fwrite(email, 1, email_len, stdout);
        fputc('\n', stdout);

        // api_key
        size_t api_key_len = cfddns_config_scan(&config);
        if (!api_key_len) continue;
        const char *api_key = config;
        config += api_key_len;
        // api_key: log
        fputs(PREFIX "api_key: ", stdout);
        fwrite(api_key, 1, api_key_len, stdout);
        fputc('\n', stdout);

        // zone_name
        size_t zone_name_len = cfddns_config_scan(&config);
        if (!zone_name_len) continue;
        const char *zone_name = config;
        config += zone_name_len;
        // zone_name: log
        fputs(PREFIX "zone_name: ", stdout);
        fwrite(zone_name, 1, zone_name_len, stdout);
        fputc('\n', stdout);

        // domain
        size_t domain_len = cfddns_config_scan(&config);
        if (!domain_len) continue;
        const char *domain = config;
        config += domain_len;
        // domain: log
        fputs(PREFIX "domain: ", stdout);
        fwrite(domain, 1, domain_len, stdout);
        fputc('\n', stdout);

        // ipv4: optional
        size_t ipv4_len = cfddns_config_scan(&config);
        char ipv4[IPV4_LEN];
        if (ipv4_len > IPV4_LEN) ipv4_len = IPV4_LEN;
        memcpy(ipv4, config, ipv4_len);
        ipv4[ipv4_len] = '\0';
        config += ipv4_len;
        // ipv4: log
        fputs(PREFIX "ipv4: ", stdout);
        fwrite(ipv4, 1, ipv4_len, stdout);
        char e = ipv4[ipv4_len];
        ipv4[ipv4_len] = '\0';
        if (!is_ipv4(ipv4)) {
            fputs(" not invalid", stdout);
        }
        ipv4[ipv4_len] = e;

        fputc('\n', stdout);

        // zone_id: optional
        size_t zone_id_len = cfddns_config_scan(&config);
        const char *zone_id = zone_id_len ? config : "";
        config += zone_id_len;
        // zone_id: log
        fputs(PREFIX "zone_id: ", stdout);
        fwrite(zone_id, 1, zone_id_len, stdout);
        fputc('\n', stdout);

        // record_id: optional
        size_t record_id_len = cfddns_config_scan(&config);
        const char *record_id = record_id_len ? config : "";
        config += record_id_len;
        // record_id: log
        fputs(PREFIX "record_id: ", stdout);
        fwrite(record_id, 1, record_id_len, stdout);
        fputc('\n', stdout);
    }
    return 0;
}

static int cfddns_main_config(char *config_path) {
    // open file
    FILE *fp = fopen(config_path, "r");
    if (fp == NULL) {
        perror(PREFIX "failed to open config file:");
        return 1;
    }

    // get file size
    fseek(fp, 0, SEEK_END);
    long config_size = ftell(fp);
    if (config_size == -1L) {
        perror(PREFIX "failed to determine size of config file:");
        fclose(fp);
        return 1;
    }
    fseek(fp, 0, SEEK_SET);

#ifdef CONFIG_ALLOC_STACK
    char config_data[config_size + 1];
#endif
#ifdef CONFIG_ALLOC_HEAP
    char *config_data = malloc(c);
    if (!config_data) {
        perror(PREFIX "failed alloc memory for config:");
        fclose(fp);
        return 1;
    }
#endif

    // read file
    config_size = fread(config_data, 1, config_size, fp);
    config_data[config_size] = '\0';

    // close file
    fclose(fp);

    // next
    curl_global_init(CURL_GLOBAL_ALL);
    int status = cfddns_main_check(config_data);
    curl_global_cleanup();

#ifdef CONFIG_ALLOC_HEAP
    free(config_data);
#endif

    return status;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fputs(BASENAME " [config_file]", stderr);
        return 1;
    }
    return cfddns_main_config(argv[1]);
}
