#ifndef NOTIFY_H
#define NOTIFY_H

#include <stdbool.h>
#include <stddef.h>

void notify_show(const char *msg);
void notify_custom(const char *title, const char *submessage, const char *icon_path);
void notify_throttled(const char *key, const char *msg, int period_s);
const char* notify_get_icon_path(void);
void get_local_ip(char* out_ip, size_t sz);

#endif
