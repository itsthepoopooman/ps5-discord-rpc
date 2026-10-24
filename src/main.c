#include "config.h"
#include "discord.h"
#include "title_resolver.h"
#include "timesync.h"
#include "webserver.h"
#include "notify.h"
#include "discord_voice.h"
#include "kabuki_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>

static bool is_payload_already_running(void) {
    // 1. Try connecting to loopback port 6767
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock >= 0) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(6767);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250000; // 250ms timeout
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

        int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        close(sock);
        if (res == 0) {
            return true;
        }
    }

    // 2. Try connecting to LAN IP on port 6767
    char local_ip[64] = "";
    get_local_ip(local_ip, sizeof(local_ip));
    if (local_ip[0]) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0) {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(6767);
            addr.sin_addr.s_addr = inet_addr(local_ip);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 250000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

            int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
            close(sock);
            if (res == 0) {
                return true;
            }
        }
    }

    // 3. Check if port 6767 is already bound by another process
    int test_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (test_fd >= 0) {
        int opt = 1;
        setsockopt(test_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(6767);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(test_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(test_fd);
            return true;
        }
        close(test_fd);
    }

    return false;
}

#include "embedded_icon.h"

int main(void) {
    // Prevent duplicate instances from running
    if (is_payload_already_running()) {
        printf("[PS5-RPC] PS5 Discord RPC is already running! Exiting duplicate instance.\n");
        char local_ip[64] = "";
        get_local_ip(local_ip, sizeof(local_ip));
        char sub_msg[128];
        if (local_ip[0]) {
            snprintf(sub_msg, sizeof(sub_msg), "Already running!\nWeb: http://%s:6767", local_ip);
        } else {
            snprintf(sub_msg, sizeof(sub_msg), "Already running!\nWeb Control: Port 6767");
        }
        notify_custom("Discord RPC", sub_msg, NULL);
        return 0;
    }

    printf("[PS5-RPC] Starting PS5 Discord Rich Presence Daemon...\n");
    printf("========================================\n");
    printf("  Ported & Modernized for Prospero Clang\n");
    printf("  Web Dashboard: Port 6767\n");
    printf("  Kabuki Overlay: 127.0.0.1:38590\n");
    printf("========================================\n");

    // 0. Ensure embedded Discord notification icon is written to disk
    write_embedded_icon();

    // 1. Initialize voice state
    discord_voice_init();

    // 2. Load or initialize configuration
    if (!config_load(&g_config)) {
        printf("[PS5-RPC] No existing config found. Setting defaults.\n");
        config_set_defaults(&g_config);
        config_save(&g_config);
    } else {
        printf("[PS5-RPC] Loaded configuration from %s\n", g_active_config_path);
    }

    // 3. Synchronize true UTC wall clock for Discord timestamps
    printf("[PS5-RPC] Synchronizing network time for Discord timestamps...\n");
    time_sync_all();

    // 4. Alert the user on the TV/Monitor with the exact IP and overlay instructions
    char local_ip[64] = "";
    get_local_ip(local_ip, sizeof(local_ip));
    char subject_msg[128];
    if (local_ip[0]) {
        snprintf(subject_msg, sizeof(subject_msg), "Web: %s:6767 | In-Game: L3+R3", local_ip);
    } else {
        snprintf(subject_msg, sizeof(subject_msg), "Web: Port 6767 | In-Game: L3+R3");
    }
    notify_custom("Discord RPC & Voice Active", subject_msg, NULL);

    // 5. Launch background threads with 1MB stack to guarantee stability
    pthread_t t_web, t_monitor, t_discord, t_kabuki;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024);

    if (pthread_create(&t_web, &attr, webserver_thread, NULL) != 0) {
        fprintf(stderr, "[PS5-RPC] Failed to start Web Server thread!\n");
    }

    if (pthread_create(&t_monitor, &attr, process_monitor_thread, NULL) != 0) {
        fprintf(stderr, "[PS5-RPC] Failed to start Process Monitor thread!\n");
    }

    if (pthread_create(&t_discord, &attr, discord_gateway_thread, NULL) != 0) {
        fprintf(stderr, "[PS5-RPC] Failed to start Discord Gateway thread!\n");
    }

    if (pthread_create(&t_kabuki, &attr, kabuki_overlay_thread, NULL) != 0) {
        fprintf(stderr, "[PS5-RPC] Failed to start Kabuki Overlay thread!\n");
    }

    pthread_attr_destroy(&attr);

    // 6. Join threads to keep daemon active
    pthread_join(t_web, NULL);
    pthread_join(t_monitor, NULL);
    pthread_join(t_discord, NULL);
    pthread_join(t_kabuki, NULL);

    return 0;
}
