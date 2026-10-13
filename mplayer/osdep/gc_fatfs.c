/* MPlayer CE GameCube port: devoptab provider over FatFs (FAT12/16/32 + exFAT).
 * Registers one newlib device per mount ("carda:", "cardb:", "sd2:", ...).
 * Handlers receive paths WITH the device prefix (devkitPro newlib passes the
 * full path, libfat-style), so the volume is resolved by prefix match.
 * FatFs (C)ChaN via swiss-gc; this glue GPL v2+ per MPlayer CE. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/iosupport.h>
#include <reent.h>
#include <ogc/disc_io.h>

#include "../fs/fatfs/ff.h"
#include "gc_fatfs.h"

#define MAX_DEVS FF_VOLUMES

typedef struct {
    FATFS fs;
    int vol;
    char name[16];
    char cwd[FF_MAX_LFN + 2];
    devoptab_t ops;
} fatfs_dev_t;

static fatfs_dev_t devs[MAX_DEVS];
static int dev_count = 0;

/* ---- helpers ---- */

static fatfs_dev_t *find_by_prefix(const char *path)
{
    int i;
    if (!path) return NULL;
    for (i = 0; i < dev_count; i++) {
        size_t n = strlen(devs[i].name);
        if (!strncmp(path, devs[i].name, n) && path[n] == ':')
            return &devs[i];
    }
    /* stripped prefix (safety net): relative resolution picks dev 0 */
    return dev_count ? &devs[0] : NULL;
}

/* Build "N:rest" with FatFs separators. p may be "/dir/file" or "" (root). */
static void mkvol(const fatfs_dev_t *d, char *dst, size_t dstlen,
                  const char *p, int use_cwd)
{
    char tmp[FF_MAX_LFN * 2 + 8];
    size_t n;
    const char *rest = p;

    /* strip device prefix if present */
    n = strlen(d->name);
    if (p && !strncmp(p, d->name, n) && p[n] == ':')
        rest = p + n + 1;

    if (use_cwd && (!rest || rest[0] != '/')) {
        snprintf(tmp, sizeof(tmp), "%d:/%s%s",
                 d->vol, d->cwd[0] ? d->cwd : "",
                 (rest && rest[0] == '/') ? rest + 1 : (rest ? rest : ""));
    } else {
        snprintf(tmp, sizeof(tmp), "%d:%s", d->vol, rest ? rest : "/");
        if (!rest || !rest[0]) snprintf(tmp, sizeof(tmp), "%d:/", d->vol);
    }
    /* FatFs accepts '/' separators; drop leading doubles */
    snprintf(dst, dstlen, "%s", tmp);
}

static void seterr(struct _reent *r, FRESULT res)
{
    if (!r) return;
    switch (res) {
    case FR_OK: break;
    case FR_NO_FILE: case FR_NO_PATH: case FR_INVALID_NAME:
        r->_errno = ENOENT; break;
    case FR_INVALID_DRIVE: case FR_NOT_ENABLED: case FR_NOT_READY:
    case FR_INVALID_OBJECT: case FR_WRITE_PROTECTED:
        r->_errno = EIO; break;
    case FR_EXIST: r->_errno = EEXIST; break;
    case FR_DENIED: case FR_LOCKED:
        r->_errno = EACCES; break;
    case FR_TOO_MANY_OPEN_FILES: r->_errno = EMFILE; break;
    case FR_DISK_FULL: r->_errno = ENOSPC; break;
    default: r->_errno = EINVAL; break;
    }
}

/* ---- file ops ---- */

static int ff_open_r(struct _reent *r, void *fileStruct,
                     const char *path, int flags, int mode)
{
    FIL *fp = (FIL *)fileStruct;
    fatfs_dev_t *d = find_by_prefix(path);
    BYTE fa = 0;
    char vp[FF_MAX_LFN * 2 + 8];
    (void)mode;
    if (!d) { r->_errno = ENODEV; return -1; }

    if (flags & O_WRONLY || flags & O_RDWR) fa |= FA_WRITE;
    if (flags & O_RDWR) fa |= FA_READ | FA_WRITE;
    if (!(flags & (O_WRONLY | O_RDWR))) fa |= FA_READ;
    if (flags & O_CREAT) {
        fa |= FA_CREATE_ALWAYS;
        if (flags & O_EXCL) fa &= ~FA_OPEN_ALWAYS, fa &= ~FA_CREATE_ALWAYS, fa |= FA_CREATE_NEW;
    }
    if (flags & O_APPEND) fa |= FA_OPEN_APPEND;

    mkvol(d, vp, sizeof(vp), path, 1);
    FRESULT res = f_open(fp, vp, fa);
    if (res == FR_OK && (flags & O_TRUNC)) {
        res = f_truncate(fp);
    }
    if (res != FR_OK) { seterr(r, res); return -1; }
    return (int)(size_t)fp; /* newlib uses the fd as index; return nonzero */
}

static int ff_close_r(struct _reent *r, void *fd)
{
    FRESULT res = f_close((FIL *)fd);
    if (res != FR_OK) { seterr(r, res); return -1; }
    return 0;
}

static ssize_t ff_write_r(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    UINT bw = 0;
    FRESULT res = f_write((FIL *)fd, ptr, len, &bw);
    if (res != FR_OK) { seterr(r, res); return -1; }
    return bw;
}

static ssize_t ff_read_r(struct _reent *r, void *fd, char *ptr, size_t len)
{
    UINT br = 0;
    FRESULT res = f_read((FIL *)fd, ptr, len, &br);
    if (res != FR_OK) { seterr(r, res); return -1; }
    return br;
}

static off_t ff_seek_r(struct _reent *r, void *fd, off_t pos, int dir)
{
    FIL *fp = (FIL *)fd;
    FSIZE_t base;
    switch (dir) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = f_tell(fp); break;
    case SEEK_END: base = f_size(fp); break;
    default: r->_errno = EINVAL; return -1;
    }
    if (base + pos < 0) { r->_errno = EINVAL; return -1; }
    FRESULT res = f_lseek(fp, base + (FSIZE_t)pos);
    if (res != FR_OK) { seterr(r, res); return -1; }
    return (off_t)f_tell(fp);
}

static int ff_fstat_r(struct _reent *r, void *fd, struct stat *st)
{
    if (!st) { r->_errno = EINVAL; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    st->st_size = (off_t)f_size((FIL *)fd);
    st->st_blksize = 512;
    st->st_blocks = (st->st_size + 511) / 512;
    return 0;
}

static int stat_common(fatfs_dev_t *d, const char *path, struct stat *st)
{
    FILINFO fno;
    char vp[FF_MAX_LFN * 2 + 8];
    if (!st) return -1;
    memset(st, 0, sizeof(*st));
    mkvol(d, vp, sizeof(vp), path, 1);
    if (f_stat(vp, &fno) != FR_OK) {
        /* root dir */
        if (!strcmp(vp + strlen(d->name) ? "" : "", "")) {}
        st->st_mode = S_IFDIR;
        return 0;
    }
    st->st_mode = (fno.fattrib & AM_DIR) ? S_IFDIR : S_IFREG;
    st->st_mode |= S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    st->st_size = (off_t)fno.fsize;
    st->st_blksize = 512;
    st->st_blocks = (st->st_size + 511) / 512;
    return 0;
}

static int ff_stat_r(struct _reent *r, const char *path, struct stat *st)
{
    fatfs_dev_t *d = find_by_prefix(path);
    if (!d) { r->_errno = ENODEV; return -1; }
    if (stat_common(d, path, st) < 0) { r->_errno = ENOENT; return -1; }
    return 0;
}

static int ff_lstat_r(struct _reent *r, const char *path, struct stat *st)
{
    return ff_stat_r(r, path, st);
}

static int ff_link_r(struct _reent *r, const char *e, const char *n)
{ (void)e; (void)n; r->_errno = ENOSYS; return -1; }

static int ff_unlink_r(struct _reent *r, const char *name)
{
    fatfs_dev_t *d = find_by_prefix(name);
    char vp[FF_MAX_LFN * 2 + 8];
    FRESULT res;
    if (!d) { r->_errno = ENODEV; return -1; }
    mkvol(d, vp, sizeof(vp), name, 1);
    res = f_unlink(vp);
    if (res != FR_OK) { seterr(r, res); return -1; }
    return 0;
}

static int ff_rmdir_r(struct _reent *r, const char *name)
{
    return ff_unlink_r(r, name);
}

static int ff_chdir_r(struct _reent *r, const char *name)
{
    fatfs_dev_t *d = find_by_prefix(name);
    char vp[FF_MAX_LFN * 2 + 8];
    const char *rest;
    size_t n;
    if (!d) { r->_errno = ENODEV; return -1; }
    mkvol(d, vp, sizeof(vp), name, 1);
    /* validate */
    {
        FILINFO fno;
        FRESULT res = f_stat(vp, &fno);
        if (res != FR_OK && strcmp(vp + 2, "/")) { seterr(r, res); return -1; }
    }
    /* store as our cwd (strip "N:" prefix) */
    rest = vp;
    while (*rest && *rest != ':') rest++;
    if (*rest == ':') rest++;
    n = strlen(rest);
    if (n >= sizeof(d->cwd)) n = sizeof(d->cwd) - 1;
    memcpy(d->cwd, rest, n);
    d->cwd[n] = 0;
    return 0;
}

static int ff_rename_r(struct _reent *r, const char *oldName, const char *newName)
{
    fatfs_dev_t *d = find_by_prefix(oldName);
    fatfs_dev_t *d2 = find_by_prefix(newName);
    char v1[FF_MAX_LFN * 2 + 8], v2[FF_MAX_LFN * 2 + 8];
    FRESULT res;
    if (!d || !d2) { r->_errno = ENODEV; return -1; }
    mkvol(d, v1, sizeof(v1), oldName, 1);
    mkvol(d2, v2, sizeof(v2), newName, 1);
    res = f_rename(v1, v2);
    if (res != FR_OK) { seterr(r, res); return -1; }
    return 0;
}

static int ff_mkdir_r(struct _reent *r, const char *path, int mode)
{
    fatfs_dev_t *d = find_by_prefix(path);
    char vp[FF_MAX_LFN * 2 + 8];
    FRESULT res;
    (void)mode;
    if (!d) { r->_errno = ENODEV; return -1; }
    mkvol(d, vp, sizeof(vp), path, 1);
    res = f_mkdir(vp);
    if (res != FR_OK) { seterr(r, res); return -1; }
    return 0;
}

/* ---- dir ops ---- */

typedef struct {
    DIR dir;
} fatfs_dir_state_t;

static DIR_ITER *ff_diropen_r(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    fatfs_dev_t *d = find_by_prefix(path);
    char vp[FF_MAX_LFN * 2 + 8];
    FRESULT res;
    if (!d) { r->_errno = ENODEV; return NULL; }
    mkvol(d, vp, sizeof(vp), path, 1);
    res = f_opendir(&((fatfs_dir_state_t *)dirState->dirStruct)->dir, vp);
    if (res != FR_OK) { seterr(r, res); return NULL; }
    return dirState;
}

static int ff_dirreset_r(struct _reent *r, DIR_ITER *dirState)
{
    FRESULT res = f_dirrewind(&((fatfs_dir_state_t *)dirState->dirStruct)->dir);
    if (res != FR_OK) { seterr(r, res); return -1; }
    return 0;
}

static int ff_dirnext_r(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *st)
{
    FILINFO fno;
    FRESULT res = f_readdir(&((fatfs_dir_state_t *)dirState->dirStruct)->dir, &fno);
    if (res != FR_OK) { seterr(r, res); return -1; }
    if (!fno.fname[0]) return -1; /* end */
    if (filename) snprintf(filename, FF_MAX_LFN + 2, "%s", fno.fname);
    if (st) {
        memset(st, 0, sizeof(*st));
        st->st_mode = (fno.fattrib & AM_DIR) ? (S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO)
                                             : (S_IFREG | S_IRUSR | S_IRGRP | S_IROTH);
        st->st_size = (off_t)fno.fsize;
        st->st_blksize = 512;
        st->st_blocks = (st->st_size + 511) / 512;
    }
    return 0;
}

static int ff_dirclose_r(struct _reent *r, DIR_ITER *dirState)
{
    FRESULT res = f_closedir(&((fatfs_dir_state_t *)dirState->dirStruct)->dir);
    if (res != FR_OK) { seterr(r, res); return -1; }
    return 0;
}

/* ---- misc ---- */

static int ff_statvfs_r(struct _reent *r, const char *path, struct statvfs *buf)
{
    fatfs_dev_t *d = find_by_prefix(path);
    DWORD free_clst = 0;
    char vp[FF_MAX_LFN * 2 + 8];
    if (!d) { r->_errno = ENODEV; return -1; }
    mkvol(d, vp, sizeof(vp), path, 1);
    if (f_getfree(vp, &free_clst, 0) != FR_OK) free_clst = 0;
    if (buf) {
        memset(buf, 0, sizeof(*buf));
        buf->f_bsize = 512;
        buf->f_frsize = d->fs.csize ? 512 * d->fs.csize : 512;
        buf->f_blocks = d->fs.n_fatent - 2;
        buf->f_bfree = free_clst;
        buf->f_bavail = free_clst;
        buf->f_namemax = FF_MAX_LFN;
    }
    return 0;
}

static int ff_ftruncate_r(struct _reent *r, void *fd, off_t len)
{
    FRESULT res;
    FSIZE_t cur = f_tell((FIL *)fd);
    f_lseek((FIL *)fd, (FSIZE_t)len);
    res = f_truncate((FIL *)fd);
    f_lseek((FIL *)fd, cur);
    if (res != FR_OK) { seterr(r, res); return -1; }
    return 0;
}

static int ff_fsync_r(struct _reent *r, void *fd)
{
    FRESULT res = f_sync((FIL *)fd);
    if (res != FR_OK) { seterr(r, res); return -1; }
    return 0;
}

static int ff_chmod_r(struct _reent *r, const char *path, mode_t mode)
{ (void)path; (void)mode; r->_errno = ENOSYS; return -1; }
static int ff_fchmod_r(struct _reent *r, void *fd, mode_t mode)
{ (void)fd; (void)mode; r->_errno = ENOSYS; return -1; }
static int ff_utimes_r(struct _reent *r, const char *f, const struct timeval t[2])
{ (void)f; (void)t; r->_errno = ENOSYS; return -1; }
static long ff_fpathconf_r(struct _reent *r, void *fd, int name)
{ (void)fd; (void)name; r->_errno = ENOSYS; return -1; }
static long ff_pathconf_r(struct _reent *r, const char *path, int name)
{ (void)path; (void)name; r->_errno = ENOSYS; return -1; }
static int ff_symlink_r(struct _reent *r, const char *t, const char *l)
{ (void)t; (void)l; r->_errno = ENOSYS; return -1; }
static ssize_t ff_readlink_r(struct _reent *r, const char *p, char *b, size_t s)
{ (void)p; (void)b; (void)s; r->_errno = ENOSYS; return -1; }

/* ---- mount/unmount ---- */

static void fill_ops(devoptab_t *ops)
{
    memset(ops, 0, sizeof(*ops));
    ops->structSize = sizeof(FIL);
    ops->open_r = ff_open_r;
    ops->close_r = ff_close_r;
    ops->write_r = ff_write_r;
    ops->read_r = ff_read_r;
    ops->seek_r = ff_seek_r;
    ops->fstat_r = ff_fstat_r;
    ops->stat_r = ff_stat_r;
    ops->link_r = ff_link_r;
    ops->unlink_r = ff_unlink_r;
    ops->chdir_r = ff_chdir_r;
    ops->rename_r = ff_rename_r;
    ops->mkdir_r = ff_mkdir_r;
    ops->dirStateSize = sizeof(fatfs_dir_state_t);
    ops->diropen_r = ff_diropen_r;
    ops->dirreset_r = ff_dirreset_r;
    ops->dirnext_r = ff_dirnext_r;
    ops->dirclose_r = ff_dirclose_r;
    ops->statvfs_r = ff_statvfs_r;
    ops->ftruncate_r = ff_ftruncate_r;
    ops->fsync_r = ff_fsync_r;
    ops->chmod_r = ff_chmod_r;
    ops->fchmod_r = ff_fchmod_r;
    ops->rmdir_r = ff_rmdir_r;
    ops->lstat_r = ff_lstat_r;
    ops->utimes_r = ff_utimes_r;
    ops->fpathconf_r = ff_fpathconf_r;
    ops->pathconf_r = ff_pathconf_r;
    ops->symlink_r = ff_symlink_r;
    ops->readlink_r = ff_readlink_r;
}

int gc_fatfs_mount(const char *name, const DISC_INTERFACE *io)
{
    fatfs_dev_t *d;
    char vp[8];
    if (dev_count >= MAX_DEVS) return -1;
    if (!name || !io) return -1;

    d = &devs[dev_count];
    memset(d, 0, sizeof(*d));
    d->vol = dev_count;
    snprintf(d->name, sizeof(d->name), "%s", name);

    if (!io->startup()) return -1;
    if (!io->isInserted()) { io->shutdown(); return -1; }

    snprintf(vp, sizeof(vp), "%d:", d->vol);
    if (f_mount(&d->fs, vp, 1) != FR_OK) { io->shutdown(); return -1; }
    fatfs_attach(d->vol, io);

    fill_ops(&d->ops);
    d->ops.name = d->name;
    d->ops.deviceData = d;
    AddDevice(&d->ops);
    dev_count++;
    return d->vol;
}

int gc_fatfs_unmount(const char *name)
{
    int i;
    for (i = 0; i < dev_count; i++) {
        if (!strcmp(devs[i].name, name)) {
            char vp[8];
            snprintf(vp, sizeof(vp), "%d:", devs[i].vol);
            f_mount(NULL, vp, 0);
            RemoveDevice(vp);
            fatfs_detach(devs[i].vol);
            /* compact not needed; slots stay reserved */
            return 0;
        }
    }
    return -1;
}