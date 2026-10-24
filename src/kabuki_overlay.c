#include "kabuki_overlay.h"
#include "discord_voice.h"
#include "title_resolver.h"
#include "notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define KABUKI_PORT 38590
#define KABUKI_HOST "127.0.0.1"
#define TILE_ID "DSCORD"

static int connect_kabuki(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(KABUKI_PORT);
    addr.sin_addr.s_addr = inet_addr(KABUKI_HOST);

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(sock);
        return -1;
    }
    return sock;
}

static void send_kabuki_content(int sock) {
    char json[4096];
    char vc_sub[128];
    if (g_voice_state.connected) {
        snprintf(vc_sub, sizeof(vc_sub), "Connected: #%s (%s)",
                 g_voice_state.channel_name[0] ? g_voice_state.channel_name : "Voice",
                 g_voice_state.guild_name[0] ? g_voice_state.guild_name : "Server");
    } else {
        snprintf(vc_sub, sizeof(vc_sub), "Disconnected (Join via Web Dashboard)");
    }

    char mute_sub[64];
    snprintf(mute_sub, sizeof(mute_sub), "%s [Click to Toggle]", g_voice_state.self_mute ? "Muted" : "Unmuted");

    char deaf_sub[64];
    snprintf(deaf_sub, sizeof(deaf_sub), "%s [Click to Toggle]", g_voice_state.self_deaf ? "Deafened" : "Undeafened");

    char game_sub[128];
    if (g_current_game.is_running && g_current_game.title_name[0]) {
        snprintf(game_sub, sizeof(game_sub), "Playing: %s (%s)", g_current_game.title_name, g_current_game.title_id);
    } else {
        snprintf(game_sub, sizeof(game_sub), "PS5 Home Menu / Idle");
    }

    snprintf(json, sizeof(json),
        "{\"v\":1,\"content\":{\"tile\":\"%s\",\"data\":{"
        "\"top\":{"
        "\"title\":\"Discord Control\","
        "\"heading\":\"Voice & Rich Presence\","
        "\"rows\":["
        "{\"key\":\"vc_status\",\"title\":\"Voice Chat\",\"subtitle\":\"%s\",\"opens\":false},"
        "{\"key\":\"toggle_mute\",\"title\":\"Microphone Mute\",\"subtitle\":\"%s\",\"opens\":false},"
        "{\"key\":\"toggle_deaf\",\"title\":\"Audio Deafen\",\"subtitle\":\"%s\",\"opens\":false},"
        "{\"key\":\"disconnect_vc\",\"title\":\"Disconnect Voice\",\"subtitle\":\"Leave current Voice Channel\",\"opens\":false},"
        "{\"key\":\"game_info\",\"title\":\"Rich Presence\",\"subtitle\":\"%s\",\"opens\":false}"
        "]}"
        "}}}\n",
        TILE_ID, vc_sub, mute_sub, deaf_sub, game_sub
    );

    write(sock, json, strlen(json));
}

#include <errno.h>

void* kabuki_overlay_thread(void* arg) {
    (void)arg;
    printf("[Kabuki-Overlay] Overlay provider thread running...\n");

    bool showed_startup_tip = false;
    bool showed_connected_notify = false;

    while (1) {
        int sock = connect_kabuki();
        if (sock < 0) {
            if (!showed_startup_tip) {
                notify_custom("Discord In-Game Overlay", "Overlay available: Start Kabuki (L3 + R3)", NULL);
                showed_startup_tip = true;
            }
            sleep(2);
            continue;
        }

        // 1. Send Kabuki hello handshake
        const char hello_msg[] =
            "{\"v\":1,\"hello\":{"
            "\"provider\":\"discord\","
            "\"name\":\"Discord\","
            "\"version\":\"2.0.0\","
            "\"tiles\":["
            "{\"id\":\"" TILE_ID "\",\"title\":\"Discord\",\"card\":\"list\",\"priority\":200}"
            "]}}\n";

        if (write(sock, hello_msg, strlen(hello_msg)) < 0) {
            close(sock);
            sleep(2);
            continue;
        }

        if (!showed_connected_notify) {
            notify_custom("Discord Overlay Ready", "In-game overlay active! (Hold L3 + R3)", NULL);
            showed_connected_notify = true;
        }

        printf("[Kabuki-Overlay] Connected to Kabuki daemon on port %d! Registered tile %s.\n", KABUKI_PORT, TILE_ID);

        // Send initial content
        send_kabuki_content(sock);

        char in_buf[4096];
        int last_game_running = -1;
        bool last_voice_connected = false;
        bool last_mute = false;
        bool last_deaf = false;
        time_t last_keepalive = time(NULL);

        while (1) {
            // Check if state changed, update Kabuki card
            bool voice_changed = (g_voice_state.connected != last_voice_connected) ||
                                 (g_voice_state.self_mute != last_mute) ||
                                 (g_voice_state.self_deaf != last_deaf);
            bool game_changed = (g_current_game.is_running != last_game_running);
            time_t now = time(NULL);

            if (voice_changed || game_changed || (now - last_keepalive >= 5)) {
                last_voice_connected = g_voice_state.connected;
                last_mute = g_voice_state.self_mute;
                last_deaf = g_voice_state.self_deaf;
                last_game_running = g_current_game.is_running;
                last_keepalive = now;
                send_kabuki_content(sock);
            }

            // Read events from Kabuki
            ssize_t n = read(sock, in_buf, sizeof(in_buf) - 1);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    // Socket read timeout: normal non-blocking loop
                    continue;
                }
                printf("[Kabuki-Overlay] Socket error (%d), disconnected.\n", errno);
                close(sock);
                break;
            } else if (n == 0) {
                printf("[Kabuki-Overlay] Disconnected by Kabuki daemon.\n");
                close(sock);
                break;
            }

            in_buf[n] = '\0';

            // Check actions
            if (strstr(in_buf, "\"action\":\"select\"")) {
                if (strstr(in_buf, "\"key\":\"toggle_mute\"")) {
                    discord_voice_toggle_mute();
                    send_kabuki_content(sock);
                } else if (strstr(in_buf, "\"key\":\"toggle_deaf\"")) {
                    discord_voice_toggle_deaf();
                    send_kabuki_content(sock);
                } else if (strstr(in_buf, "\"key\":\"disconnect_vc\"")) {
                    discord_voice_disconnect();
                    send_kabuki_content(sock);
                }
            }
        }

        sleep(2);
    }

    return NULL;
}
