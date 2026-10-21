#include "notify.h"
#include "embedded_icon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>

void get_local_ip(char* out_ip, size_t sz) {
    if (!out_ip || sz == 0) return;
    out_ip[0] = '\0';

    struct ifaddrs *ifaddr = NULL, *ifa = NULL;
    if (getifaddrs(&ifaddr) == -1) return;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *pAddr = (struct sockaddr_in *)ifa->ifa_addr;
            char* ip = inet_ntoa(pAddr->sin_addr);
            if (ip && strcmp(ip, "127.0.0.1") != 0) {
                strncpy(out_ip, ip, sz - 1);
                out_ip[sz - 1] = '\0';
                break;
            }
        }
    }
    freeifaddrs(ifaddr);
}

#define SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM 0xFE

/* Native PS5 Notification IPC */
extern int sceNotificationSend(int userId, bool isLogged, const char* payload);

static bool file_exists_nonempty(const char* path) {
    if (!path || !path[0]) return false;
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0);
}

const char* notify_get_icon_path(void) {
    write_embedded_icon();
    if (file_exists_nonempty("/data/discord_rpc/icon.png")) {
        return "/user/data/discord_rpc/icon.png";
    }
    if (file_exists_nonempty("/data/discord.png")) {
        return "/user/data/discord.png";
    }
    return "/user/data/discord_rpc/icon.png";
}

static void escape_json_str(const char* in, char* out, size_t out_sz) {
    size_t j = 0;
    for (size_t i = 0; in && in[i] && j < out_sz - 2; i++) {
        if (in[i] == '\"' || in[i] == '\\') {
            out[j++] = '\\';
        }
        if (in[i] == '\n') {
            out[j++] = ' ';
            continue;
        }
        if (in[i] == '\r') continue;
        out[j++] = in[i];
    }
    out[j] = '\0';
}

void notify_custom(const char *title, const char *submessage, const char *icon_path) {
    if (!title || !title[0]) return;
    
    const char *actual_icon = icon_path;
    if (!actual_icon || !actual_icon[0]) {
        actual_icon = notify_get_icon_path();
    }

    char esc_title[256] = {0};
    char esc_sub[256] = {0};
    escape_json_str(title, esc_title, sizeof(esc_title));
    escape_json_str(submessage ? submessage : "", esc_sub, sizeof(esc_sub));

    time_t now = time(NULL);
    char time_str[64];
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S.000Z", &tm_utc);

    // InteractiveToastTemplateB logic
    char json[2048];
    snprintf(json, sizeof(json),
        "{\n"
        "  \"rawData\": {\n"
        "    \"viewTemplateType\": \"InteractiveToastTemplateB\",\n"
        "    \"channelType\": \"Downloads\",\n"
        "    \"useCaseId\": \"IDC\",\n"
        "    \"toastOverwriteType\": \"No\",\n"
        "    \"isImmediate\": true,\n"
        "    \"priority\": 100,\n"
        "    \"viewData\": {\n"
        "      \"icon\": {\n"
        "        \"type\": \"Url\",\n"
        "        \"parameters\": {\n"
        "          \"url\": \"%s\"\n"
        "        }\n"
        "      },\n"
        "      \"message\": {\n"
        "        \"body\": \"%s\"\n"
        "      },\n"
        "      \"subMessage\": {\n"
        "        \"body\": \"%s\"\n"
        "      },\n"
        "      \"actions\": [\n"
        "        {\n"
        "          \"actionName\": \"Go to Debug Settings\",\n"
        "          \"actionType\": \"DeepLink\",\n"
        "          \"defaultFocus\": true,\n"
        "          \"parameters\": {\n"
        "            \"actionUrl\": \"pssettings:play?function=debug_settings\"\n"
        "          }\n"
        "        }\n"
        "      ]\n"
        "    },\n"
        "    \"platformViews\": {\n"
        "      \"previewDisabled\": {\n"
        "        \"viewData\": {\n"
        "          \"icon\": {\n"
        "            \"type\": \"Predefined\",\n"
        "            \"parameters\": {\n"
        "              \"icon\": \"download\"\n"
        "            }\n"
        "          },\n"
        "          \"message\": {\n"
        "            \"body\": \"%s\"\n"
        "          }\n"
        "        }\n"
        "      }\n"
        "    }\n"
        "  },\n"
        "  \"createdDateTime\": \"%s\",\n"
        "  \"localNotificationId\": \"%lu\"\n"
        "}",
        actual_icon,
        esc_title,
        esc_sub,
        esc_title,
        time_str,
        (unsigned long)(now ^ (unsigned long)getpid())
    );

    int rc = sceNotificationSend(SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM, true, json);
    printf("[PS5-RPC-NOTIFY] sceNotificationSend returned: 0x%08x (icon: %s)\n", rc, actual_icon ? actual_icon : "default");
}

void notify_show(const char *msg) {
    if (!msg || !msg[0]) return;
    const char *nl = strchr(msg, '\n');
    if (nl) {
        char title[256];
        size_t tlen = (size_t)(nl - msg);
        if (tlen >= sizeof(title)) tlen = sizeof(title) - 1;
        strncpy(title, msg, tlen);
        title[tlen] = '\0';
        notify_custom(title, nl + 1, NULL);
    } else {
        notify_custom(msg, NULL, NULL);
    }
}

void notify_throttled(const char *key, const char *msg, int period_s) {
    static struct {
        char key[32];
        time_t last_time;
    } s_throttle[8];
    static int s_throttle_count = 0;
    
    time_t now = time(NULL);
    for (int i = 0; i < s_throttle_count; i++) {
        if (strcmp(s_throttle[i].key, key) == 0) {
            if (now - s_throttle[i].last_time < period_s) return;
            s_throttle[i].last_time = now;
            notify_show(msg);
            return;
        }
    }
    
    if (s_throttle_count < 8) {
        strncpy(s_throttle[s_throttle_count].key, key, 31);
        s_throttle[s_throttle_count].last_time = now;
        s_throttle_count++;
    }
    notify_show(msg);
}
