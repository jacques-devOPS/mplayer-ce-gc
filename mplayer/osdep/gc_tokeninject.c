/* MPlayer CE GameCube port: Jellyfin token injection for tokenless URLs.
 * JellyCube exports M3U playlists without api_key (playlists survive
 * re-authentication). At HTTP-open time the player appends the live
 * token from token.cache, kept fresh by JellyCube.
 * GPL v2+ per MPlayer CE. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "config.h"
#include "mp_msg.h"

static char jellyfin_token[256] = "";
static int  jellyfin_token_loaded = 0;

/* token.cache sits beside the DOL, same directory as mplayer.conf.
 * Config discovery here mirrors the launch-path logic in plat_gekko.c. */

static int read_line_stripped(const char *path, char *dst, size_t size)
{
    FILE *f = fopen(path, "r");
    size_t n;
    if (!f) return -1;
    if (!fgets(dst, size, f)) { fclose(f); return -1; }
    fclose(f);
    n = strlen(dst);
    while (n && (dst[n-1] == '\n' || dst[n-1] == '\r' || dst[n-1] == ' ' || dst[n-1] == '\t'))
        dst[--n] = 0;
    return n ? 0 : -1;
}

static int try_token_paths(char *out, size_t out_size)
{
    static const char *names[] = { "token.cache", "apps/mplayer-ce/token.cache" };
    static char dir[512];
    static int dir_known = 0;
    unsigned i;

    if (!dir_known) {
        char self[512];
        ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
        char *slash;
        /* Gekko: no /proc; try argv[0]-derived cwd via config search instead. */
        dir_known = 1;
        if (n > 0) {
            self[n] = 0;
            slash = strrchr(self, '/');
            if (slash) {
                size_t d = (size_t)(slash - self);
                if (d >= sizeof(dir)) d = sizeof(dir) - 1;
                memcpy(dir, self, d);
                dir[d] = 0;
                dir_known = 2;
            }
        }
        if (dir_known != 2) {
            /* fall back: MPLAYER_CONFDIR is exported by plat_gekko init */
            extern char MPLAYER_CONFDIR[100];
            snprintf(dir, sizeof(dir), "%s", MPLAYER_CONFDIR);
            dir_known = 2;
        }
    }

    if (dir_known != 2) return -1;
    for (i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        snprintf(out, out_size, "%s/%s", dir, names[i]);
        if (access(out, R_OK) == 0) return 0;
        if (dir[0] == 0) { /* bare name case */
            snprintf(out, out_size, "%s", names[i]);
            if (access(out, R_OK) == 0) return 0;
        }
    }
    return -1;
}

int gc_jellyfin_token(const char **token)
{
    char path[600];
    if (!token) return -1;
    if (jellyfin_token_loaded && jellyfin_token[0]) {
        *token = jellyfin_token;
        return 0;
    }
    if (try_token_paths(path, sizeof(path)) < 0) return -1;
    if (read_line_stripped(path, jellyfin_token, sizeof(jellyfin_token)) < 0) return -1;
    jellyfin_token_loaded = 1;
    *token = jellyfin_token;
    return 0;
}

/* Rewrite url -> out (size out_size): append &api_key=<token> when the URL
 * is a Jellyfin stream URL (contains "/Videos/" and "stream.ts" or
 * "/Audio/" and "/universal" or "/hls1/") that carries no api_key yet. */
int gc_jellyfin_inject(const char *url, char *out, size_t out_size)
{
    const char *tok;
    const char *q;
    int need_amp;

    if (!url || !out || !out_size) return -1;

    if (strstr(url, "/Videos/") == NULL && strstr(url, "/Audio/") == NULL)
        goto passthrough;
    if (strstr(url, "api_key=") != NULL)
        goto passthrough;

    if (gc_jellyfin_token(&tok) < 0)
        goto passthrough;

    need_amp = (strchr(url, '?') != NULL);
    q = strchr(url, '#'); /* keep any fragment at the tail */
    if (q) {
        size_t head = (size_t)(q - url);
        if (head >= out_size) return -1;
        memcpy(out, url, head);
        out[head] = 0;
    } else {
        if (strlen(url) >= out_size) return -1;
        strcpy(out, url);
    }
    need_amp = (strchr(out, '?') != NULL);

    {
        int n = snprintf(out + strlen(out), out_size - strlen(out),
                         "%sapi_key=%s", need_amp ? "&" : "", tok);
        if (n < 0 || (size_t)n >= out_size - strlen(out)) return -1;
        if (q)
            strncat(out, q, out_size - strlen(out) - 1);
    }
    return 0;

passthrough:
    if (strlen(url) >= out_size) return -1;
    strcpy(out, url);
    return 0;
}
