#include "webserver.h"
#include "config.h"
#include "notify.h"
#include "web_ui.h"
#include "title_resolver.h"
#include "discord_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>

#define WEBSERVER_PORT 6767

static void handle_options(int client_fd) {
    const char resp[] =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";
    write(client_fd, resp, strlen(resp));
}

static bool get_query_param(const char* req, const char* key, char* out, size_t out_sz) {
    if (!req || !key || !out || out_sz == 0) return false;
    out[0] = '\0';

    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char* p = strstr(req, search);
    if (!p) return false;

    p += strlen(search);
    size_t i = 0;
    while (*p && *p != '&' && *p != ' ' && *p != '\r' && *p != '\n' && i < out_sz - 1) {
        if (*p == '%' && *(p+1) && *(p+2)) {
            char hex[3] = { *(p+1), *(p+2), 0 };
            out[i++] = (char)strtol(hex, NULL, 16);
            p += 3;
        } else if (*p == '+') {
            out[i++] = ' ';
            p++;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = '\0';
    return true;
}



static void handle_get_config(int client_fd) {
    char json[2048];
    config_build_json(&g_config, json, sizeof(json));

    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Expires: 0\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        strlen(json));

    write(client_fd, header, strlen(header));
    write(client_fd, json, strlen(json));
}

static void handle_get_games(int client_fd) {
    InstalledGameEntry* entries = (InstalledGameEntry*)calloc(256, sizeof(InstalledGameEntry));
    if (!entries) {
        const char err[] = "HTTP/1.1 500 Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write(client_fd, err, strlen(err));
        return;
    }

    int count = get_installed_games(entries, 256);

    size_t cap = 131072;
    char* json = (char*)malloc(cap);
    if (!json) {
        free(entries);
        const char err[] = "HTTP/1.1 500 Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write(client_fd, err, strlen(err));
        return;
    }

    size_t pos = 0;
    pos += snprintf(json + pos, cap - pos, "[");
    for (int i = 0; i < count; i++) {
        char clean_name[256];
        size_t cj = 0;
        for (size_t ci = 0; entries[i].title_name[ci] && cj < sizeof(clean_name) - 2; ci++) {
            if (entries[i].title_name[ci] == '\"' || entries[i].title_name[ci] == '\\') {
                clean_name[cj++] = '\\';
            }
            clean_name[cj++] = entries[i].title_name[ci];
        }
        clean_name[cj] = '\0';

        pos += snprintf(json + pos, cap - pos,
            "%s{\"title_id\":\"%s\",\"title_name\":\"%s\",\"platform\":\"%s\",\"icon_url\":\"%s\",\"blacklisted\":%s}",
            (i > 0 ? "," : ""),
            entries[i].title_id,
            clean_name,
            entries[i].platform,
            entries[i].icon_url,
            entries[i].is_blacklisted ? "true" : "false"
        );
    }
    pos += snprintf(json + pos, cap - pos, "]");

    free(entries);

    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        pos);

    write(client_fd, header, strlen(header));
    write(client_fd, json, pos);
    free(json);
}

#include "embedded_icon.h"

static void handle_get_icon(int client_fd, const char* req) {
    char tid[64] = "";
    get_query_param(req, "id", tid, sizeof(tid));
    if (!tid[0]) {
        const char not_found[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write(client_fd, not_found, strlen(not_found));
        return;
    }

    /* If toast or default icon requested, serve custom/embedded Discord PNG */
    if (strcmp(tid, "toast") == 0 || strcmp(tid, "default") == 0) {
        FILE* f = fopen("/user/data/discord_rpc/icon.png", "rb");
        if (!f) f = fopen("/data/discord_rpc/icon.png", "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0 && sz <= 8 * 1024 * 1024) {
                char header[256];
                snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: image/png\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Cache-Control: no-cache\r\n"
                    "Content-Length: %ld\r\n"
                    "Connection: close\r\n\r\n",
                    sz);
                write(client_fd, header, strlen(header));
                char chunk[8192];
                size_t r;
                while ((r = fread(chunk, 1, sizeof(chunk), f)) > 0) {
                    write(client_fd, chunk, r);
                }
                fclose(f);
                return;
            }
            fclose(f);
        }

        /* Direct fallback to embedded Discord icon memory */
        char header[256];
        snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: image/png\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: public, max-age=86400\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n",
            embedded_icon_png_size);
        write(client_fd, header, strlen(header));
        write(client_fd, embedded_icon_png, embedded_icon_png_size);
        return;
    }

    char file_path[256] = "";
    if (find_readable_icon(tid, NULL, file_path, sizeof(file_path))) {
        FILE* f = fopen(file_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0 && sz <= 8 * 1024 * 1024) {
                char header[256];
                snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: image/png\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Cache-Control: public, max-age=86400\r\n"
                    "Content-Length: %ld\r\n"
                    "Connection: close\r\n\r\n",
                    sz);
                write(client_fd, header, strlen(header));

                char chunk[8192];
                size_t r;
                while ((r = fread(chunk, 1, sizeof(chunk), f)) > 0) {
                    write(client_fd, chunk, r);
                }
                fclose(f);
                return;
            }
            fclose(f);
        }
    }

    /* Fallback: serve embedded Discord icon */
    char header[256];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: image/png\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Cache-Control: public, max-age=86400\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        embedded_icon_png_size);
    write(client_fd, header, strlen(header));
    write(client_fd, embedded_icon_png, embedded_icon_png_size);
}

static void handle_toggle_blacklist(int client_fd, const char* req) {
    char tid[64] = "";
    char bl_val[16] = "";
    get_query_param(req, "id", tid, sizeof(tid));
    get_query_param(req, "blacklisted", bl_val, sizeof(bl_val));

    if (tid[0]) {
        bool bl = (strcmp(bl_val, "true") == 0);
        config_set_blacklist(tid, bl);
        config_save(&g_config);
        g_config.needs_update = true;
    }

    const char resp[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: 15\r\n"
        "Connection: close\r\n\r\n"
        "{\"status\":\"ok\"}";
    write(client_fd, resp, strlen(resp));
}


static void handle_upload_icon(int client_fd, const char* req, size_t req_len) {
    const char* marker = "\r\n\r\n";
    char* body = strstr(req, marker);
    if (!body) {
        const char err[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write(client_fd, err, strlen(err));
        return;
    }
    body += 4;
    size_t body_len = req_len - (body - req);

    const unsigned char png_magic[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    const unsigned char* png_data = NULL;
    size_t png_len = 0;

    for (size_t i = 0; i + 8 <= body_len; i++) {
        if (memcmp(body + i, png_magic, 8) == 0) {
            png_data = (const unsigned char*)(body + i);
            png_len = body_len - i;
            break;
        }
    }

    if (!png_data || png_len < 8) {
        const char err[] = "HTTP/1.1 400 Not a valid PNG\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write(client_fd, err, strlen(err));
        return;
    }

    mkdir("/data/discord_rpc", 0777);
    chmod("/data/discord_rpc", 0777);

    FILE* f2 = fopen("/data/discord_rpc/icon.png", "wb");
    if (f2) {
        fwrite(png_data, 1, png_len, f2);
        fclose(f2);
        chmod("/data/discord_rpc/icon.png", 0666);
    }

    FILE* f3 = fopen("/data/discord.png", "wb");
    if (f3) {
        fwrite(png_data, 1, png_len, f3);
        fclose(f3);
        chmod("/data/discord.png", 0666);
    }

    printf("[PS5-RPC] Saved custom notification PNG: /data/discord_rpc/icon.png (%zu bytes)\n", png_len);

    const char resp[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: 15\r\n"
        "Connection: close\r\n\r\n"
        "{\"status\":\"ok\"}";
    write(client_fd, resp, strlen(resp));
}

static ssize_t read_full_request(int client_fd, char* buf, size_t max_sz) {
    size_t total = 0;
    int expected_len = -1;
    char* body = NULL;

    while (total < max_sz - 1) {
        ssize_t n = read(client_fd, buf + total, max_sz - 1 - total);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';

        if (!body) {
            body = strstr(buf, "\r\n\r\n");
            if (body) {
                body += 4;
                char* cl = strstr(buf, "Content-Length:");
                if (!cl) cl = strstr(buf, "content-length:");
                if (cl && cl < body) {
                    expected_len = atoi(cl + 15);
                } else {
                    expected_len = 0;
                }
            }
        }

        if (body && expected_len >= 0) {
            size_t body_sz = total - (body - buf);
            if (body_sz >= (size_t)expected_len) {
                break;
            }
        }
    }
    return total;
}

static bool extract_json_str(const char* json, const char* key, char* out, size_t out_sz) {
    if (!json || !key || !out || out_sz == 0) return false;
    out[0] = '\0';
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char* p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '\"' && i < out_sz - 1) {
        if (*p == '\\' && *(p+1)) p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

static void handle_auth_login_req(int client_fd, const char* body) {
    char login[128] = "";
    char password[128] = "";
    extract_json_str(body, "login", login, sizeof(login));
    extract_json_str(body, "password", password, sizeof(password));

    char resp_buf[8192] = "";
    int res = discord_auth_login(login, password, resp_buf, sizeof(resp_buf));
    if (res != 0 || resp_buf[0] == '\0') {
        const char err[] = "HTTP/1.1 502 Bad Gateway\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 36\r\nConnection: close\r\n\r\n{\"status\":\"error\",\"message\":\"failed\"}";
        write(client_fd, err, strlen(err));
        return;
    }

    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        strlen(resp_buf)
    );
    write(client_fd, header, strlen(header));
    write(client_fd, resp_buf, strlen(resp_buf));
}

static void handle_auth_mfa_req(int client_fd, const char* body) {
    char ticket[128] = "";
    char code[32] = "";
    extract_json_str(body, "ticket", ticket, sizeof(ticket));
    extract_json_str(body, "code", code, sizeof(code));

    char resp_buf[8192] = "";
    int res = discord_auth_mfa(ticket, code, resp_buf, sizeof(resp_buf));
    if (res != 0 || resp_buf[0] == '\0') {
        const char err[] = "HTTP/1.1 502 Bad Gateway\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 36\r\nConnection: close\r\n\r\n{\"status\":\"error\",\"message\":\"failed\"}";
        write(client_fd, err, strlen(err));
        return;
    }

    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        strlen(resp_buf)
    );
    write(client_fd, header, strlen(header));
    write(client_fd, resp_buf, strlen(resp_buf));
}

static void handle_client(int client_fd) {
    char req[8192];
    ssize_t bytes = read_full_request(client_fd, req, sizeof(req));
    if (bytes <= 0) {
        close(client_fd);
        return;
    }

    if (strncmp(req, "OPTIONS", 7) == 0) {
        handle_options(client_fd);
    } else if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index.html", 15) == 0) {
        size_t html_len = strlen(WEB_UI_HTML);
        char header[512];
        snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=UTF-8\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Expires: 0\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n",
            html_len);
        write(client_fd, header, strlen(header));
        write(client_fd, WEB_UI_HTML, html_len);
    } else if (strncmp(req, "GET /api/config", 15) == 0) {
        handle_get_config(client_fd);
    } else if (strncmp(req, "GET /api/games", 14) == 0) {
        handle_get_games(client_fd);
    } else if (strncmp(req, "GET /api/blacklist", 18) == 0) {
        handle_toggle_blacklist(client_fd, req);
    } else if (strncmp(req, "GET /api/icon", 13) == 0) {
        handle_get_icon(client_fd, req);
    } else if (strncmp(req, "POST /api/auth/login", 20) == 0) {
        char* body = strstr(req, "\r\n\r\n");
        if (body) {
            handle_auth_login_req(client_fd, body + 4);
        } else {
            const char resp[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            write(client_fd, resp, strlen(resp));
        }
    } else if (strncmp(req, "POST /api/auth/mfa", 18) == 0) {
        char* body = strstr(req, "\r\n\r\n");
        if (body) {
            handle_auth_mfa_req(client_fd, body + 4);
        } else {
            const char resp[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            write(client_fd, resp, strlen(resp));
        }
    } else if (strncmp(req, "POST /api/upload_icon", 21) == 0) {
        handle_upload_icon(client_fd, req, (size_t)bytes);
    } else if (strncmp(req, "POST /api/config", 16) == 0) {
        char* body = strstr(req, "\r\n\r\n");
        if (body) {
            body += 4;
            config_parse_json(&g_config, body);
            bool saved = config_save(&g_config);
            g_config.needs_update = true;
            
            if (saved) {
                const char resp[] = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: 15\r\n"
                    "Connection: close\r\n\r\n"
                    "{\"status\":\"ok\"}";
                write(client_fd, resp, strlen(resp));
            } else {
                const char resp[] = 
                    "HTTP/1.1 500 Error\r\n"
                    "Content-Type: application/json\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: 18\r\n"
                    "Connection: close\r\n\r\n"
                    "{\"status\":\"error\"}";
                write(client_fd, resp, strlen(resp));
            }
        } else {
            const char resp[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            write(client_fd, resp, strlen(resp));
        }
    } else {
        const char not_found[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write(client_fd, not_found, strlen(not_found));
    }

    close(client_fd);
}

void* webserver_thread(void* arg) {
    (void)arg;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NULL;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(WEBSERVER_PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 16) < 0) {
        close(server_fd);
        return NULL;
    }

    printf("[PS5-RPC] Web Server running at http://0.0.0.0:%d\n", WEBSERVER_PORT);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd >= 0) {
            handle_client(client_fd);
        }
    }

    close(server_fd);
    return NULL;
}
