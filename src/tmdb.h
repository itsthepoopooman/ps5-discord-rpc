/* tmdb.h - Sony TMDB runtime title/icon resolver (PS4 transport + cache). */
#ifndef TMDB_H
#define TMDB_H
#include <stddef.h>
#include <netinet/in.h>

int resolve_via_public_dns(const char *host, struct in_addr *out_ip);

int tmdb_resolve(const char *titleId, char *name, size_t name_cap,
                 char *icon, size_t icon_cap);
#endif
