#ifndef KABUKI_UI_H
#define KABUKI_UI_H

#include <stdbool.h>

typedef struct {
    bool connected;
    bool self_mute;
    bool self_deaf;
    char guild_name[64];
    char channel_name[64];
    char guild_id[32];
    char channel_id[32];
    int participant_count;
    char participants[16][32];
} DiscordVoiceState;

extern DiscordVoiceState g_voice_state;

void* kabuki_thread(void* arg);
void kabuki_ui_notify_state_change(void);
void kabuki_ui_set_voice_state(bool connected, const char* guild, const char* channel, const char* guild_id, const char* channel_id);

#endif
