#include "title_resolver.h"
#include "config.h"
#include "sfo.h"
#include "tmdb.h"
#include "steam.h"
#include "catbox.h"
#include "timesync.h"
#include "art.h"
#include "discord.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

GameInfo g_current_game;

// Curated official high-resolution art for major PlayStation exclusives
static const struct {
    const char* prefix;
    const char* icon_url;
} s_ps_exclusives[] = {
    {"PPSA01339", "https://image.api.playstation.com/vulcan/img/rnd/202011/1717/GemhuAoqyXl9WcM9xQY01k2r.png"},
    {"PPSA01340", "https://image.api.playstation.com/vulcan/img/rnd/202011/1717/GemhuAoqyXl9WcM9xQY01k2r.png"},
    {"PPSA21564", "https://image.api.playstation.com/vulcan/ap/rnd/202405/2816/094b8e210515aa0fdf4fa5a9b9a59b9c916248c5ddfc39d2.png"},
    {"PPSA21565", "https://image.api.playstation.com/vulcan/ap/rnd/202405/2816/094b8e210515aa0fdf4fa5a9b9a59b9c916248c5ddfc39d2.png"},
    {"PPSA01521", "https://image.api.playstation.com/vulcan/ap/rnd/202109/1321/yZ7uHkWnT9c4c4X6iKjV7t7e.png"},
    {"PPSA01522", "https://image.api.playstation.com/vulcan/ap/rnd/202109/1321/yZ7uHkWnT9c4c4X6iKjV7t7e.png"},
    {"CUSA00207", "https://image.api.playstation.com/vulcan/img/rnd/202010/2614/NVNmBruQLBaAHxNLvH0c20aE.png"},
    {"CUSA03173", "https://image.api.playstation.com/vulcan/img/rnd/202010/2614/NVNmBruQLBaAHxNLvH0c20aE.png"},
    {"PPSA10664", "https://image.api.playstation.com/vulcan/ap/rnd/202401/1611/bf8aa61d76ae5298a58ecad380ce32bc5ebdd0a56f5a3e1b.png"},
    {"PPSA10665", "https://image.api.playstation.com/vulcan/ap/rnd/202401/1611/bf8aa61d76ae5298a58ecad380ce32bc5ebdd0a56f5a3e1b.png"},
    {NULL, NULL}
};

static void parse_json_value(const char* json, const char* key, char* out, size_t out_sz) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char* p = strstr(json, search);
    if (!p) return;
    
    p = strchr(p + strlen(search), ':');
    if (!p) return;
    
    p = strchr(p, '\"');
    if (!p) return;
    p++;
    
    char* end = strchr(p, '\"');
    if (!end) return;
    
    size_t len = (size_t)(end - p);
    if (len >= out_sz) len = out_sz - 1;
    strncpy(out, p, len);
    out[len] = '\0';
}

static bool read_file_bytes(const char* path, char* buf, size_t cap, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    if (n == 0) return false;
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return true;
}

static bool file_exists_nonempty(const char* path) {
    if (!path || !path[0]) return false;
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0);
}

bool find_readable_icon(const char* title_id, const char* raw_folder, char* out_file, size_t out_cap) {
    if (!out_file || out_cap == 0) return false;
    out_file[0] = '\0';

    static const char* const kCandidates[] = {
        "/user/appmeta/%s/icon0.png",
        "/user/appmeta/%s/sce_sys/icon0.png",
        "/user/appmeta/external/%s/icon0.png",
        "/user/appmeta/external/%s/sce_sys/icon0.png",
        "/system_data/priv/appmeta/%s/icon0.png",
        "/system_data/priv/appmeta/%s/sce_sys/icon0.png",
        "/system_data/priv/appmeta/external/%s/icon0.png",
        "/system_data/priv/appmeta/external/%s/sce_sys/icon0.png",
        "/user/app/%s/sce_sys/icon0.png",
        "/user/app/%s/icon0.png",
        "/user/patch/%s/sce_sys/icon0.png",
        "/system_ex/app/%s/sce_sys/icon0.png",
        "/mnt/shadowmnt/%s/sce_sys/icon0.png",
        "/mnt/sandbox/%s/app0/sce_sys/icon0.png",
        "/mnt/sandbox/pfsmnt/%s/sce_sys/icon0.png",
        "/mnt/sandbox/pfsmnt/%s-app0/sce_sys/icon0.png",
        NULL
    };

    char test_path[256];
    for (int i = 0; kCandidates[i]; i++) {
        if (title_id && title_id[0]) {
            snprintf(test_path, sizeof(test_path), kCandidates[i], title_id);
            if (file_exists_nonempty(test_path)) {
                strncpy(out_file, test_path, out_cap - 1);
                out_file[out_cap - 1] = '\0';
                return true;
            }
        }
        if (raw_folder && raw_folder[0]) {
            snprintf(test_path, sizeof(test_path), kCandidates[i], raw_folder);
            if (file_exists_nonempty(test_path)) {
                strncpy(out_file, test_path, out_cap - 1);
                out_file[out_cap - 1] = '\0';
                return true;
            }
        }
    }

    return false;
}

bool find_readable_metadata(const char* title_id, const char* raw_folder, char* out_name, size_t out_sz) {
    if (!out_name || out_sz == 0) return false;
    out_name[0] = '\0';

    static const char* const kJsonPaths[] = {
        "/user/appmeta/%s/param.json",
        "/user/appmeta/%s/sce_sys/param.json",
        "/user/appmeta/external/%s/param.json",
        "/user/appmeta/external/%s/sce_sys/param.json",
        "/system_data/priv/appmeta/%s/param.json",
        "/system_data/priv/appmeta/%s/sce_sys/param.json",
        "/system_data/priv/appmeta/external/%s/param.json",
        "/system_data/priv/appmeta/external/%s/sce_sys/param.json",
        "/user/patch/%s/sce_sys/param.json",
        "/user/app/%s/sce_sys/param.json",
        "/system_ex/app/%s/sce_sys/param.json",
        "/mnt/shadowmnt/%s/sce_sys/param.json",
        NULL
    };

    static const char* const kSfoPaths[] = {
        "/user/appmeta/%s/param.sfo",
        "/user/appmeta/%s/sce_sys/param.sfo",
        "/user/appmeta/external/%s/param.sfo",
        "/user/appmeta/external/%s/sce_sys/param.sfo",
        "/system_data/priv/appmeta/%s/param.sfo",
        "/system_data/priv/appmeta/%s/sce_sys/param.sfo",
        "/system_data/priv/appmeta/external/%s/param.sfo",
        "/system_data/priv/appmeta/external/%s/sce_sys/param.sfo",
        "/user/patch/%s/sce_sys/param.sfo",
        "/user/app/%s/sce_sys/param.sfo",
        "/system_ex/app/%s/sce_sys/param.sfo",
        "/mnt/shadowmnt/%s/sce_sys/param.sfo",
        NULL
    };

    char* buf = (char*)malloc(4096);
    if (!buf) return false;
    size_t sz = 0;
    char path[256];

    for (int i = 0; kJsonPaths[i]; i++) {
        if (title_id && title_id[0]) {
            snprintf(path, sizeof(path), kJsonPaths[i], title_id);
            if (read_file_bytes(path, buf, 4096, &sz)) {
                parse_json_value(buf, "titleName", out_name, out_sz);
                if (!out_name[0]) parse_json_value(buf, "title", out_name, out_sz);
                if (out_name[0]) { free(buf); return true; }
            }
        }
        if (raw_folder && raw_folder[0]) {
            snprintf(path, sizeof(path), kJsonPaths[i], raw_folder);
            if (read_file_bytes(path, buf, 4096, &sz)) {
                parse_json_value(buf, "titleName", out_name, out_sz);
                if (!out_name[0]) parse_json_value(buf, "title", out_name, out_sz);
                if (out_name[0]) { free(buf); return true; }
            }
        }
    }

    for (int i = 0; kSfoPaths[i]; i++) {
        if (title_id && title_id[0]) {
            snprintf(path, sizeof(path), kSfoPaths[i], title_id);
            if (read_file_bytes(path, buf, 4096, &sz)) {
                if (sfo_title((const unsigned char*)buf, sz, out_name, out_sz) == 0 && out_name[0]) {
                    free(buf);
                    return true;
                }
            }
        }
        if (raw_folder && raw_folder[0]) {
            snprintf(path, sizeof(path), kSfoPaths[i], raw_folder);
            if (read_file_bytes(path, buf, 4096, &sz)) {
                if (sfo_title((const unsigned char*)buf, sz, out_name, out_sz) == 0 && out_name[0]) {
                    free(buf);
                    return true;
                }
            }
        }
    }

    free(buf);
    return false;
}

bool resolve_game_metadata(const char* title_id, const char* raw_folder, char* title_name_out, size_t name_sz, char* icon_url_out, size_t icon_sz) {
    title_name_out[0] = '\0';
    icon_url_out[0] = '\0';

    // 1. Resolve title name from local param.json or param.sfo across all environments (including ShadowMount Plus)
    if (find_readable_metadata(title_id, raw_folder, title_name_out, name_sz)) {
        printf("[PS5-RPC] Resolved local title name: %s\n", title_name_out);
    }

    // 2. Resolve high-resolution official icon URL & fallback title name from Sony TMDB (PS4 games)
    char tmdb_name[128] = "";
    char tmdb_icon[256] = "";
    if (strncmp(title_id, "CUSA", 4) == 0) {
        printf("[PS5-RPC] Querying Sony TMDB for title %s...\n", title_id);
        if (tmdb_resolve(title_id, tmdb_name, sizeof(tmdb_name), tmdb_icon, sizeof(tmdb_icon)) == 0) {
            if (!title_name_out[0] && tmdb_name[0]) {
                strncpy(title_name_out, tmdb_name, name_sz - 1);
                title_name_out[name_sz - 1] = '\0';
                printf("[PS5-RPC] Resolved TMDB title name: %s\n", title_name_out);
            }
            if (tmdb_icon[0]) {
                if (strncmp(tmdb_icon, "http://", 7) == 0) {
                    snprintf(icon_url_out, icon_sz, "https://%s", tmdb_icon + 7);
                } else {
                    strncpy(icon_url_out, tmdb_icon, icon_sz - 1);
                    icon_url_out[icon_sz - 1] = '\0';
                }
                printf("[PS5-RPC] Resolved TMDB icon URL: %s\n", icon_url_out);
            }
        }
    }

    // 3. Known homebrew friendly names fallback
    if (!title_name_out[0]) {
        if (strcmp(title_id, "ITEM00001") == 0) strncpy(title_name_out, "Itemzflow Game Manager", name_sz - 1);
        else if (strcmp(title_id, "APOL00001") == 0 || strcmp(title_id, "APOL00000") == 0) strncpy(title_name_out, "Apollo Save Tool", name_sz - 1);
        else if (strcmp(title_id, "CHIA00001") == 0) strncpy(title_name_out, "Chiaki PS Remote Play", name_sz - 1);
        else {
            strncpy(title_name_out, title_id, name_sz - 1);
        }
        title_name_out[name_sz - 1] = '\0';
    }

    // 4. Check curated PlayStation exclusives table
    if (!icon_url_out[0]) {
        for (int i = 0; s_ps_exclusives[i].prefix; i++) {
            if (strncmp(title_id, s_ps_exclusives[i].prefix, strlen(s_ps_exclusives[i].prefix)) == 0) {
                strncpy(icon_url_out, s_ps_exclusives[i].icon_url, icon_sz - 1);
                icon_url_out[icon_sz - 1] = '\0';
                printf("[PS5-RPC] Resolved curated PlayStation exclusive icon: %s\n", icon_url_out);
                break;
            }
        }
    }

    // 5. PRIORITIZE LOCAL CONSOLE PNG UPLOAD TO CATBOX.MOE!
    // Supports native retail games, ShadowMount Plus mounts, and all homebrew apps.
    // Persistent JSON cache (/data/discord_rpc/icon_cache.json) guarantees it only uploads once.
    if (!icon_url_out[0]) {
        char local_icon[300] = {0};
        if (find_readable_icon(title_id, raw_folder, local_icon, sizeof(local_icon))) {
            printf("[PS5-RPC] Found readable local icon at %s, uploading to Catbox.moe...\n", local_icon);
            if (catbox_upload_icon(title_id, local_icon, icon_url_out, icon_sz) && icon_url_out[0]) {
                printf("[PS5-RPC] Successfully using Catbox icon on RPC: %s\n", icon_url_out);
            }
        } else {
            printf("[PS5-RPC] No local icon file could be located on disk for %s\n", title_id);
        }
    }

    // 6. Fallback: Query Steam Store Search if local icon was not found on disk or upload failed
    if (!icon_url_out[0] && title_name_out[0]) {
        char steam_icon[256] = "";
        printf("[PS5-RPC] Local icon not found. Fallback querying Steam Store for \"%s\"...\n", title_name_out);
        if (steam_resolve_icon(title_name_out, steam_icon, sizeof(steam_icon))) {
            strncpy(icon_url_out, steam_icon, icon_sz - 1);
            icon_url_out[icon_sz - 1] = '\0';
            printf("[PS5-RPC] Resolved game cover from Steam CDN: %s\n", icon_url_out);
        }
    }

    // 7. Last-resort fallback icon URL
    if (!icon_url_out[0]) {
        strncpy(icon_url_out,
                "https://raw.githubusercontent.com/SirHumza/orbisRPC/refs/heads/main/config/images/icons/ps-logo-full.png",
                icon_sz - 1);
        icon_url_out[icon_sz - 1] = '\0';
    }

    // 8. Pre-resolve with Discord Media Proxy (mp:) before returning!
    // Discord client requires large_image to start with "mp:" to render external URLs.
    // If it's a raw https:// URL, we query Discord's external-assets API and retry up to 5 times.
    if (icon_url_out[0] && strncmp(icon_url_out, "mp:", 3) != 0 && g_config.token[0]) {
        char mp_buf[512] = "";
        for (int attempt = 1; attempt <= 5; attempt++) {
            printf("[PS5-RPC] Resolving icon to Discord mp: proxy (attempt %d/5): %s\n", attempt, icon_url_out);
            if (art_resolve_mp(DEFAULT_APP_ID, g_config.token, icon_url_out, mp_buf, sizeof(mp_buf)) && mp_buf[0]) {
                strncpy(icon_url_out, mp_buf, icon_sz - 1);
                icon_url_out[icon_sz - 1] = '\0';
                printf("[PS5-RPC] Successfully resolved Discord mp: proxy icon: %s\n", icon_url_out);
                catbox_cache_update(title_id, mp_buf);
                break;
            }
            sleep(1);
        }
    }

    return true;
}

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>

static bool is_valid_title_id(const char* id) {
    if (!id) return false;
    size_t len = strlen(id);
    // Real PlayStation / Homebrew Title IDs are between 7 and 12 chars
    // e.g. CUSA00123 (9), PPSA01234 (9), ITEM00001 (9)
    if (len < 7 || len > 12) return false;

    int upper_count = 0;
    int digit_count = 0;
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        if (c >= 'A' && c <= 'Z') {
            upper_count++;
        } else if (c >= '0' && c <= '9') {
            digit_count++;
        } else {
            // Reject any lowercase letters (e.g. 'download', 'pfsmnt'), spaces, symbols
            return false;
        }
    }

    // Must have at least 2 uppercase letters and at least 3 digits
    if (upper_count < 2 || digit_count < 3) return false;

    // Explicit blacklist for system / daemon pseudo-IDs
    if (strncmp(id, "NPXS", 4) == 0 ||
        strncmp(id, "NPXX", 4) == 0 ||
        strncmp(id, "SCA", 3) == 0 ||
        strncmp(id, "SHELL", 5) == 0 ||
        strcmp(id, "COMMON") == 0) {
        return false;
    }

    return true;
}

// Scans active sandbox mounts and ShadowMount Plus structures to detect running title
static bool detect_active_title(char* title_id_out, size_t sz, char* raw_folder_out, size_t raw_sz) {
    if (title_id_out && sz > 0) title_id_out[0] = '\0';
    if (raw_folder_out && raw_sz > 0) raw_folder_out[0] = '\0';
    
    char best_id[32] = "";
    char best_folder[64] = "";
    bool found = false;

    // 1. Check /mnt/sandbox/pfsmnt (Standard retail / fPKG active mounts)
    DIR* dir = opendir("/mnt/sandbox/pfsmnt");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            
            char candidate[32] = "";
            char* dash = strchr(entry->d_name, '-');
            size_t len = dash ? (size_t)(dash - entry->d_name) : strlen(entry->d_name);
            if (len >= 4 && len < sizeof(candidate)) {
                strncpy(candidate, entry->d_name, len);
                candidate[len] = '\0';
                
                if (is_valid_title_id(candidate)) {
                    strncpy(best_id, candidate, sizeof(best_id) - 1);
                    best_id[sizeof(best_id) - 1] = '\0';
                    strncpy(best_folder, entry->d_name, sizeof(best_folder) - 1);
                    best_folder[sizeof(best_folder) - 1] = '\0';
                    found = true;
                    if (strstr(entry->d_name, "-app0")) break;
                }
            }
        }
        closedir(dir);
    }

    // 2. Check /mnt/sandbox directly (ShadowMount Plus per-process sandboxes: /mnt/sandbox/<TITLE_ID>_<pid>/)
    // Note: requires '_' or '-' in directory name to strictly avoid background daemons like "download", "pfsmnt"
    if (!found) {
        DIR* sdir = opendir("/mnt/sandbox");
        if (sdir) {
            struct dirent* entry;
            while ((entry = readdir(sdir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                if (strcmp(entry->d_name, "pfsmnt") == 0) continue;
                if (strcmp(entry->d_name, "download") == 0) continue;
                
                char* sep = strchr(entry->d_name, '_');
                if (!sep) sep = strchr(entry->d_name, '-');
                if (!sep) continue; // Real per-process sandbox directories ALWAYS contain a pid separator

                size_t len = (size_t)(sep - entry->d_name);
                char candidate[32] = "";
                if (len >= 4 && len < sizeof(candidate)) {
                    strncpy(candidate, entry->d_name, len);
                    candidate[len] = '\0';
                    
                    if (is_valid_title_id(candidate)) {
                        strncpy(best_id, candidate, sizeof(best_id) - 1);
                        best_id[sizeof(best_id) - 1] = '\0';
                        strncpy(best_folder, entry->d_name, sizeof(best_folder) - 1);
                        best_folder[sizeof(best_folder) - 1] = '\0';
                        found = true;
                        break;
                    }
                }
            }
            closedir(sdir);
        }
    }

    // 3. Check /mnt/shadowmnt (ShadowMount Plus active dynamic image mounts)
    if (!found) {
        DIR* smdir = opendir("/mnt/shadowmnt");
        if (smdir) {
            struct dirent* entry;
            while ((entry = readdir(smdir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                if (strcmp(entry->d_name, "pfsc") == 0) continue;
                
                char pj_path[256];
                snprintf(pj_path, sizeof(pj_path), "/mnt/shadowmnt/%s/sce_sys/param.json", entry->d_name);
                FILE* f = fopen(pj_path, "rb");
                if (f) {
                    char pbuf[4096];
                    size_t pn = fread(pbuf, 1, sizeof(pbuf) - 1, f);
                    fclose(f);
                    pbuf[pn] = '\0';
                    char tid[32] = {0};
                    parse_json_value(pbuf, "titleId", tid, sizeof(tid));
                    if (!tid[0]) parse_json_value(pbuf, "title_id", tid, sizeof(tid));
                    if (tid[0] && is_valid_title_id(tid)) {
                        strncpy(best_id, tid, sizeof(best_id) - 1);
                        best_id[sizeof(best_id) - 1] = '\0';
                        strncpy(best_folder, entry->d_name, sizeof(best_folder) - 1);
                        best_folder[sizeof(best_folder) - 1] = '\0';
                        found = true;
                        break;
                    }
                }
                
                char* sep = strchr(entry->d_name, '_');
                if (!sep) sep = strchr(entry->d_name, '-');
                size_t len = sep ? (size_t)(sep - entry->d_name) : strlen(entry->d_name);
                char candidate[32] = "";
                if (len >= 4 && len < sizeof(candidate)) {
                    strncpy(candidate, entry->d_name, len);
                    candidate[len] = '\0';
                    if (is_valid_title_id(candidate)) {
                        strncpy(best_id, candidate, sizeof(best_id) - 1);
                        best_id[sizeof(best_id) - 1] = '\0';
                        strncpy(best_folder, entry->d_name, sizeof(best_folder) - 1);
                        best_folder[sizeof(best_folder) - 1] = '\0';
                        found = true;
                        break;
                    }
                }
            }
            closedir(smdir);
        }
    }

    // 4. Check /system_ex/app (ShadowMount Plus active nullfs mounts)
    if (!found) {
        DIR* exdir = opendir("/system_ex/app");
        if (exdir) {
            struct dirent* entry;
            while ((entry = readdir(exdir)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                if (!is_valid_title_id(entry->d_name)) continue;
                
                char test_path[256];
                snprintf(test_path, sizeof(test_path), "/system_ex/app/%s/sce_sys/icon0.png", entry->d_name);
                FILE* tf = fopen(test_path, "rb");
                if (tf) {
                    fclose(tf);
                    strncpy(best_id, entry->d_name, sizeof(best_id) - 1);
                    best_id[sizeof(best_id) - 1] = '\0';
                    strncpy(best_folder, entry->d_name, sizeof(best_folder) - 1);
                    best_folder[sizeof(best_folder) - 1] = '\0';
                    found = true;
                    break;
                }
            }
            closedir(exdir);
        }
    }

    if (found) {
        if (title_id_out && sz > 0) {
            strncpy(title_id_out, best_id, sz - 1);
            title_id_out[sz - 1] = '\0';
        }
        if (raw_folder_out && raw_sz > 0) {
            strncpy(raw_folder_out, best_folder, raw_sz - 1);
            raw_folder_out[raw_sz - 1] = '\0';
        }
        return true;
    }
    
    return false;
}

void* process_monitor_thread(void* arg) {
    (void)arg;
    memset(&g_current_game, 0, sizeof(GameInfo));
    
    char detected_id[32] = {0};
    char detected_folder[64] = {0};
    
    while (1) {
        bool running = detect_active_title(detected_id, sizeof(detected_id), detected_folder, sizeof(detected_folder));
        
        // Suppress presence if title is blacklisted by user
        if (running && config_is_blacklisted(detected_id)) {
            running = false;
        }
        
        if (running) {
            // New game launched or game swapped
            if (!g_current_game.is_running || strcmp(g_current_game.title_id, detected_id) != 0) {
                char new_title_name[128] = "";
                char new_icon_url[256] = "";

                printf("[PS5-RPC] Detected title %s (%s). Resolving icon & uploading before showing RPC...\n", 
                       detected_id, detected_folder);

                // 1. Resolve title name and upload/fetch icon FIRST (blocking until done)
                resolve_game_metadata(detected_id, detected_folder,
                                      new_title_name, sizeof(new_title_name),
                                      new_icon_url, sizeof(new_icon_url));

                // 2. Only commit to g_current_game after icon upload is completely done!
                strncpy(g_current_game.title_id, detected_id, sizeof(g_current_game.title_id) - 1);
                g_current_game.title_id[sizeof(g_current_game.title_id) - 1] = '\0';
                
                strncpy(g_current_game.title_name, new_title_name, sizeof(g_current_game.title_name) - 1);
                g_current_game.title_name[sizeof(g_current_game.title_name) - 1] = '\0';

                strncpy(g_current_game.icon_url, new_icon_url, sizeof(g_current_game.icon_url) - 1);
                g_current_game.icon_url[sizeof(g_current_game.icon_url) - 1] = '\0';

                // Timer starts fresh from 0:00 when presence goes live with the finished icon
                g_current_game.start_time = time_fixed();
                g_current_game.is_running = true;

                printf("[PS5-RPC] Active Title Ready: \"%s\" [%s], icon: %s, timer restarted: %lld\n", 
                       g_current_game.title_name, g_current_game.title_id, g_current_game.icon_url, (long long)g_current_game.start_time);

                g_config.needs_update = true;
            }
        } else {
            // No game running: console is on home screen / dashboard
            if (g_current_game.is_running) {
                printf("[PS5-RPC] Title closed. Home screen active (RPC turned off).\n");
                g_current_game.is_running = false;
                memset(g_current_game.title_id, 0, sizeof(g_current_game.title_id));
                memset(g_current_game.title_name, 0, sizeof(g_current_game.title_name));
                memset(g_current_game.icon_url, 0, sizeof(g_current_game.icon_url));
                g_current_game.start_time = 0;
                g_config.needs_update = true;
            }
        }
        
        sleep(1);
    }
    
    return NULL;
}

int get_installed_games(InstalledGameEntry* out_entries, int max_entries) {
    if (!out_entries || max_entries <= 0) return 0;
    int count = 0;

    const char* roots[] = {
        "/user/appmeta",
        "/user/appmeta/external",
        "/system_data/priv/appmeta",
        "/system_data/priv/appmeta/external",
        "/user/app",
        "/data/homebrew",
        "/data/games",
        NULL
    };

    for (int r = 0; roots[r] && count < max_entries; r++) {
        DIR* dir = opendir(roots[r]);
        if (!dir) continue;
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL && count < max_entries) {
            if (entry->d_name[0] == '.') continue;
            if (strcmp(entry->d_name, "pfsc") == 0) continue;

            char candidate_id[32] = {0};

            if (is_valid_title_id(entry->d_name)) {
                strncpy(candidate_id, entry->d_name, sizeof(candidate_id) - 1);
            } else {
                // Check if directory name is a Content ID containing PPSA or CUSA
                const char* ppsa = strstr(entry->d_name, "PPSA");
                const char* cusa = strstr(entry->d_name, "CUSA");
                const char* pos = ppsa ? ppsa : cusa;
                if (pos && strlen(pos) >= 9) {
                    char temp_id[16] = {0};
                    strncpy(temp_id, pos, 9);
                    temp_id[9] = '\0';
                    if (is_valid_title_id(temp_id)) {
                        strncpy(candidate_id, temp_id, sizeof(candidate_id) - 1);
                    }
                }
            }

            if (!candidate_id[0]) continue;

            // Check duplicate
            bool duplicate = false;
            for (int i = 0; i < count; i++) {
                if (strcasecmp(out_entries[i].title_id, candidate_id) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            strncpy(out_entries[count].title_id, candidate_id, sizeof(out_entries[count].title_id) - 1);
            out_entries[count].title_id[sizeof(out_entries[count].title_id) - 1] = '\0';

            // Platform
            if (strncmp(candidate_id, "PPSA", 4) == 0) {
                strcpy(out_entries[count].platform, "PS5");
            } else if (strncmp(candidate_id, "CUSA", 4) == 0) {
                strcpy(out_entries[count].platform, "PS4");
            } else {
                strcpy(out_entries[count].platform, "PS5");
            }

            // Title name
            char tname[128] = {0};
            find_readable_metadata(candidate_id, entry->d_name, tname, sizeof(tname));
            if (!tname[0]) {
                strncpy(tname, candidate_id, sizeof(tname) - 1);
            }
            strncpy(out_entries[count].title_name, tname, sizeof(out_entries[count].title_name) - 1);
            out_entries[count].title_name[sizeof(out_entries[count].title_name) - 1] = '\0';

            snprintf(out_entries[count].icon_url, sizeof(out_entries[count].icon_url), "/api/icon?id=%s", candidate_id);
            out_entries[count].is_blacklisted = config_is_blacklisted(candidate_id);

            count++;
        }
        closedir(dir);
    }

    // Prepend or add current running game if active
    if (g_current_game.title_id[0] && is_valid_title_id(g_current_game.title_id) && count < max_entries) {
        bool duplicate = false;
        for (int i = 0; i < count; i++) {
            if (strcasecmp(out_entries[i].title_id, g_current_game.title_id) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            strncpy(out_entries[count].title_id, g_current_game.title_id, sizeof(out_entries[count].title_id) - 1);
            strncpy(out_entries[count].title_name, g_current_game.title_name[0] ? g_current_game.title_name : g_current_game.title_id, sizeof(out_entries[count].title_name) - 1);
            if (strncmp(g_current_game.title_id, "PPSA", 4) == 0) {
                strcpy(out_entries[count].platform, "PS5");
            } else if (strncmp(g_current_game.title_id, "CUSA", 4) == 0) {
                strcpy(out_entries[count].platform, "PS4");
            } else {
                strcpy(out_entries[count].platform, "PS5");
            }
            snprintf(out_entries[count].icon_url, sizeof(out_entries[count].icon_url), "/api/icon?id=%s", g_current_game.title_id);
            out_entries[count].is_blacklisted = config_is_blacklisted(g_current_game.title_id);
            count++;
        }
    }

    return count;
}
