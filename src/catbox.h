#ifndef CATBOX_H
#define CATBOX_H

#include <stddef.h>

/* Uploads a local PNG image file to catbox.moe and returns the public HTTPS URL.
 * Results are cached in memory and in /data/discord_rpc/icon_cache.json by
 * title_id, so a given icon is uploaded once per console.
 * Returns 1 on success with out_url populated, 0 on failure. */
int catbox_upload_icon(const char *title_id, const char *file_path, char *out_url, size_t cap);

/* Returns 1 and copies the cached URL for title_id if a Catbox-backed entry
 * exists. 0 otherwise. */
int catbox_cache_lookup(const char *title_id, char *out_url, size_t cap);

/* Records the final Discord-ready URL for a title. Only Catbox-hosted URLs are
 * persisted; media-proxy paths for other hosts are ignored. */
void catbox_cache_update(const char *title_id, const char *url);

#endif
