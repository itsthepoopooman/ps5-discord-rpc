#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

RpcConfig g_config;
char g_active_config_path[256] = CONFIG_PATH;

void config_set_defaults(RpcConfig* cfg) {
    memset(cfg, 0, sizeof(RpcConfig));
    cfg->token[0] = '\0';
    cfg->details[0] = '\0';
    cfg->state[0] = '\0';
    
    cfg->btn1_enabled = false;
    cfg->btn1_label[0] = '\0';
    cfg->btn1_url[0] = '\0';
    
    cfg->btn2_enabled = false;
    cfg->btn2_label[0] = '\0';
    cfg->btn2_url[0] = '\0';
    
    cfg->show_badge = false;
    cfg->show_timer = true;
    cfg->show_idle = false;
    cfg->needs_update = true;
}

static void extract_string_field(const char* json, const char* key, char* out, size_t out_size) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char* p = strstr(json, search);
    if (!p) {
        out[0] = '\0';
        return;
    }
    
    p = strchr(p + strlen(search), ':');
    if (!p) {
        out[0] = '\0';
        return;
    }
    p++;
    
    p = strchr(p, '\"');
    if (!p) {
        out[0] = '\0';
        return;
    }
    p++;
    
    char* end = strchr(p, '\"');
    if (!end) {
        out[0] = '\0';
        return;
    }
    
    size_t len = (size_t)(end - p);
    if (len >= out_size) len = out_size - 1;
    strncpy(out, p, len);
    out[len] = '\0';
}

static void extract_bool_field(const char* json, const char* key, bool* out) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char* p = strstr(json, search);
    if (!p) return;
    
    p = strchr(p + strlen(search), ':');
    if (!p) return;
    p++;
    
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
    } else if (strncmp(p, "false", 5) == 0) {
        *out = false;
    }
}

bool config_parse_json(RpcConfig* cfg, const char* json) {
    if (!json || strlen(json) == 0) return false;
    
    extract_string_field(json, "token", cfg->token, sizeof(cfg->token));
    extract_string_field(json, "details", cfg->details, sizeof(cfg->details));
    extract_string_field(json, "state", cfg->state, sizeof(cfg->state));
    
    extract_bool_field(json, "btn1_enabled", &cfg->btn1_enabled);
    extract_string_field(json, "btn1_label", cfg->btn1_label, sizeof(cfg->btn1_label));
    extract_string_field(json, "btn1_url", cfg->btn1_url, sizeof(cfg->btn1_url));
    
    extract_bool_field(json, "btn2_enabled", &cfg->btn2_enabled);
    extract_string_field(json, "btn2_label", cfg->btn2_label, sizeof(cfg->btn2_label));
    extract_string_field(json, "btn2_url", cfg->btn2_url, sizeof(cfg->btn2_url));
    
    extract_bool_field(json, "show_badge", &cfg->show_badge);
    extract_bool_field(json, "show_timer", &cfg->show_timer);
    extract_bool_field(json, "show_idle", &cfg->show_idle);
    
    // Parse blacklist array of title IDs
    const char* bl = strstr(json, "\"blacklist\"");
    if (bl) {
        const char* arr = strchr(bl, '[');
        if (arr) {
            const char* end_arr = strchr(arr, ']');
            if (end_arr) {
                cfg->blacklist_count = 0;
                const char* p = arr + 1;
                while (p < end_arr && cfg->blacklist_count < MAX_BLACKLIST) {
                    const char* q1 = strchr(p, '\"');
                    if (!q1 || q1 >= end_arr) break;
                    const char* q2 = strchr(q1 + 1, '\"');
                    if (!q2 || q2 >= end_arr) break;
                    size_t len = (size_t)(q2 - (q1 + 1));
                    if (len > 0 && len < sizeof(cfg->blacklist[0])) {
                        strncpy(cfg->blacklist[cfg->blacklist_count], q1 + 1, len);
                        cfg->blacklist[cfg->blacklist_count][len] = '\0';
                        cfg->blacklist_count++;
                    }
                    p = q2 + 1;
                }
            }
        }
    }
    
    cfg->needs_update = true;
    return true;
}

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

static bool load_file_content(const char* path, char** out_buf, size_t* out_sz) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (sz <= 0 || sz > 65536) {
        fclose(f);
        return false;
    }
    
    char* buf = (char*)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    
    *out_buf = buf;
    if (out_sz) *out_sz = sz;
    return true;
}

bool config_load(RpcConfig* cfg) {
    config_set_defaults(cfg);
    
    char* buf = NULL;
    const char* loaded_path = NULL;
    
    if (load_file_content(CONFIG_PATH, &buf, NULL)) {
        loaded_path = CONFIG_PATH;
    } else if (load_file_content(CONFIG_PATH_FALLBACK, &buf, NULL)) {
        loaded_path = CONFIG_PATH_FALLBACK;
    }
    
    if (!loaded_path || !buf) {
        printf("[PS5-RPC] No existing config found at %s or %s\n", CONFIG_PATH, CONFIG_PATH_FALLBACK);
        return false;
    }
    
    strncpy(g_active_config_path, loaded_path, sizeof(g_active_config_path) - 1);
    printf("[PS5-RPC] Loaded config from %s\n", loaded_path);
    
    config_parse_json(cfg, buf);
    free(buf);
    return true;
}

static bool write_config_to_file(const RpcConfig* cfg, const char* path, const char* dir) {
    mkdir_p(dir);
    
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    
    fprintf(f, "{\n");
    fprintf(f, "  \"token\": \"%s\",\n", cfg->token);
    fprintf(f, "  \"details\": \"%s\",\n", cfg->details);
    fprintf(f, "  \"state\": \"%s\",\n", cfg->state);
    fprintf(f, "  \"btn1_enabled\": %s,\n", cfg->btn1_enabled ? "true" : "false");
    fprintf(f, "  \"btn1_label\": \"%s\",\n", cfg->btn1_label);
    fprintf(f, "  \"btn1_url\": \"%s\",\n", cfg->btn1_url);
    fprintf(f, "  \"btn2_enabled\": %s,\n", cfg->btn2_enabled ? "true" : "false");
    fprintf(f, "  \"btn2_label\": \"%s\",\n", cfg->btn2_label);
    fprintf(f, "  \"btn2_url\": \"%s\",\n", cfg->btn2_url);
    fprintf(f, "  \"show_badge\": %s,\n", cfg->show_badge ? "true" : "false");
    fprintf(f, "  \"show_timer\": %s,\n", cfg->show_timer ? "true" : "false");
    fprintf(f, "  \"show_idle\": %s,\n", cfg->show_idle ? "true" : "false");
    fprintf(f, "  \"blacklist\": [");
    for (int i = 0; i < cfg->blacklist_count; i++) {
        fprintf(f, "\"%s\"%s", cfg->blacklist[i], (i + 1 < cfg->blacklist_count) ? ", " : "");
    }
    fprintf(f, "]\n");
    fprintf(f, "}\n");
    
    fclose(f);
    return true;
}

bool config_build_json(const RpcConfig* cfg, char* out, size_t out_sz) {
    if (!cfg || !out || out_sz == 0) return false;
    snprintf(out, out_sz,
        "{\n"
        "  \"token\": \"%s\",\n"
        "  \"custom_details\": \"%s\",\n"
        "  \"custom_state\": \"%s\",\n"
        "  \"btn1_label\": \"%s\",\n"
        "  \"btn1_url\": \"%s\",\n"
        "  \"btn2_label\": \"%s\",\n"
        "  \"btn2_url\": \"%s\",\n"
        "  \"show_badge\": %s,\n"
        "  \"show_timer\": %s,\n"
        "  \"show_idle\": %s\n"
        "}",
        cfg->token,
        cfg->details,
        cfg->state,
        cfg->btn1_label,
        cfg->btn1_url,
        cfg->btn2_label,
        cfg->btn2_url,
        cfg->show_badge ? "true" : "false",
        cfg->show_timer ? "true" : "false",
        cfg->show_idle ? "true" : "false"
    );
    return true;
}

bool config_save(const RpcConfig* cfg) {
    if (write_config_to_file(cfg, CONFIG_PATH, CONFIG_DIR)) {
        strncpy(g_active_config_path, CONFIG_PATH, sizeof(g_active_config_path) - 1);
        printf("[PS5-RPC] Config saved successfully to %s\n", CONFIG_PATH);
        return true;
    }
    
    if (write_config_to_file(cfg, CONFIG_PATH_FALLBACK, CONFIG_DIR_FALLBACK)) {
        strncpy(g_active_config_path, CONFIG_PATH_FALLBACK, sizeof(g_active_config_path) - 1);
        printf("[PS5-RPC] Config saved successfully to fallback %s\n", CONFIG_PATH_FALLBACK);
        return true;
    }
    
    fprintf(stderr, "[PS5-RPC] ERROR: Failed to write config to both %s and %s!\n", 
            CONFIG_PATH, CONFIG_PATH_FALLBACK);
    return false;
}

bool config_is_blacklisted(const char* title_id) {
    if (!title_id || !title_id[0]) return false;
    for (int i = 0; i < g_config.blacklist_count; i++) {
        if (strcmp(g_config.blacklist[i], title_id) == 0) return true;
    }
    return false;
}

bool config_set_blacklist(const char* title_id, bool blacklisted) {
    if (!title_id || !title_id[0]) return false;
    int found_idx = -1;
    for (int i = 0; i < g_config.blacklist_count; i++) {
        if (strcmp(g_config.blacklist[i], title_id) == 0) {
            found_idx = i;
            break;
        }
    }
    
    if (blacklisted) {
        if (found_idx < 0 && g_config.blacklist_count < MAX_BLACKLIST) {
            strncpy(g_config.blacklist[g_config.blacklist_count], title_id, sizeof(g_config.blacklist[0]) - 1);
            g_config.blacklist[g_config.blacklist_count][sizeof(g_config.blacklist[0]) - 1] = '\0';
            g_config.blacklist_count++;
            return true;
        }
    } else {
        if (found_idx >= 0) {
            for (int i = found_idx; i < g_config.blacklist_count - 1; i++) {
                memcpy(g_config.blacklist[i], g_config.blacklist[i + 1], sizeof(g_config.blacklist[0]));
            }
            g_config.blacklist_count--;
            return true;
        }
    }
    return false;
}
