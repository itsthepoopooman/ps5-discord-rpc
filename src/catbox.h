#ifndef CATBOX_H
#define CATBOX_H

#include <stddef.h>

/* Uploads a local PNG image file to catbox.moe and returns the public HTTPS URL.
 * Results are cached in memory by title_id.
 * Returns 1 on success with out_url populated, 0 on failure. */
int catbox_upload_icon(const char *title_id, const char *file_path, char *out_url, size_t cap);
void catbox_cache_update(const char *title_id, const char *url);

#endif
