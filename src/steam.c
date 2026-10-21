#include "steam.h"
#include "tmdb.h"
#include "tls.h"
#include "clock.h"
#include "log.h"
#include "updater_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define STEAM_HOST "store.steampowered.com"
#define STEAM_DEADLINE_S 8

static void url_encode(const char *src, char *dst, size_t dst_cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 4 < dst_cap; si++) {
        unsigned char c = (unsigned char)src[si];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = c;
        } else {
            dst[di++] = '%';
            dst[di++] = hex[c >> 4];
            dst[di++] = hex[c & 0x0F];
        }
    }
    dst[di] = '\0';
}

int steam_resolve_icon(const char *title_name, char *out_icon, size_t cap) {
    if (!title_name || !title_name[0] || !out_icon || cap < 64) return 0;
    out_icon[0] = '\0';

    char enc_term[256];
    url_encode(title_name, enc_term, sizeof(enc_term));

    char path[512];
    snprintf(path, sizeof(path), "/api/storesearch/?term=%s&l=english&cc=US", enc_term);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(443);
    int ok = 0;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(STEAM_HOST, "443", &hints, &res) == 0 && res) {
        struct sockaddr_in *sin = (struct sockaddr_in*)res->ai_addr;
        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        if (ip != 0 && (ip >> 24) != 127) {
            sa.sin_addr = sin->sin_addr;
            ok = 1;
        }
        freeaddrinfo(res);
    }

    if (!ok) {
        if (resolve_via_public_dns(STEAM_HOST, &sa.sin_addr) == 0) {
            ok = 1;
        }
    }

    if (!ok) {
        printf("[PS5-RPC] DNS resolution failed for %s\n", STEAM_HOST);
        return 0;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    struct timeval tv = { .tv_sec = STEAM_DEADLINE_S, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        close(fd);
        return 0;
    }

    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    tls_ctx_t *t = tls_start(fd, STEAM_HOST);
    if (!t) {
        close(fd);
        return 0;
    }

    char req[1024];
    int rl = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: Mozilla/5.0\r\n"
        "Connection: close\r\n\r\n",
        path, STEAM_HOST);

    if (rl <= 0 || tls_write(t, req, (size_t)rl) < 0) {
        tls_free(t);
        close(fd);
        return 0;
    }

    static char raw[32768];
    size_t bl = 0;
    int64_t dl = orbis_mono_s() + STEAM_DEADLINE_S + 5;
    for (;;) {
        char tmp[1024];
        int r = tls_read(t, tmp, sizeof(tmp) - 1);
        if (r < 0) break;
        if (r == 0) {
            if (orbis_mono_s() > dl) break;
            usleep(20000);
            continue;
        }
        if (bl + (size_t)r >= sizeof(raw) - 1) break;
        memcpy(raw + bl, tmp, (size_t)r);
        bl += (size_t)r;
        if (orbis_mono_s() > dl) break;
    }

    tls_free(t);
    close(fd);

    if (bl == 0) return 0;
    raw[bl] = '\0';

    int st = 0;
    size_t olen = 0;
    char *body = upd_parse_response(raw, bl, sizeof(raw), &st, &olen, NULL, 0);
    if (!body || st != 200) {
        if (body) free(body);
        return 0;
    }

    // Look for "id":<num> in response
    int found = 0;
    char *p = strstr(body, "\"id\":");
    if (p) {
        p += 5;
        while (*p == ' ') p++;
        char app_id[32] = "";
        size_t ai = 0;
        while (*p >= '0' && *p <= '9' && ai < sizeof(app_id) - 1) {
            app_id[ai++] = *p++;
        }
        app_id[ai] = '\0';
        if (ai > 0) {
            snprintf(out_icon, cap, "https://cdn.cloudflare.steamstatic.com/steam/apps/%s/library_600x900.jpg", app_id);
            found = 1;
        }
    }

    free(body);
    return found;
}
