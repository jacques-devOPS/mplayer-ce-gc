/*
 * GameCube network stubs.
 *
 * The GameCube BBA stack in libogc has no DNS resolver and no
 * getsockname support. libogc 3.1 cube builds do not export
 * net_gethostbyname or net_getsockname, but generic socket code
 * (MPlayer stream layer, ffmpeg, libogc soc_getsockname.o)
 * still references them.
 *
 * These stubs fail cleanly. Streams must use literal IPv4 addresses.
 */
#include <gctypes.h>
#include <errno.h>

struct hostent;

struct hostent *net_gethostbyname(const char *name)
{
	(void)name;
	errno = ENOSYS;
	return (struct hostent *)0;
}

s32 net_getsockname(s32 s, void *name, void *namelen)
{
	(void)s;
	(void)name;
	(void)namelen;
	errno = ENOSYS;
	return -1;
}
