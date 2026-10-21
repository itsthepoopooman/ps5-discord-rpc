#ifndef EMBEDDED_ICON_H
#define EMBEDDED_ICON_H

#include <stddef.h>

extern const unsigned char embedded_icon_png[];
extern const size_t embedded_icon_png_size;

void write_embedded_icon(void);

#endif /* EMBEDDED_ICON_H */
