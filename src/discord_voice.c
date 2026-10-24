#include "discord_voice.h"
#include "tls.h"
#include "notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define DISCORD_HOST "discord.com"

discord_voice_state_t g_voice_state = {0};

void discord_voice_init(void) {
    memset(&g_voice_state, 0, sizeof(g_voice_state));
}

void discord_voice_join(const char* guild_id, const char* channel_id, const char* guild_name, const char* channel_name) {
    if (guild_id) strncpy(g_voice_state.guild_id, guild_id, sizeof(g_voice_state.guild_id) - 1);
    if (channel_id) strncpy(g_voice_state.channel_id, channel_id, sizeof(g_voice_state.channel_id) - 1);
    if (guild_name) strncpy(g_voice_state.guild_name, guild_name, sizeof(g_voice_state.guild_name) - 1);
    if (channel_name) strncpy(g_voice_state.channel_name, channel_name, sizeof(g_voice_state.channel_name) - 1);
    g_voice_state.connected = true;
    g_voice_state.needs_update = true;

    char msg[128];
    snprintf(msg, sizeof(msg), "Connected to #%s", g_voice_state.channel_name[0] ? g_voice_state.channel_name : "VC");
    notify_custom("Discord Voice", msg, NULL);
    printf("[Voice] Joined voice channel: #%s (%s) in %s (%s)\n",
           g_voice_state.channel_name, g_voice_state.channel_id,
           g_voice_state.guild_name, g_voice_state.guild_id);
}

void discord_voice_disconnect(void) {
    g_voice_state.connected = false;
    g_voice_state.guild_id[0] = '\0';
    g_voice_state.channel_id[0] = '\0';
    g_voice_state.guild_name[0] = '\0';
    g_voice_state.channel_name[0] = '\0';
    g_voice_state.needs_update = true;

    notify_custom("Discord Voice", "Disconnected from Voice Chat", NULL);
    printf("[Voice] Disconnected from Voice Chat\n");
}

void discord_voice_toggle_mute(void) {
    g_voice_state.self_mute = !g_voice_state.self_mute;
    g_voice_state.needs_update = true;

    char msg[64];
    snprintf(msg, sizeof(msg), "Microphone: %s", g_voice_state.self_mute ? "Muted" : "Unmuted");
    notify_custom("Discord Voice", msg, NULL);
    printf("[Voice] Toggle Mute: %s\n", g_voice_state.self_mute ? "MUTED" : "UNMUTED");
}

void discord_voice_toggle_deaf(void) {
    g_voice_state.self_deaf = !g_voice_state.self_deaf;
    g_voice_state.needs_update = true;

    char msg[64];
    snprintf(msg, sizeof(msg), "Headset: %s", g_voice_state.self_deaf ? "Deafened" : "Undeafened");
    notify_custom("Discord Voice", msg, NULL);
    printf("[Voice] Toggle Deafen: %s\n", g_voice_state.self_deaf ? "DEAFENED" : "UNDEAFENED");
}

static int discord_https_get(const char* path, const char* token, char* out_resp, size_t out_resp_sz) {
    if (!path || !token || !out_resp || out_resp_sz == 0) return -1;
    out_resp[0] = '\0';

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(DISCORD_HOST, "443", &hints, &res) != 0 || !res) {
        printf("[Discord-Voice] Failed to resolve %s\n", DISCORD_HOST);
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = 8;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        printf("[Discord-Voice] Failed to connect to %s:443\n", DISCORD_HOST);
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    tls_ctx_t* tls = tls_start(fd, DISCORD_HOST);
    if (!tls) {
        printf("[Discord-Voice] TLS handshake failed for %s\n", DISCORD_HOST);
        close(fd);
        return -1;
    }

    char req[1024];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: %s\r\n"
        "User-Agent: Mozilla/5.0 (PlayStation 5) AppleWebKit/605.1.15 Discord/1.0\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n\r\n",
        path, DISCORD_HOST, token);

    if (tls_write(tls, req, (size_t)req_len) < 0) {
        printf("[Discord-Voice] Failed to write GET request\n");
        tls_free(tls);
        close(fd);
        return -1;
    }

    // Read full HTTP response
    size_t total = 0;
    char raw_buf[65536];
    while (total < sizeof(raw_buf) - 1) {
        int r = tls_read(tls, raw_buf + total, sizeof(raw_buf) - 1 - total);
        if (r <= 0) break;
        total += (size_t)r;
    }
    raw_buf[total] = '\0';

    tls_free(tls);
    close(fd);

    char* body = strstr(raw_buf, "\r\n\r\n");
    if (!body) return -1;
    body += 4;

    strncpy(out_resp, body, out_resp_sz - 1);
    out_resp[out_resp_sz - 1] = '\0';
    return 0;
}

int discord_fetch_guilds(const char* token, char* out_buf, size_t out_buf_sz) {
    return discord_https_get("/api/v9/users/@me/guilds", token, out_buf, out_buf_sz);
}

int discord_fetch_channels(const char* guild_id, const char* token, char* out_buf, size_t out_buf_sz) {
    char path[128];
    snprintf(path, sizeof(path), "/api/v9/guilds/%s/channels", guild_id);
    return discord_https_get(path, token, out_buf, out_buf_sz);
}
