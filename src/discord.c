#include "discord.h"
#include "config.h"
#include "title_resolver.h"
#include "ws.h"
#include "tls.h"
#include "clock.h"
#include "b64.h"
#include "log.h"
#include "notify.h"
#include "art.h"
#include "jsonlite.h"
#include "detect.h"
#include "timesync.h"
#include "catbox.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define GW_HOST "gateway.discord.gg"
#define GW_PORT 443
#define GW_PATH "/?v=10&encoding=json"
#define DEFAULT_APP_ID "1536977374795538532"
#define DEFAULT_LOGO_URL "https://raw.githubusercontent.com/SirHumza/orbisRPC/refs/heads/main/config/images/icons/ps-logo-full.png"
#define DEFAULT_BADGE_URL "https://raw.githubusercontent.com/SirHumza/orbisRPC/refs/heads/main/config/images/icons/ps-logo-small.png"

typedef struct {
    ws_t ws;
    char token[512];
    int64_t last_heartbeat;
    int64_t last_ack;
    int64_t hb_interval_ms;
    int seq;
    int connected;
    int sent_hb;
} discord_session_t;

static void make_key(char *out) {
    unsigned char b[16];
    memset(b, 0, sizeof(b));
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        size_t got = 0;
        while (got < sizeof(b)) {
            long r = read(fd, (char *)b + got, sizeof(b) - got);
            if (r <= 0) break;
            got += (size_t)r;
        }
        close(fd);
        if (got < sizeof(b)) {
            uint32_t t = (uint32_t)time(NULL);
            for (size_t i = got; i < sizeof(b); i++, t = t * 1664525u + 1013904223u)
                b[i] ^= (unsigned char)(t >> 24);
        }
    } else {
        for (int i = 0; i < 16; i++) b[i] = (unsigned char)(time(NULL) + i * 7);
    }
    b64_encode(b, 16, out);
}

static const char *top_val(const char *json, size_t len, const char *key) {
    size_t kl = strlen(key);
    int depth = 0, instr = 0, esc = 0;
    for (size_t i = 0; i < len; i++) {
        char c = json[i];
        if (instr) {
            if (esc) esc = 0;
            else if (c == '\\') esc = 1;
            else if (c == '"') instr = 0;
            continue;
        }
        if (c == '"') {
            size_t j = i + 1, k = 0;
            while (j < len && k < kl && json[j] == key[k]) { j++; k++; }
            if (k == kl && j < len && json[j] == '"') {
                j++;
                while (j < len && (json[j] == ' ' || json[j] == '\t')) j++;
                if (j < len && json[j] == ':') {
                    if (depth == 1) return json + j + 1;
                    i = j; continue;
                }
            }
            instr = 1; continue;
        }
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') depth--;
    }
    return NULL;
}

static int gw_op(const char *json, size_t len) {
    const char *v = top_val(json, len, "op");
    if (!v) return -1;
    while (v < json + len && (*v == ' ' || *v == '\t')) v++;
    if (v >= json + len || *v < '0' || *v > '9') return -1;
    int op = 0;
    while (v < json + len && *v >= '0' && *v <= '9') { op = op * 10 + (*v - '0'); v++; }
    return op;
}

static void gw_seq(discord_session_t *d, const char *json, size_t len) {
    if (!d) return;
    const char *v = top_val(json, len, "s");
    if (!v) return;
    while (v < json + len && (*v == ' ' || *v == '\t')) v++;
    if (v >= json + len || *v < '0' || *v > '9') return;
    int s = 0;
    while (v < json + len && *v >= '0' && *v <= '9') { s = s * 10 + (*v - '0'); v++; }
    d->seq = s;
}

static int is_ready(const char *json, size_t len) {
    const char *v = top_val(json, len, "t");
    if (!v) return 0;
    while (v < json + len && (*v == ' ' || *v == '\t')) v++;
    return (size_t)(json + len - v) >= 7 && !memcmp(v, "\"READY\"", 7);
}

static int rx_frame(discord_session_t *d, char *buf, size_t cap, int *op, int *fin, int64_t deadline) {
    for (;;) {
        int nr = ws_recv_frame(&d->ws, buf, cap, op, fin);
        if (nr == -3) continue;
        if (nr != 0) return nr;
        if (orbis_mono_s() > deadline) return -4;
        usleep(50000);
    }
}

static int send_identify(discord_session_t *d, const char *token) {
    jl_val_t *root = jl_new_object();
    if (!root) return -1;
    jl_obj_set(root, "op", jl_new_int(2));
    jl_val_t *dd = jl_new_object();
    if (!dd) { jl_free(root); return -1; }
    jl_obj_set(dd, "token", jl_new_string(token));
    jl_val_t *pp = jl_new_object();
    if (!pp) { jl_free(root); return -1; }
    jl_obj_set(pp, "os", jl_new_string("PlayStation 5"));
    jl_obj_set(pp, "browser", jl_new_string("PlayStation 5"));
    jl_obj_set(pp, "device", jl_new_string("PlayStation 5"));
    jl_obj_set(dd, "properties", pp);
    jl_val_t *pr = jl_new_object();
    if (!pr) { jl_free(root); return -1; }
    jl_obj_set(pr, "status", jl_new_string("online"));
    jl_obj_set(pr, "activities", jl_new_array());
    jl_obj_set(pr, "afk", jl_new_bool(0));
    jl_obj_set(pr, "since", jl_new_null());
    jl_obj_set(dd, "presence", pr);
    jl_obj_set(root, "d", dd);
    char *s = jl_stringify(root); jl_free(root);
    if (!s) return -1;
    int rc = ws_send_text(&d->ws, s, strlen(s));
    free(s);
    return rc < 0 ? -1 : 0;
}

static int discord_connect_session(discord_session_t *d, const char *token) {
    if (!d || !token || !token[0]) return -1;
    memset(d, 0, sizeof(*d));
    strncpy(d->token, token, sizeof(d->token) - 1);
    d->token[sizeof(d->token) - 1] = 0;
    char key[64] = ""; make_key(key);
    
    printf("[PS5-RPC] Connecting to Discord Gateway (%s:443)...\n", GW_HOST);
    int rc = ws_connect(&d->ws, GW_HOST, GW_PORT, GW_PATH, key);
    if (rc) {
        printf("[PS5-RPC] WebSocket connect failed: %d\n", rc);
        return -1;
    }
    
    d->connected = 1;
    int64_t now = orbis_mono_s();
    d->last_heartbeat = now;
    d->last_ack = now;
    
    /* Receive HELLO (op 10) */
    char buf[2048]; int op = 0, fin = 0;
    int nr = rx_frame(d, buf, sizeof(buf), &op, &fin, now + 15);
    if (nr <= 0 || op != 1) {
        printf("[PS5-RPC] Gateway HELLO timeout/error (nr=%d, op=%d)\n", nr, op);
        ws_close(&d->ws);
        d->connected = 0;
        return -1;
    }
    
    jl_val_t *h = jl_parse(buf, nr);
    if (h) {
        const jl_val_t *dd = jl_obj_get(h, "d");
        if (dd) {
            const jl_val_t *hi = jl_obj_get(dd, "heartbeat_interval");
            if (hi && hi->type == JL_NUMBER) d->hb_interval_ms = (int64_t)hi->num;
        }
        jl_free(h);
    }
    if (!d->hb_interval_ms) d->hb_interval_ms = 41250;
    if (d->hb_interval_ms < 5000) d->hb_interval_ms = 5000;
    
    printf("[PS5-RPC] Gateway HELLO received (heartbeat interval: %lld ms). Sending IDENTIFY...\n", 
           (long long)d->hb_interval_ms);
           
    if (send_identify(d, token) < 0) {
        printf("[PS5-RPC] IDENTIFY send failed\n");
        ws_close(&d->ws);
        d->connected = 0;
        return -1;
    }
    
    /* Wait for READY */
    int64_t dl = orbis_mono_s() + 20;
    for (;;) {
        nr = rx_frame(d, buf, sizeof(buf), &op, &fin, dl);
        if (nr <= 0) {
            printf("[PS5-RPC] READY wait error (nr=%d)\n", nr);
            break;
        }
        if (op == 8) {
            unsigned code = 0;
            if (nr >= 2) code = (((unsigned char)buf[0] << 8) | ((unsigned char)buf[1]));
            printf("[PS5-RPC] Gateway closed during auth: code %u\n", code);
            ws_close(&d->ws);
            d->connected = 0;
            return code == 4004 ? -2 : -1;
        }
        if (op == 9) { ws_pong(&d->ws); continue; }
        if (op != 1) continue;
        int go = gw_op(buf, (size_t)nr);
        if (go == 11) { d->last_ack = orbis_mono_s(); continue; }
        if (go == 0 && is_ready(buf, (size_t)nr)) {
            gw_seq(d, buf, (size_t)nr);
            printf("[PS5-RPC] Discord Gateway READY! Authentication successful!\n");
            return 0;
        }
    }
    
    ws_close(&d->ws);
    d->connected = 0;
    return -1;
}

static int discord_tick_session(discord_session_t *d) {
    if (!d || !d->connected) return -1;
    int64_t now = orbis_mono_s();
    long hb_s = (long)(d->hb_interval_ms / 1000);
    if (hb_s < 5) hb_s = 5;
    
    if (d->sent_hb && now - d->last_ack > hb_s * 2 + 15) {
        printf("[PS5-RPC] Gateway heartbeat ACK timeout\n");
        d->connected = 0;
        return -1;
    }
    
    long fire = hb_s > 3 ? hb_s - 2 : hb_s;
    if (now - d->last_heartbeat >= fire) {
        char hb[64];
        if (d->seq > 0) snprintf(hb, sizeof(hb), "{\"op\":1,\"d\":%d}", d->seq);
        else            snprintf(hb, sizeof(hb), "{\"op\":1,\"d\":null}");
        if (ws_send_text(&d->ws, hb, strlen(hb)) < 0) {
            printf("[PS5-RPC] Heartbeat send failed\n");
            d->connected = 0;
            return -1;
        }
        d->last_heartbeat = now;
        d->sent_hb = 1;
    }
    
    char buf[2048]; int op = 0, fin = 0;
    for (int i = 0; i < 32; i++) {
        int nr = ws_recv_frame(&d->ws, buf, sizeof(buf), &op, &fin);
        if (nr == -3) continue;
        if (nr == 0) break;
        if (nr < 0) { d->connected = 0; return -1; }
        if (op == 8) {
            unsigned code = 0;
            if (nr >= 2) code = (((unsigned char)buf[0] << 8) | ((unsigned char)buf[1]));
            printf("[PS5-RPC] Gateway closed frame received: %u\n", code);
            d->connected = 0;
            return code == 4004 ? -2 : -1;
        }
        if (op == 9) { ws_pong(&d->ws); continue; }
        if (op != 1 && op != 0) continue;
        switch (gw_op(buf, (size_t)nr)) {
        case 0:
            gw_seq(d, buf, (size_t)nr);
            break;
        case 7:
            printf("[PS5-RPC] Gateway requested reconnect (op 7)\n");
            d->connected = 0;
            return -1;
        case 9:
            printf("[PS5-RPC] Gateway invalid session (op 9)\n");
            d->connected = 0;
            return -3;
        case 11:
            d->last_ack = now;
            break;
        default: break;
        }
    }
    return 0;
}

static int send_presence_update(discord_session_t *d) {
    if (!d || !d->connected) return -1;

    jl_val_t *root = jl_new_object();
    if (!root) return -1;
    jl_obj_set(root, "op", jl_new_int(3));

    jl_val_t *dd = jl_new_object();
    if (!dd) { jl_free(root); return -1; }
    jl_obj_set(root, "d", dd);

    jl_obj_set(dd, "status", jl_new_string("online"));
    jl_obj_set(dd, "since", jl_new_null());
    jl_obj_set(dd, "afk", jl_new_bool(0));

    jl_val_t *activities = jl_new_array();
    if (!activities) { jl_free(root); return -1; }
    jl_obj_set(dd, "activities", activities);

    // If not running a game (home screen / dashboard), turn off RPC completely
    if (!g_current_game.is_running) {
        char *s = jl_stringify(root); jl_free(root);
        if (!s) return -1;
        int r = ws_send_text(&d->ws, s, strlen(s));
        free(s);
        printf("[PS5-RPC] Home screen active: RPC turned off (presence cleared)\n");
        return (r >= 0) ? 0 : -1;
    }

    jl_val_t *act = jl_new_object();
    if (!act) { jl_free(root); return -1; }
    jl_arr_push(activities, act);

    const char *title_name = g_current_game.is_running && g_current_game.title_name[0]
                           ? g_current_game.title_name : "PlayStation 5";
    jl_obj_set(act, "name", jl_new_string(title_name));

    int act_type = 0;
    if (g_current_game.is_running && g_current_game.title_id[0]) {
        act_type = detect_media_type(g_current_game.title_id);
    }
    jl_obj_set(act, "type", jl_new_int(act_type));

    // Native PlayStation platform flag: enables "Playing on PlayStation" + console badge in Discord UI
    jl_obj_set(act, "platform", jl_new_string("ps5"));

    // Custom Details: only set if non-empty
    if (g_config.details[0]) {
        jl_obj_set(act, "details", jl_new_string(g_config.details));
    }

    // Custom State: only set if non-empty
    if (g_config.state[0]) {
        jl_obj_set(act, "state", jl_new_string(g_config.state));
    } else if (!g_current_game.is_running) {
        jl_obj_set(act, "state", jl_new_string("Home Menu"));
    }

    // Playtime Timestamps
    if (g_config.show_timer && g_current_game.is_running && g_current_game.start_time > 0) {
        int64_t now_fixed = time_fixed();
        int64_t st = g_current_game.start_time;
        // Never allow start_time to be in the future (Discord freezes timer at 0:00 if in future)
        if (st > now_fixed) st = now_fixed;
        if (st < 1577836800LL) st = now_fixed; // If epoch before 2020, use current time

        jl_val_t *ts = jl_new_object();
        if (ts) {
            jl_obj_set(ts, "start", jl_new_int(st * 1000LL));
            jl_obj_set(act, "timestamps", ts);
        }
    }

    jl_obj_set(act, "application_id", jl_new_string(DEFAULT_APP_ID));

    // Artwork & Assets
    jl_val_t *assets = jl_new_object();
    if (assets) {
        char mp_buf[512] = "";
        const char *cover_url = g_current_game.is_running && g_current_game.icon_url[0]
                              ? g_current_game.icon_url : DEFAULT_LOGO_URL;
        
        char safe_cover[512] = "";
        if (strncmp(cover_url, "http://", 7) == 0) {
            snprintf(safe_cover, sizeof(safe_cover), "https://%s", cover_url + 7);
            cover_url = safe_cover;
        }

        if (strncmp(cover_url, "mp:", 3) == 0) {
            jl_obj_set(assets, "large_image", jl_new_string(cover_url));
            printf("[PS5-RPC] Large image using pre-resolved mp proxy: %s\n", cover_url);
        } else if (art_resolve_mp(DEFAULT_APP_ID, d->token, cover_url, mp_buf, sizeof(mp_buf)) && mp_buf[0]) {
            jl_obj_set(assets, "large_image", jl_new_string(mp_buf));
            printf("[PS5-RPC] Large image resolved to mp proxy: %s\n", mp_buf);
        } else {
            printf("[PS5-RPC] Fallback cover URL for large_image: %s\n", cover_url);
            jl_obj_set(assets, "large_image", jl_new_string(cover_url));
        }
        jl_obj_set(assets, "large_text", jl_new_string(title_name));

        if (g_config.show_badge) {
            char badge_mp[512] = "";
            if (art_resolve_mp(DEFAULT_APP_ID, d->token, DEFAULT_BADGE_URL, badge_mp, sizeof(badge_mp))) {
                jl_obj_set(assets, "small_image", jl_new_string(badge_mp));
            }
            jl_obj_set(assets, "small_text", jl_new_string("PlayStation 5"));
        }
        jl_obj_set(act, "assets", assets);
    }

    // Interactive Action Buttons
    bool has_b1 = (g_config.btn1_enabled && g_config.btn1_label[0] && g_config.btn1_url[0]);
    bool has_b2 = (g_config.btn2_enabled && g_config.btn2_label[0] && g_config.btn2_url[0]);

    if (has_b1 || has_b2) {
        jl_val_t *btns = jl_new_array();
        jl_val_t *urls = jl_new_array();
        if (has_b1) {
            jl_arr_push(btns, jl_new_string(g_config.btn1_label));
            jl_arr_push(urls, jl_new_string(g_config.btn1_url));
        }
        if (has_b2) {
            jl_arr_push(btns, jl_new_string(g_config.btn2_label));
            jl_arr_push(urls, jl_new_string(g_config.btn2_url));
        }
        jl_obj_set(act, "buttons", btns);

        jl_val_t *meta = jl_new_object();
        if (meta) {
            jl_obj_set(meta, "button_urls", urls);
            jl_obj_set(act, "metadata", meta);
        }
    }

    jl_obj_set(act, "instance", jl_new_bool(1));

    char *s = jl_stringify(root); jl_free(root);
    if (!s) return -1;
    printf("[PS5-RPC] Sending Presence Update:\n%s\n", s);
    int r = ws_send_text(&d->ws, s, strlen(s));
    free(s);
    return (r >= 0) ? 0 : -1;
}

void* discord_gateway_thread(void* arg) {
    (void)arg;
    discord_session_t dc;
    memset(&dc, 0, sizeof(dc));
    
    printf("[PS5-RPC] Discord Gateway worker thread running.\n");

    while (1) {
        if (strlen(g_config.token) == 0) {
            // Waiting for token configuration from web UI
            sleep(2);
            continue;
        }

        int rc = discord_connect_session(&dc, g_config.token);
        if (rc == -2) {
            char ip_buf[64] = "";
            get_local_ip(ip_buf, sizeof(ip_buf));
            char notify_msg[128];
            if (ip_buf[0]) {
                printf("[PS5-RPC] FATAL: Discord token rejected (Close 4004). Please update token at http://%s:6767\n", ip_buf);
                snprintf(notify_msg, sizeof(notify_msg), "Discord Token Invalid!\nPlease check http://%s:6767", ip_buf);
            } else {
                printf("[PS5-RPC] FATAL: Discord token rejected (Close 4004). Please update token at web UI (Port 6767).\n");
                snprintf(notify_msg, sizeof(notify_msg), "Discord Token Invalid!\nPlease check Web UI (Port 6767)");
            }
            notify_throttled("token4004", notify_msg, 60);
            sleep(5);
            continue;
        }
        if (rc != 0) {
            printf("[PS5-RPC] Failed to reach Discord Gateway, retrying in 5s...\n");
            sleep(5);
            continue;
        }

        // Post presence immediately on connection
        send_presence_update(&dc);
        g_config.needs_update = false;

        char last_game_id[64] = "";
        strncpy(last_game_id, g_current_game.title_id, sizeof(last_game_id) - 1);
        char last_icon_url[256] = "";
        strncpy(last_icon_url, g_current_game.icon_url, sizeof(last_icon_url) - 1);
        int64_t last_art_retry_time = 0;

        while (dc.connected) {
            int tr = discord_tick_session(&dc);
            if (tr != 0) {
                printf("[PS5-RPC] Gateway session dropped (tick result: %d). Reconnecting...\n", tr);
                break;
            }

            // Check if game, icon, or settings changed
            bool game_changed = (strcmp(last_game_id, g_current_game.title_id) != 0);
            bool icon_changed = (strcmp(last_icon_url, g_current_game.icon_url) != 0);

            if (game_changed || icon_changed) {
                strncpy(last_game_id, g_current_game.title_id, sizeof(last_game_id) - 1);
                strncpy(last_icon_url, g_current_game.icon_url, sizeof(last_icon_url) - 1);
                g_config.needs_update = true;
            }

            // Auto-retry icon resolution in the background if fresh upload hasn't converted to mp: yet.
            // As soon as Discord proxies the newly uploaded image, it immediately updates Discord!
            int64_t now_mono = orbis_mono_s();
            if (g_current_game.is_running && g_current_game.icon_url[0] &&
                strncmp(g_current_game.icon_url, "mp:", 3) != 0 &&
                now_mono - last_art_retry_time >= 2) {
                last_art_retry_time = now_mono;
                char retry_mp[512] = "";
                if (art_resolve_mp(DEFAULT_APP_ID, dc.token, g_current_game.icon_url, retry_mp, sizeof(retry_mp)) && retry_mp[0]) {
                    printf("[PS5-RPC] Auto-retry resolved mp proxy: %s\n", retry_mp);
                    strncpy(g_current_game.icon_url, retry_mp, sizeof(g_current_game.icon_url) - 1);
                    g_current_game.icon_url[sizeof(g_current_game.icon_url) - 1] = '\0';
                    strncpy(last_icon_url, g_current_game.icon_url, sizeof(last_icon_url) - 1);
                    catbox_cache_update(g_current_game.title_id, retry_mp);
                    g_config.needs_update = true;
                }
            }

            if (g_config.needs_update) {
                g_config.needs_update = false;
                send_presence_update(&dc);
            }

            usleep(250000); // 250ms tick
        }

        ws_close(&dc.ws);
        dc.connected = 0;
        sleep(2);
    }

    return NULL;
}
