#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <stdlib.h>
#define IPV4_LEN 15
#define IPV4_SIZE 16

#define BASENAME "cfddns"
#define PREFIX BASENAME ": "
#define URL_GET_IPV4 "ipv4.icanhazip.com"

static const char* pass_uint8_dec(const char *c) {
    if (*c < '0' || *c > '9') return 0;
    for (unsigned n = *c++ - '0'; ; ++c) {
        if (*c < '0' || *c > '9') return c;
        if ((n = n * 10 + *c - '0') > 255) return 0;
    }
}

static bool is_ipv4(const char *c)
{
    return (c = pass_uint8_dec(c)) && *c == '.'
        && (c = pass_uint8_dec(c + 1)) && *c == '.'
        && (c = pass_uint8_dec(c + 1)) && *c == '.'
        && (c = pass_uint8_dec(c + 1)) && *c == '\0';
}


/**
 * replace meaningless chars to '\0' and count real lines
 * @param c string to modify and count lines
 * @return number of real lines
 */
static size_t cfddns_config_filter(char *c)
{
    size_t lines = 0;
    char quote = '\0';
    for (; ; ++c) {
        if (*c == '\0') return lines;
        if (*c == ' ') {
            do *c++ = '\0'; while (*c == ' ');
            if (*c == '\0') return lines;
        }
        if (*c == '\n') {
            *c = '\0'; quote = '\0';
            continue;
        }
        if (*c == '#') {
            do { *c++ = '\0';
                if (*c == '\0') return lines;
            } while (*c != '\n');
            *c = '\0';
            continue;
        }
        ++lines;
        if (*c == '"' || *c == '\'') {
            quote = *c;
        }
        for (++c; ; ++c) {
            if (*c == '\0') {
                for (char *e = c - 1; *e == ' '; *e-- = '\0');
                return lines;
            }
            if (*c == '\n') {
                *c = '\0'; quote = '\0';
                for (char *e = c - 1; *e == ' '; *e-- = '\0');
                break;
            }
            if (quote) {
                if (*c == quote && c[-1] != '\\') quote = '\0';
                continue;
            }
            if (*c == '"' || *c == '\'') {
                quote = *c;
                continue;
            }
            if (*c == '#') {
                for (char *e = c - 1; *e == ' '; *e-- = '\0');
                do { *c++ = '\0';
                    if (*c == '\0') return lines;
                } while (*c != '\n');
                *c = '\0';
                break;
            }
        }
    }
}

static size_t cfddns_config_scan(const char **config)
{
    const char *c = *config;
    for (; ; *config = ++c) switch (*c) {
        case '\0': return 0;
        case '\n':
        case ' ':
        case '#': if (c != *config)
            return c - *config;
    }
}



static bool cfddns_config_next(const char **config)
{
    const char *c = *config;
    for (; ; *config = ++c) switch (*c) {
        case '\0': return false;
        case '\n':
        case ' ': break;
        case '#': {
            for (++c; *c != '\n'; ++c) {
                if (*c == '\0') {
                    *config = c;
                    return false;
                }
            }
            break;
        }
        default: return true;
    }
    
}

static size_t cfddns_write_zone_id( char *json, size_t char_size, size_t nmemb, const char **zone_id )
{
#define ZONE_ID_HEAD "{\"result\":[{\"id\":\""
    if (nmemb > sizeof(ZONE_ID_HEAD)) {
        if (memcmp(json, ZONE_ID_HEAD, sizeof(ZONE_ID_HEAD))) {
            char *start = json + sizeof(ZONE_ID_HEAD);
            for (char *i = start; *i; ++i) {
                if (*i == '"') {
                    char *zone_id_str = malloc(i - start + 1);
                    if (zone_id_str) {
                        memcpy(zone_id_str, start, i - start);
                        *zone_id = zone_id_str;
                        return nmemb;
                    }
                }
            }
        }
    }
    return 0;
}

static char* cfddns_get_zone_id (char *zone_name, char* email, char* api_key, size_t zone_name_len, size_t email_len, size_t api_key_len)
{
    CURL *req = curl_easy_init();
    if (!req) return NULL;

#define URL_GET_ZONE_ID "https://api.cloudflare.com/client/v4/zones?name="
    const size_t url_size = sizeof(URL_GET_ZONE_ID) + zone_name_len + 1;
    char url[url_size];
    memcpy(url, URL_GET_ZONE_ID, sizeof(URL_GET_ZONE_ID));
    memcpy(url + sizeof(URL_GET_ZONE_ID), zone_name, zone_name_len);
    url[url_size] = '\0';

#define HEADER_EMAIL "X-Auth-Email: "
    const size_t header_email_size = sizeof(HEADER_EMAIL) + email_len + 1;
    char header_email[header_email_size];
    memcpy(header_email, HEADER_EMAIL, sizeof(HEADER_EMAIL));
    memcpy(header_email + sizeof(HEADER_EMAIL), email, email_len);
    header_email[header_email_size] = '\0';

#define HEADER_API_KEY "X-Auth-Key: "
    const size_t header_api_key_size = sizeof(HEADER_API_KEY) + email_len + 1;
    char header_api_key[header_api_key_size];
    memcpy(header_api_key, HEADER_API_KEY, sizeof(HEADER_API_KEY));
    memcpy(header_api_key + sizeof(HEADER_API_KEY), api_key, api_key_len);
    header_api_key[header_api_key_size] = '\0';

#define HEADER_CONTENT_TYPE "Content-Type: application/json"
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, header_email);
    headers = curl_slist_append(headers, header_api_key);
    headers = curl_slist_append(headers, HEADER_CONTENT_TYPE);

    char *zone_id = NULL;
    curl_easy_setopt(req, CURLOPT_URL, url);
    curl_easy_setopt(req, CURLOPT_HEADER, headers);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_write_zone_id);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, &zone_id);
    curl_easy_perform(req);
    curl_easy_cleanup(req);

    curl_slist_free_all(headers);
    return zone_id;
}

static size_t cfddns_write_ipv4( char *ptr, size_t size, size_t nmemb, char *ipv4 )
{
    size_t len = nmemb < IPV4_SIZE ? nmemb : IPV4_SIZE;
    if (len && ptr[len - 1] == '\n') --len;
    memcpy(ipv4, ptr, len);
    ipv4[len] = '\0';
    return nmemb;
}

static void cfddns_update (char *domain, char *ipv4, char *zoneid, char *recordid)
{
    CURL *req = curl_easy_init();
    curl_easy_setopt(req, CURLOPT_URL, "https://api.cloudflare.com/client/v4/zones?name=$zone_name");
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_write_ipv4);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, ipv4);
    curl_easy_perform(req);
    curl_easy_cleanup(req);
}

static void cfddns_get_ipv4_now(char *ipv4)
{
    CURL *req = curl_easy_init();
    curl_easy_setopt(req, CURLOPT_URL, URL_GET_IPV4);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, cfddns_write_ipv4);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, ipv4);
    curl_easy_perform(req);
    curl_easy_cleanup(req);
}

static int cfddns_check ( const char **config )
{
    char ipv4_now[IPV4_SIZE];
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
    while (cfddns_config_next(config)) {
        // email
        size_t email_len = cfddns_config_scan(config);
        if (!email_len) continue;
        const char *email = *config;
        *config += email_len;
        // email: log
        fputs(PREFIX "email: ", stdout);
        fwrite(email, 1, email_len, stdout);
        fputc('\n', stdout);

        // api_key
        size_t api_key_len = cfddns_config_scan(config);
        if (!api_key_len) continue;
        const char *api_key = *config;
        *config += api_key_len;
        // api_key: log
        fputs(PREFIX "api_key: ", stdout);
        fwrite(api_key, 1, api_key_len, stdout);
        fputc('\n', stdout);

        // zone_name
        size_t zone_name_len = cfddns_config_scan(config);
        if (!zone_name_len) continue;
        const char *zone_name = *config;
        *config += zone_name_len;
        // zone_name: log
        fputs(PREFIX "zone_name: ", stdout);
        fwrite(zone_name, 1, zone_name_len, stdout);
        fputc('\n', stdout);

        // domain
        size_t domain_len = cfddns_config_scan(config);
        if (!domain_len) continue;
        const char *domain = *config;
        *config += domain_len;
        // domain: log
        fputs(PREFIX "domain: ", stdout);
        fwrite(domain, 1, domain_len, stdout);
        fputc('\n', stdout);

        // ipv4: optional
        size_t ipv4_len = cfddns_config_scan(config);
        char ipv4[IPV4_LEN];
        if (ipv4_len > IPV4_LEN) ipv4_len = IPV4_LEN;
        memcpy(ipv4, *config, ipv4_len);
        ipv4[ipv4_len] = '\0';
        *config += ipv4_len;
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
        size_t zone_id_len = cfddns_config_scan(config);
        const char *zone_id = zone_id_len ? *config : "";
        *config += zone_id_len;
        // zone_id: log
        fputs(PREFIX "zone_id: ", stdout);
        fwrite(zone_id, 1, zone_id_len, stdout);
        fputc('\n', stdout);

        // record_id: optional
        size_t record_id_len = cfddns_config_scan(config);
        const char *record_id = record_id_len ? *config : "";
        *config += record_id_len;
        // record_id: log
        fputs(PREFIX "record_id: ", stdout);
        fwrite(record_id, 1, record_id_len, stdout);
        fputc('\n', stdout);
    }
    return 0;
}

int main (int argc, char *argv[])
{
    if (argc < 2) {
        fputs(BASENAME " [config_file]", stderr);
        return 1;
    }
    // open config file
    FILE *cfg_fp = fopen(argv[1], "rw");
    if (cfg_fp == NULL) {
        perror(PREFIX "failed to open config file:");
        return 1;
    }
    // determine size of config file
    fseek(cfg_fp, 0, SEEK_END);
    long cfg_size = ftell(cfg_fp);
    if (cfg_size == -1L) {
        perror(PREFIX "failed to determine size of config file:");
        fclose(cfg_fp);
        return 1;
    }
    fseek(cfg_fp, 0, SEEK_SET);
    // read config file
    char cfg_data[cfg_size + 1];
    cfg_size = fread(cfg_data, 1, cfg_size, cfg_fp);
    cfg_data[cfg_size] = '\0';
    // load curl
    curl_global_init(CURL_GLOBAL_ALL);
    char *config = cfg_data;
    int status = cfddns_check((const char **)&config);
    curl_global_cleanup();
    return status;
}

