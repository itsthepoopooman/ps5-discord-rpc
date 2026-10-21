#include "detect.h"
#include <string.h>

int detect_media_type(const char *title_id) {
    if (!title_id) return 0;
    static const struct { const char *id; int type; } media[] = {
        { "CUSA00127", 3 }, /* Netflix -> Watching */
        { "CUSA01015", 3 }, /* YouTube -> Watching */
        { "PPSA01650", 3 }, /* YouTube PS5 */
        { "PPSA01651", 3 }, /* Netflix PS5 */
        { "PPSA02058", 3 }, /* Disney+ PS5 */
        { "PPSA02980", 2 }, /* Spotify PS5 -> Listening */
    };
    for (unsigned i = 0; i < sizeof(media) / sizeof(media[0]); i++) {
        if (!strcmp(title_id, media[i].id)) return media[i].type;
    }
    return 0; /* Default: Playing (type 0) */
}
