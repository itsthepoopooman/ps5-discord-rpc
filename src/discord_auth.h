#ifndef DISCORD_AUTH_H
#define DISCORD_AUTH_H

#include <stddef.h>
#include <stdbool.h>

int discord_auth_login(const char* login, const char* password, char* out_resp, size_t out_resp_sz);
int discord_auth_mfa(const char* ticket, const char* code, char* out_resp, size_t out_resp_sz);

#endif
