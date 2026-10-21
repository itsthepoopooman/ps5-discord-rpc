#include "catbox.h"
#include "tmdb.h"
#include "tls.h"
#include "clock.h"
#include "log.h"
#include "updater_http.h"
#include "jsonlite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>

#define CATBOX_HOST "catbox.moe"
#define CATBOX_DEADLINE_S 30
#define CATBOX_MAX_CACHE 256
#define BOUNDARY "----WebKitFormBoundary7MA4YWxkTrZu0gW"

#define ICON_CACHE_PRIMARY   "/data/discord_rpc/icon_cache.json"
#define ICON_CACHE_FALLBACK  "/user/home/discord_rpc/icon_cache.json"

static struct {
    char title_id[32];
    char url[256];
} s_cache[CATBOX_MAX_CACHE];
static int s_cache_count = 0;
static int s_cache_loaded = 0;

static void mkdir_p(const char* dir) {
    char tmp[256];
    char* p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (len == 0) return;
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static void load_cache_file(void) {
    if (s_cache_loaded) return;
    s_cache_loaded = 1;

    const char *paths[] = { ICON_CACHE_PRIMARY, ICON_CACHE_FALLBACK, NULL };
    for (int p = 0; paths[p]; p++) {
        FILE *f = fopen(paths[p], "rb");
        if (!f) continue;

        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 262144) { fclose(f); continue; }

        char *buf = malloc((size_t)sz + 1);
        if (!buf) { fclose(f); continue; }
        fread(buf, 1, (size_t)sz, f);
        fclose(f);
        buf[sz] = '\0';

        jl_val_t *root = jl_parse(buf, (size_t)sz);
        free(buf);
        if (!root || root->type != JL_OBJECT) {
            if (root) jl_free(root);
            continue;
        }

        for (jl_val_t *pair = root->child; pair; pair = pair->next) {
            if (pair->str && pair->child && pair->child->type == JL_STRING && pair->child->str) {
                if (s_cache_count < CATBOX_MAX_CACHE) {
                    strncpy(s_cache[s_cache_count].title_id, pair->str, sizeof(s_cache[s_cache_count].title_id) - 1);
                    s_cache[s_cache_count].title_id[sizeof(s_cache[s_cache_count].title_id) - 1] = '\0';
                    strncpy(s_cache[s_cache_count].url, pair->child->str, sizeof(s_cache[s_cache_count].url) - 1);
                    s_cache[s_cache_count].url[sizeof(s_cache[s_cache_count].url) - 1] = '\0';
                    s_cache_count++;
                }
            }
        }
        jl_free(root);
        printf("[PS5-RPC] Loaded %d cached icon(s) from %s\n", s_cache_count, paths[p]);
        break;
    }
}

static void save_cache_file(void) {
    jl_val_t *root = jl_new_object();
    if (!root) return;

    for (int i = 0; i < s_cache_count; i++) {
        jl_obj_set(root, s_cache[i].title_id, jl_new_string(s_cache[i].url));
    }

    char *json = jl_stringify(root);
    jl_free(root);
    if (!json) return;

    const char *paths[] = { ICON_CACHE_PRIMARY, ICON_CACHE_FALLBACK, NULL };
    const char *dirs[]  = { "/data/discord_rpc", "/user/home/discord_rpc", NULL };
    for (int p = 0; paths[p]; p++) {
        if (dirs[p]) mkdir_p(dirs[p]);
        FILE *f = fopen(paths[p], "wb");
        if (f) {
            fwrite(json, 1, strlen(json), f);
            fclose(f);
            printf("[PS5-RPC] Saved icon cache to %s (%d entries)\n", paths[p], s_cache_count);
            break;
        }
    }
    free(json);
}

void catbox_cache_update(const char *title_id, const char *url) {
    if (!title_id || !title_id[0] || !url || !url[0]) return;
    load_cache_file();
    for (int i = 0; i < s_cache_count; i++) {
        if (strcmp(s_cache[i].title_id, title_id) == 0) {
            strncpy(s_cache[i].url, url, sizeof(s_cache[i].url) - 1);
            s_cache[i].url[sizeof(s_cache[i].url) - 1] = '\0';
            save_cache_file();
            return;
        }
    }
    if (s_cache_count < CATBOX_MAX_CACHE) {
        strncpy(s_cache[s_cache_count].title_id, title_id, sizeof(s_cache[s_cache_count].title_id) - 1);
        s_cache[s_cache_count].title_id[sizeof(s_cache[s_cache_count].title_id) - 1] = '\0';
        strncpy(s_cache[s_cache_count].url, url, sizeof(s_cache[s_cache_count].url) - 1);
        s_cache[s_cache_count].url[sizeof(s_cache[s_cache_count].url) - 1] = '\0';
        s_cache_count++;
        save_cache_file();
    }
}

int catbox_upload_icon(const char *title_id, const char *file_path, char *out_url, size_t cap) {
    if (!title_id || !file_path || !out_url || cap < 32) return 0;
    out_url[0] = '\0';

    // 1. Ensure persistent cache is loaded
    load_cache_file();

    // 2. Check cache (avoids reuploading if already uploaded before)
    for (int i = 0; i < s_cache_count; i++) {
        if (strcmp(s_cache[i].title_id, title_id) == 0) {
            strncpy(out_url, s_cache[i].url, cap - 1);
            out_url[cap - 1] = '\0';
            printf("[PS5-RPC] Using persistent cached Catbox icon for %s: %s\n", title_id, out_url);
            return 1;
        }
    }

    // 3. Read PNG file from disk
    FILE *f = fopen(file_path, "rb");
    if (!f) {
        printf("[PS5-RPC] Cannot open icon file: %s\n", file_path);
        return 0;
    }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long fsize = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0 || fsize <= 0 || fsize > 8 * 1024 * 1024) {
        fclose(f);
        return 0;
    }

    unsigned char *file_data = malloc((size_t)fsize);
    if (!file_data) { fclose(f); return 0; }

    size_t bytes_read = fread(file_data, 1, (size_t)fsize, f);
    fclose(f);
    if (bytes_read != (size_t)fsize) {
        free(file_data);
        return 0;
    }

    // 4. Resolve catbox.moe IP (system DNS with public DNS fallback)
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(443);
    int ok = 0;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(CATBOX_HOST, "443", &hints, &res) == 0 && res) {
        struct sockaddr_in *sin = (struct sockaddr_in*)res->ai_addr;
        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        if (ip != 0 && (ip >> 24) != 127) {
            sa.sin_addr = sin->sin_addr;
            ok = 1;
        }
        freeaddrinfo(res);
    }

    if (!ok) {
        if (resolve_via_public_dns(CATBOX_HOST, &sa.sin_addr) == 0) {
            ok = 1;
        }
    }

    if (!ok) {
        printf("[PS5-RPC] DNS resolution failed for %s\n", CATBOX_HOST);
        free(file_data);
        return 0;
    }

    // 5. Connect TCP socket + TLS
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        free(file_data);
        return 0;
    }

    struct timeval tv = { .tv_sec = CATBOX_DEADLINE_S, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (connect(fd, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        printf("[PS5-RPC] Connect to %s:443 failed\n", CATBOX_HOST);
        close(fd);
        free(file_data);
        return 0;
    }

    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    tls_ctx_t *t = tls_start(fd, CATBOX_HOST);
    if (!t) {
        printf("[PS5-RPC] TLS handshake failed for %s\n", CATBOX_HOST);
        close(fd);
        free(file_data);
        return 0;
    }

    // 6. Construct multipart/form-data
    char part_head[512];
    int head_len = snprintf(part_head, sizeof(part_head),
        "--" BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"reqtype\"\r\n\r\n"
        "fileupload\r\n"
        "--" BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"fileToUpload\"; filename=\"icon0.png\"\r\n"
        "Content-Type: image/png\r\n\r\n");

    const char part_tail[] = "\r\n--" BOUNDARY "--\r\n";
    size_t tail_len = strlen(part_tail);
    size_t total_content_len = (size_t)head_len + (size_t)fsize + tail_len;

    char http_req[512];
    int req_len = snprintf(http_req, sizeof(http_req),
        "POST /user/api.php HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: Mozilla/5.0\r\n"
        "Content-Type: multipart/form-data; boundary=" BOUNDARY "\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        CATBOX_HOST, total_content_len);

    printf("[PS5-RPC] Uploading %s icon0.png (%ld bytes) to Catbox.moe...\n", title_id, fsize);

    int write_err = 0;
    if (tls_write(t, http_req, (size_t)req_len) < 0) write_err = 1;
    if (!write_err && tls_write(t, part_head, (size_t)head_len) < 0) write_err = 1;
    if (!write_err && tls_write(t, file_data, (size_t)fsize) < 0) write_err = 1;
    if (!write_err && tls_write(t, part_tail, tail_len) < 0) write_err = 1;

    free(file_data);

    if (write_err) {
        printf("[PS5-RPC] Failed writing upload data to TLS socket\n");
        tls_free(t);
        close(fd);
        return 0;
    }

    // 7. Read HTTP response
    static char raw_resp[4096];
    size_t bl = 0;
    int64_t dl = orbis_mono_s() + CATBOX_DEADLINE_S + 5;
    for (;;) {
        char tmp[1024];
        int r = tls_read(t, tmp, sizeof(tmp) - 1);
        if (r < 0) break;
        if (r == 0) {
            if (orbis_mono_s() > dl) break;
            usleep(20000);
            continue;
        }
        if (bl + (size_t)r >= sizeof(raw_resp) - 1) break;
        memcpy(raw_resp + bl, tmp, (size_t)r);
        bl += (size_t)r;
        if (orbis_mono_s() > dl) break;
    }

    tls_free(t);
    close(fd);

    if (bl == 0) {
        printf("[PS5-RPC] Catbox returned empty response\n");
        return 0;
    }
    raw_resp[bl] = '\0';

    int st = 0;
    size_t olen = 0;
    char *body = upd_parse_response(raw_resp, bl, sizeof(raw_resp), &st, &olen, NULL, 0);
    if (!body || st != 200) {
        printf("[PS5-RPC] Catbox HTTP status %d\n", st);
        if (body) free(body);
        return 0;
    }

    // Catbox body is the raw direct URL, e.g. "https://files.catbox.moe/xxxxxx.png\n"
    char *url_p = body;
    while (*url_p == ' ' || *url_p == '\r' || *url_p == '\n' || *url_p == '\t') url_p++;
    size_t ulen = strlen(url_p);
    while (ulen > 0 && (url_p[ulen - 1] == '\r' || url_p[ulen - 1] == '\n' || url_p[ulen - 1] == ' ' || url_p[ulen - 1] == '\t')) {
        url_p[--ulen] = '\0';
    }

    int success = 0;
    if (strstr(url_p, "files.catbox.moe/")) {
        if (strncmp(url_p, "http://", 7) == 0) {
            snprintf(out_url, cap, "https://%s", url_p + 7);
        } else {
            strncpy(out_url, url_p, cap - 1);
            out_url[cap - 1] = '\0';
        }
        success = 1;

        // Cache result in memory and persist to JSON file on disk
        if (s_cache_count < CATBOX_MAX_CACHE) {
            strncpy(s_cache[s_cache_count].title_id, title_id, sizeof(s_cache[s_cache_count].title_id) - 1);
            s_cache[s_cache_count].title_id[sizeof(s_cache[s_cache_count].title_id) - 1] = '\0';
            strncpy(s_cache[s_cache_count].url, out_url, sizeof(s_cache[s_cache_count].url) - 1);
            s_cache[s_cache_count].url[sizeof(s_cache[s_cache_count].url) - 1] = '\0';
            s_cache_count++;
            save_cache_file();
        }
        printf("[PS5-RPC] Successfully uploaded icon to Catbox: %s\n", out_url);
    } else {
        printf("[PS5-RPC] Unexpected response from Catbox: %s\n", body);
    }

    free(body);
    return success;
}
