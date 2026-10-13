/* MPlayer CE GameCube port: FatFs devoptab mount API. GPL v2+. */
#ifndef GC_FATFS_H
#define GC_FATFS_H

#include <ogc/disc_io.h>

/* Mount device `name` (e.g. "carda") on io, registering "name:" paths.
 * Returns volume index >= 0, or -1 on failure. Supports FAT12/16/32/exFAT. */
int gc_fatfs_mount(const char *name, const DISC_INTERFACE *io);

/* Unmount by device name. Returns 0 on success, -1 if not found. */
int gc_fatfs_unmount(const char *name);

#endif