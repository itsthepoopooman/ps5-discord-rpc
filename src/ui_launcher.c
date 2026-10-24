#include "ui_launcher.h"
#include "notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SCE_PAD_BUTTON_R3 0x00000004
#define SCE_PAD_BUTTON_UP 0x00000010
#define COMBO_TRIGGER     (SCE_PAD_BUTTON_UP | SCE_PAD_BUTTON_R3)

typedef struct {
    uint32_t buttons;
    uint8_t  lx;
    uint8_t  ly;
    uint8_t  rx;
    uint8_t  ry;
    uint8_t  l2;
    uint8_t  r2;
    uint8_t  reserved[128];
} ScePadData;

/* System library declarations */
extern int sceUserServiceInitialize(void*);
extern int sceUserServiceGetInitialUser(int *userId);
extern int sceUserServiceGetForegroundUser(int *userId);
extern int sceSystemServiceLaunchWebBrowser(const char *uri, void* reserved);

extern int scePadInit(void);
extern int scePadOpen(int userId, int type, int index, void *param);
extern int scePadReadState(int handle, ScePadData *pData);
extern int scePadClose(int handle);

static time_t s_last_launch_time = 0;

void launch_discord_web_ui(void) {
    time_t now = time(NULL);
    if (now - s_last_launch_time < 3) {
        return; // Debounce for 3 seconds
    }
    s_last_launch_time = now;

    printf("[UI-Launcher] Launching PS5 native browser to http://127.0.0.1:6767...\n");
    notify_custom("Discord Hub", "Opening Discord Web UI...", NULL);

    sceUserServiceInitialize(NULL);
    int res = sceSystemServiceLaunchWebBrowser("http://127.0.0.1:6767", NULL);
    if (res != 0) {
        printf("[UI-Launcher] sceSystemServiceLaunchWebBrowser returned: 0x%08X\n", res);
    } else {
        printf("[UI-Launcher] Successfully launched Discord Web UI!\n");
    }
}

void* controller_listener_thread(void* arg) {
    printf("[UI-Launcher] Controller keybind listener started (D-Pad UP + R3)...\n");

    sceUserServiceInitialize(NULL);
    scePadInit();

    int pad_handle = -1;
    int current_user = -1;

    while (1) {
        // 1. Ensure we have an open pad handle
        if (pad_handle < 0) {
            int user_id = -1;
            if (sceUserServiceGetForegroundUser(&user_id) != 0 || user_id < 0) {
                if (sceUserServiceGetInitialUser(&user_id) != 0 || user_id < 0) {
                    user_id = 0xFE; // system/default user fallback
                }
            }

            current_user = user_id;
            pad_handle = scePadOpen(current_user, 0, 0, NULL);
            if (pad_handle < 0) {
                pad_handle = scePadOpen(0xFE, 0, 0, NULL);
            }

            if (pad_handle < 0) {
                usleep(500000); // 500ms before retry
                continue;
            }
        }

        // 2. Read pad state
        ScePadData data;
        memset(&data, 0, sizeof(data));
        int ret = scePadReadState(pad_handle, &data);

        if (ret == 0) {
            // Check if D-Pad UP and R3 are held down simultaneously
            if ((data.buttons & COMBO_TRIGGER) == COMBO_TRIGGER) {
                printf("[UI-Launcher] Shortcut detected: D-Pad UP + R3 pressed!\n");
                launch_discord_web_ui();

                // Wait until buttons are released before listening again
                while (1) {
                    usleep(100000); // 100ms
                    memset(&data, 0, sizeof(data));
                    if (scePadReadState(pad_handle, &data) != 0 ||
                        (data.buttons & COMBO_TRIGGER) != COMBO_TRIGGER) {
                        break;
                    }
                }
            }
        } else {
            // Error reading state, close and reopen handle next tick
            scePadClose(pad_handle);
            pad_handle = -1;
        }

        usleep(50000); // Poll every 50ms (20Hz)
    }

    return NULL;
}
