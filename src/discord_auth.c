#include "discord_auth.h"
#include "tls.h"
#include "config.h"
#include "notify.h"
#include "jsonlite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define DISCORD_HOST "discord.com"

static int discord_https_post(const char* path, const char* json_body, char* out_resp, size_t out_resp_sz) {
    if (!path || !json_body || !out_resp || out_resp_sz == 0) return -1;
    out_resp[0] = '\0';

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(DISCORD_HOST, "443", &hints, &res) != 0 || !res) {
        printf("[Discord-Auth] Failed to resolve %s\n", DISCORD_HOST);
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 8;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        printf("[Discord-Auth] Failed to connect to %s:443\n", DISCORD_HOST);
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    tls_ctx_t* tls = tls_start(fd, DISCORD_HOST);
    if (!tls) {
        printf("[Discord-Auth] TLS handshake failed for %s\n", DISCORD_HOST);
        close(fd);
        return -1;
    }

    char req_head[1024];
    size_t body_len = strlen(json_body);
    int head_len = snprintf(req_head, sizeof(req_head),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: Mozilla/5.0 (PlayStation 5; PS5) AppleWebKit/605.1.15 (KHTML, like Gecko)\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        path, DISCORD_HOST, body_len
    );

    tls_write(tls, req_head, (size_t)head_len);
    tls_write(tls, json_body, body_len);

    char raw_resp[16384];
    size_t total_read = 0;
    while (total_read < sizeof(raw_resp) - 1) {
        int r = tls_read(tls, raw_resp + total_read, sizeof(raw_resp) - 1 - total_read);
        if (r <= 0) break;
        total_read += (size_t)r;
    }
    raw_resp[total_read] = '\0';

    tls_free(tls);
    close(fd);

    if (total_read == 0) {
        return -1;
    }

    // Extract body after \r\n\r\n
    const char* body_start = strstr(raw_resp, "\r\n\r\n");
    if (!body_start) {
        body_start = raw_resp;
    } else {
        body_start += 4;
    }

    snprintf(out_resp, out_resp_sz, "%s", body_start);

    // Check if token is present in the response
    const char* tok_pattern = "\"token\":";
    const char* tok_ptr = strstr(body_start, tok_pattern);
    if (tok_ptr) {
        tok_ptr += strlen(tok_pattern);
        while (*tok_ptr == ' ' || *tok_ptr == '\"') tok_ptr++;
        char extracted_token[256] = "";
        size_t ti = 0;
        while (*tok_ptr && *tok_ptr != '\"' && *tok_ptr != '\r' && *tok_ptr != '\n' && *tok_ptr != '}' && ti < sizeof(extracted_token) - 1) {
            extracted_token[ti++] = *tok_ptr++;
        }
        extracted_token[ti] = '\0';

        if (extracted_token[0]) {
            snprintf(g_config.token, sizeof(g_config.token), "%s", extracted_token);
            config_save(&g_config);
            g_config.needs_update = true;
            printf("[Discord-Auth] Captured and saved Discord token from authentication response!\n");
            notify_custom("Discord Login", "Successfully authenticated!", NULL);
        }
    }

    return 0;
}

int discord_auth_login(const char* login, const char* password, char* out_resp, size_t out_resp_sz) {
    if (!login || !password) return -1;
    char json_body[1024];
    snprintf(json_body, sizeof(json_body),
        "{\"login\":\"%s\",\"password\":\"%s\",\"undelete\":false}",
        login, password
    );
    return discord_https_post("/api/v9/auth/login", json_body, out_resp, out_resp_sz);
}

int discord_auth_mfa(const char* ticket, const char* code, char* out_resp, size_t out_resp_sz) {
    if (!ticket || !code) return -1;
    char json_body[512];
    snprintf(json_body, sizeof(json_body),
        "{\"ticket\":\"%s\",\"code\":\"%s\"}",
        ticket, code
    );
    return discord_https_post("/api/v9/auth/mfa/totp", json_body, out_resp, out_resp_sz);
}
