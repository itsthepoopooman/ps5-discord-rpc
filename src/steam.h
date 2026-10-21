#ifndef STEAM_H
#define STEAM_H

#include <stddef.h>

/* Resolves official high-resolution game cover art by title name using Steam Store Search.
 * Returns 1 if resolved with out_icon populated, 0 otherwise. */
int steam_resolve_icon(const char *title_name, char *out_icon, size_t cap);

#endif
