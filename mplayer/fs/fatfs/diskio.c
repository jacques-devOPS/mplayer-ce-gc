/* MPlayer CE GameCube port: FatFs disk interface on libogc DISC_INTERFACE.
 * FatFs (C)ChaN, via swiss-gc; disk glue (this file) GPL v2+ per MPlayer CE. */

#include "diskio.h"
#include <ogc/disc_io.h>

#define MAX_FATFS_VOLUMES 12
static const DISC_INTERFACE *ff_ifaces[MAX_FATFS_VOLUMES];

int fatfs_attach(int vol, const DISC_INTERFACE *io)
{
    if (vol < 0 || vol >= MAX_FATFS_VOLUMES || !io) return -1;
    ff_ifaces[vol] = io;
    return 0;
}

int fatfs_detach(int vol)
{
    if (vol < 0 || vol >= MAX_FATFS_VOLUMES) return -1;
    ff_ifaces[vol] = NULL;
    return 0;
}

DSTATUS disk_status(BYTE pdrv)
{
    const DISC_INTERFACE *io = ff_ifaces[pdrv];
    if (!io || !io->isInserted()) return STA_NOINIT;
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    const DISC_INTERFACE *io = ff_ifaces[pdrv];
    if (!io) return STA_NOINIT;
    if (!io->startup()) return STA_NOINIT;
    if (!io->isInserted()) return STA_NODISK;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    const DISC_INTERFACE *io = ff_ifaces[pdrv];
    if (!io) return RES_NOTRDY;
    if (!io->readSectors((sec_t)sector, (sec_t)count, buff)) return RES_ERROR;
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    const DISC_INTERFACE *io = ff_ifaces[pdrv];
    if (!io) return RES_NOTRDY;
    if (!io->writeSectors((sec_t)sector, (sec_t)count, (void *)buff)) return RES_ERROR;
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    const DISC_INTERFACE *io = ff_ifaces[pdrv];
    if (!io) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    default:
        return RES_OK;
    }
}

/* no RTC path plumbed in the player; fixed timestamp */
DWORD get_fattime(void)
{
    return ((DWORD)(2020 - 1980) << 25) | (1 << 21) | (1 << 16);
}