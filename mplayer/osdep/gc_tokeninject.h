/* MPlayer CE GameCube port: Jellyfin token injection API. GPL v2+. */
#ifndef GC_TOKENINJECT_H
#define GC_TOKENINJECT_H

#include <stddef.h>

/* out = url with api_key=<token from token.cache> appended when the URL is
 * a Jellyfin stream URL without an api_key. Returns 0 always when the URL
 * is untouched; -1 on buffer overflow. */
int gc_jellyfin_inject(const char *url, char *out, size_t out_size);

#endif