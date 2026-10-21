#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define CONFIG_PATH           "/data/discord_rpc/config.json"
#define CONFIG_DIR            "/data/discord_rpc"
#define CONFIG_PATH_FALLBACK  "/user/home/discord_rpc/config.json"
#define CONFIG_DIR_FALLBACK   "/user/home/discord_rpc"

#define MAX_BLACKLIST 128

typedef struct {
    char token[128];
    char details[128];
    char state[128];
    
    bool btn1_enabled;
    char btn1_label[64];
    char btn1_url[256];
    
    bool btn2_enabled;
    char btn2_label[64];
    char btn2_url[256];
    
    bool show_badge;
    bool show_timer;
    bool show_idle;
    
    int blacklist_count;
    char blacklist[MAX_BLACKLIST][32];

    // Internal synchronization flag
    bool needs_update;
} RpcConfig;

extern RpcConfig g_config;
extern char g_active_config_path[256];

void config_set_defaults(RpcConfig* cfg);
bool config_parse_json(RpcConfig* cfg, const char* json);
bool config_load(RpcConfig* cfg);
bool config_save(const RpcConfig* cfg);

bool config_build_json(const RpcConfig* cfg, char* out, size_t out_sz);
bool config_is_blacklisted(const char* title_id);
bool config_set_blacklist(const char* title_id, bool blacklisted);

#endif
