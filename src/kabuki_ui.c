#include "kabuki.h"
#include "kabuki_ui.h"
#include "config.h"
#include "title_resolver.h"
#include "notify.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

DiscordVoiceState g_voice_state = {
    .connected = false,
    .self_mute = false,
    .self_deaf = false,
    .guild_name = "",
    .channel_name = "",
    .guild_id = "",
    .channel_id = "",
    .participant_count = 0,
    .participants = {{0}}
};

static kabuki_client s_client;
static pthread_mutex_t s_kabuki_lock = PTHREAD_MUTEX_INITIALIZER;
static bool s_is_connected = false;
static bool s_in_channels_sub = false;
static bool s_needs_push = false;

static void build_tile_json(char* out, size_t cap) {
    char local_ip[64] = "";
    get_local_ip(local_ip, sizeof(local_ip));

    char vc_status_title[64];
    char vc_status_sub[96];
    if (g_voice_state.connected && g_voice_state.channel_name[0]) {
        snprintf(vc_status_title, sizeof(vc_status_title), "Voice: #%s", g_voice_state.channel_name);
        snprintf(vc_status_sub, sizeof(vc_status_sub), "Server: %s (Connected)", g_voice_state.guild_name[0] ? g_voice_state.guild_name : "Direct");
    } else {
        snprintf(vc_status_title, sizeof(vc_status_title), "Voice: Not Connected");
        snprintf(vc_status_sub, sizeof(vc_status_sub), "Select 'Voice Channels' below to join");
    }

    const char* mute_title = g_voice_state.self_mute ? "[Mic] Muted: YES" : "[Mic] Muted: NO";
    const char* deaf_title = g_voice_state.self_deaf ? "[Audio] Deafened: YES" : "[Audio] Deafened: NO";

    char game_title[128];
    char game_sub[96];
    if (g_current_game.is_running && g_current_game.title_name[0]) {
        snprintf(game_title, sizeof(game_title), "Playing: %s", g_current_game.title_name);
        snprintf(game_sub, sizeof(game_sub), "ID: %s (Active RPC)", g_current_game.title_id);
    } else {
        snprintf(game_title, sizeof(game_title), "Playing: None (Home Menu)");
        snprintf(game_sub, sizeof(game_sub), g_config.show_idle ? "Status: Displaying Idle" : "Status: Hidden");
    }

    char web_sub[96];
    if (local_ip[0]) {
        snprintf(web_sub, sizeof(web_sub), "Web Dashboard: http://%s:6767", local_ip);
    } else {
        snprintf(web_sub, sizeof(web_sub), "Web Dashboard: Port 6767");
    }

    if (s_in_channels_sub) {
        // Render sub-level with voice channels
        snprintf(out, cap,
            "{\"top\":{\"title\":\"Discord\",\"covers\":false,\"rows\":["
                "{\"key\":\"vc_status\",\"title\":\"%s\",\"subtitle\":\"%s\"},"
                "{\"key\":\"mute\",\"title\":\"%s\",\"subtitle\":\"Press Cross to toggle microphone\"},"
                "{\"key\":\"deaf\",\"title\":\"%s\",\"subtitle\":\"Press Cross to toggle deafen\"},"
                "%s"
                "{\"key\":\"channels_menu\",\"title\":\"Voice Channels\",\"subtitle\":\"Browse voice channels\",\"opens\":true},"
                "{\"key\":\"rpc_game\",\"title\":\"%s\",\"subtitle\":\"%s\"},"
                "{\"key\":\"web_info\",\"title\":\"Dashboard\",\"subtitle\":\"%s\"}"
            "]},"
            "\"sub_key\":\"channels_menu\","
            "\"sub\":{\"title\":\"Voice Channels\",\"covers\":false,\"rows\":["
                "{\"key\":\"vc_join_general\",\"title\":\"#general\",\"subtitle\":\"Press Cross to connect\"},"
                "{\"key\":\"vc_join_lounge\",\"title\":\"#lounge\",\"subtitle\":\"Press Cross to connect\"},"
                "{\"key\":\"vc_join_gaming\",\"title\":\"#gaming\",\"subtitle\":\"Press Cross to connect\"}"
            "]}}",
            vc_status_title, vc_status_sub,
            mute_title,
            deaf_title,
            g_voice_state.connected ? "{\"key\":\"disconnect\",\"title\":\"Disconnect from Voice\",\"subtitle\":\"Leave current channel\"}," : "",
            game_title, game_sub,
            web_sub
        );
    } else {
        // Standard top-level view
        snprintf(out, cap,
            "{\"top\":{\"title\":\"Discord\",\"covers\":false,\"rows\":["
                "{\"key\":\"vc_status\",\"title\":\"%s\",\"subtitle\":\"%s\"},"
                "{\"key\":\"mute\",\"title\":\"%s\",\"subtitle\":\"Press Cross to toggle microphone\"},"
                "{\"key\":\"deaf\",\"title\":\"%s\",\"subtitle\":\"Press Cross to toggle deafen\"},"
                "%s"
                "{\"key\":\"channels_menu\",\"title\":\"Voice Channels\",\"subtitle\":\"Browse voice channels\",\"opens\":true},"
                "{\"key\":\"rpc_game\",\"title\":\"%s\",\"subtitle\":\"%s\"},"
                "{\"key\":\"web_info\",\"title\":\"Dashboard\",\"subtitle\":\"%s\"}"
            "]},\"sub_key\":null}",
            vc_status_title, vc_status_sub,
            mute_title,
            deaf_title,
            g_voice_state.connected ? "{\"key\":\"disconnect\",\"title\":\"Disconnect from Voice\",\"subtitle\":\"Leave current channel\"}," : "",
            game_title, game_sub,
            web_sub
        );
    }
}

static void send_content_locked(void) {
    if (!s_is_connected || s_client.fd < 0) return;
    char buffer[4096];
    build_tile_json(buffer, sizeof(buffer));
    kabuki_content(&s_client, "DSCORD", buffer);
}

void kabuki_ui_notify_state_change(void) {
    pthread_mutex_lock(&s_kabuki_lock);
    s_needs_push = true;
    pthread_mutex_unlock(&s_kabuki_lock);
}

void kabuki_ui_set_voice_state(bool connected, const char* guild, const char* channel, const char* guild_id, const char* channel_id) {
    pthread_mutex_lock(&s_kabuki_lock);
    g_voice_state.connected = connected;
    if (guild) snprintf(g_voice_state.guild_name, sizeof(g_voice_state.guild_name), "%s", guild);
    if (channel) snprintf(g_voice_state.channel_name, sizeof(g_voice_state.channel_name), "%s", channel);
    if (guild_id) snprintf(g_voice_state.guild_id, sizeof(g_voice_state.guild_id), "%s", guild_id);
    if (channel_id) snprintf(g_voice_state.channel_id, sizeof(g_voice_state.channel_id), "%s", channel_id);
    send_content_locked();
    pthread_mutex_unlock(&s_kabuki_lock);
}

void* kabuki_thread(void* arg) {
    (void)arg;
    static const kabuki_tile s_tile = {
        .id = "DSCORD",
        .title = "Discord",
        .icon = "icon:iconid_party",
        .card = "list",
        .priority = 350
    };

    bool warned_not_running = false;
    time_t last_sync = 0;

    printf("[Kabuki] Starting Kabuki overlay client thread...\n");

    while (1) {
        pthread_mutex_lock(&s_kabuki_lock);
        s_is_connected = false;
        kabuki_close(&s_client);
        pthread_mutex_unlock(&s_kabuki_lock);

        // 1. Attempt connection to Kabuki on loopback port 38590
        kabuki_client test_client;
        if (kabuki_connect(&test_client) != 0) {
            if (!warned_not_running) {
                printf("[Kabuki] Kabuki overlay is not running. Prompting user to launch Kabuki.\n");
                notify_custom("Kabuki Overlay", "Launch Kabuki to use in-game Discord controls (L3+R3)", NULL);
                warned_not_running = true;
            }
            sleep(2);
            continue;
        }

        // 2. Connected to Kabuki daemon!
        pthread_mutex_lock(&s_kabuki_lock);
        s_client = test_client;
        if (kabuki_hello(&s_client, "discord-rpc", &s_tile, 1, NULL, 3000) != 0) {
            printf("[Kabuki] Failed handshake with Kabuki daemon.\n");
            kabuki_close(&s_client);
            pthread_mutex_unlock(&s_kabuki_lock);
            sleep(2);
            continue;
        }

        s_is_connected = true;
        s_in_channels_sub = false;
        printf("[Kabuki] Handshake successful! Registered tile 'DSCORD'.\n");
        notify_custom("Discord UI Ready", "Kabuki overlay connected! Press L3+R3 in-game", NULL);
        warned_not_running = false;

        kabuki_status(&s_client, "Discord RPC Active");
        send_content_locked();
        pthread_mutex_unlock(&s_kabuki_lock);

        // 3. Event and poll loop
        while (1) {
            kabuki_event ev;
            int got = kabuki_poll(&s_client, &ev, 1000);

            if (got < 0) {
                printf("[Kabuki] Connection lost with Kabuki daemon. Will attempt reconnection.\n");
                break;
            }

            pthread_mutex_lock(&s_kabuki_lock);

            if (got == 1) {
                if (ev.kind == KABUKI_ACTION) {
                    if (!strcmp(ev.action, "select")) {
                        if (!strcmp(ev.key, "mute")) {
                            g_voice_state.self_mute = !g_voice_state.self_mute;
                            printf("[Kabuki] Toggled microphone mute: %s\n", g_voice_state.self_mute ? "Muted" : "Unmuted");
                            send_content_locked();
                        } else if (!strcmp(ev.key, "deaf")) {
                            g_voice_state.self_deaf = !g_voice_state.self_deaf;
                            printf("[Kabuki] Toggled audio deafen: %s\n", g_voice_state.self_deaf ? "Deafened" : "Undeafened");
                            send_content_locked();
                        } else if (!strcmp(ev.key, "disconnect")) {
                            g_voice_state.connected = false;
                            g_voice_state.channel_name[0] = '\0';
                            g_voice_state.guild_name[0] = '\0';
                            printf("[Kabuki] User requested disconnect from VC.\n");
                            send_content_locked();
                        } else if (strncmp(ev.key, "vc_join_", 8) == 0) {
                            const char* ch_name = ev.key + 8;
                            g_voice_state.connected = true;
                            snprintf(g_voice_state.channel_name, sizeof(g_voice_state.channel_name), "%s", ch_name);
                            snprintf(g_voice_state.guild_name, sizeof(g_voice_state.guild_name), "Community");
                            s_in_channels_sub = false;
                            printf("[Kabuki] Connected to voice channel #%s\n", ch_name);
                            notify_custom("Discord Voice", "Connected to voice channel!", NULL);
                            send_content_locked();
                        }
                    } else if (!strcmp(ev.action, "open")) {
                        if (!strcmp(ev.key, "channels_menu")) {
                            s_in_channels_sub = true;
                            send_content_locked();
                        }
                    } else if (!strcmp(ev.action, "close")) {
                        s_in_channels_sub = false;
                        send_content_locked();
                    }
                } else if (ev.kind == KABUKI_SHOWN) {
                    // Update content dynamically when user opens Kabuki overlay card
                    send_content_locked();
                }
            }

            // Periodic sync check (every 3 seconds or on explicit notify)
            time_t now = time(NULL);
            if (s_needs_push || (now - last_sync >= 3)) {
                send_content_locked();
                s_needs_push = false;
                last_sync = now;
            }

            pthread_mutex_unlock(&s_kabuki_lock);
        }
    }

    return NULL;
}
