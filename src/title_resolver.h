#ifndef TITLE_RESOLVER_H
#define TITLE_RESOLVER_H

#include <stdbool.h>
#include <time.h>

typedef struct {
    char title_id[32];
    char title_name[128];
    char icon_url[512];
    time_t start_time;
    bool is_running;
} GameInfo;

typedef struct {
    char title_id[32];
    char title_name[128];
    char platform[8];
    char icon_url[256];
    bool is_blacklisted;
} InstalledGameEntry;

extern GameInfo g_current_game;

void* process_monitor_thread(void* arg);
bool resolve_game_metadata(const char* title_id, const char* raw_folder, char* title_name_out, size_t name_sz, char* icon_url_out, size_t icon_sz);
bool find_readable_icon(const char* title_id, const char* raw_folder, char* out_file, size_t out_cap);
bool find_readable_metadata(const char* title_id, const char* raw_folder, char* out_name, size_t out_sz);
int get_installed_games(InstalledGameEntry* out_entries, int max_entries);

#endif
