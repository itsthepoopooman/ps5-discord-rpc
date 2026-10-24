#ifndef DISCORD_VOICE_H
#define DISCORD_VOICE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char guild_id[32];
    char channel_id[32];
    char guild_name[64];
    char channel_name[64];
    bool connected;
    bool self_mute;
    bool self_deaf;
    bool needs_update;
} discord_voice_state_t;

extern discord_voice_state_t g_voice_state;

void discord_voice_init(void);
void discord_voice_join(const char* guild_id, const char* channel_id, const char* guild_name, const char* channel_name);
void discord_voice_disconnect(void);
void discord_voice_toggle_mute(void);
void discord_voice_toggle_deaf(void);

/* REST API helpers */
int discord_fetch_guilds(const char* token, char* out_buf, size_t out_buf_sz);
int discord_fetch_channels(const char* guild_id, const char* token, char* out_buf, size_t out_buf_sz);

#endif
