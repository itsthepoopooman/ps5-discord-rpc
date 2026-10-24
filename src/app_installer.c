#include "app_installer.h"
#include "embedded_sfo.h"
#include "embedded_icon.h"
#include "embedded_app_html.h"
#include "notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#define PRIMARY_APP_DIR "/user/app/DISC00001"
#define FALLBACK_APP_DIR "/data/apps/DISC00001"

static void ensure_dir(const char* dir) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t len = strlen(tmp);
    if (len == 0) return;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            chmod(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
    chmod(tmp, 0777);
}

static bool write_file(const char* path, const void* data, size_t sz) {
    if (!path || !data) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t written = fwrite(data, 1, sz, f);
    fclose(f);
    chmod(path, 0666);
    return written == sz;
}

bool is_media_app_installed(void) {
    if (access(PRIMARY_APP_DIR "/sce_sys/param.sfo", F_OK) == 0) {
        return true;
    }
    if (access(FALLBACK_APP_DIR "/sce_sys/param.sfo", F_OK) == 0) {
        return true;
    }
    return false;
}

bool install_media_app_to_ps5(void) {
    printf("[App-Installer] Installing Discord Media App (DISC00001) to PS5...\n");

    const char* target_base = PRIMARY_APP_DIR;

    // 1. Create target directories
    char sys_dir[256];
    snprintf(sys_dir, sizeof(sys_dir), "%s/sce_sys", target_base);
    ensure_dir(sys_dir);

    // 2. Write PARAM.SFO
    char sfo_path[256];
    snprintf(sfo_path, sizeof(sfo_path), "%s/param.sfo", sys_dir);
    if (!write_file(sfo_path, embedded_param_sfo, embedded_param_sfo_size)) {
        printf("[App-Installer] Cannot write to %s, attempting fallback to %s\n", sfo_path, FALLBACK_APP_DIR);
        target_base = FALLBACK_APP_DIR;
        snprintf(sys_dir, sizeof(sys_dir), "%s/sce_sys", target_base);
        ensure_dir(sys_dir);
        snprintf(sfo_path, sizeof(sfo_path), "%s/param.sfo", sys_dir);
        if (!write_file(sfo_path, embedded_param_sfo, embedded_param_sfo_size)) {
            printf("[App-Installer] Failed to write PARAM.SFO to console storage!\n");
            return false;
        }
    }
    printf("[App-Installer] Wrote %s (%zu bytes)\n", sfo_path, embedded_param_sfo_size);

    // 3. Write icon0.png
    char icon_path[256];
    snprintf(icon_path, sizeof(icon_path), "%s/icon0.png", sys_dir);
    write_file(icon_path, embedded_icon_png, embedded_icon_png_size);
    printf("[App-Installer] Wrote %s (%zu bytes)\n", icon_path, embedded_icon_png_size);

    // 4. Write index.html (Fullscreen launcher)
    char html_path[256];
    snprintf(html_path, sizeof(html_path), "%s/index.html", target_base);
    write_file(html_path, embedded_app_html, strlen(embedded_app_html));
    printf("[App-Installer] Wrote %s (%zu bytes)\n", html_path, strlen(embedded_app_html));

    printf("[App-Installer] Successfully installed Discord Media App to %s\n", target_base);
    notify_custom("Discord Media App", "Installed Discord to Media Home Screen!", NULL);

    return true;
}

void app_installer_auto_check(void) {
    if (!is_media_app_installed()) {
        printf("[App-Installer] Media App (DISC00001) not found. Auto-installing...\n");
        install_media_app_to_ps5();
    } else {
        printf("[App-Installer] Discord Media App is already installed on console storage.\n");
    }
}
