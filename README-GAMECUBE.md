# MPlayer CE GameCube Port

This repository derives from [extremscorner/mplayer-ce](https://github.com/extremscorner/mplayer-ce).
It builds the GameCube target with current devkitPro packages.

## Build environment

Verified environment:

- devkitPPC r50, GCC 16.1.0
- libogc 3.1.0
- `elf2dol` on PATH
- ppc-freetype 2.14.1, ppc-fribidi, ppc-libiconv

Build:

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=/opt/devkitpro/devkitPPC
export PATH="$DEVKITPRO/tools/bin:$PATH"
cd mplayer && make gc-build
```

`gc-build` never runs `make clean`; tracked generated tables must not be
regenerated with host rounding.

## Port changes from upstream

1. Build: GameCube config selected (not Wii); `-mpaired`/`-mstring`
   removed (unsupported by GCC 16); explicit `-msdata` dropped for the
   devkitPPC `-msdata=data` default; `-fpermissive` and `-fcommon` added
   for legacy C; paired-single FFmpeg optimizations disabled.
2. Link: freetype2/zlib/PNG/bzip2/Brotli/fribidi/libiconv portlibs;
   stock devkitPPC GameCube linker setup replaces the 2011 specs.
3. libogc API: `if_config()` retries argument; TV mode symbol rename;
   `poll.h` from libogc; `poll`→`net_poll` macro removed; DNS and
   socket-name stubs added (no resolver on cube BBA); GEKKO connect
   rewritten as one blocking `net_connect()` because `net_fcntl()` is
   a stub on cube libogc.
4. OS surfaces: Wii-only NTFS/ext2 includes guarded with `HW_RVL`;
   `DCBlockFlush()` replaced by `DCFlushRange()`; 512 KiB GameCube
   main stack restored via `__ppc_main_sp`.
5. Storage: device table (`carda:`, `cardb:`, `sd2:` via `__io_gcsd2`),
   per-device `startup/inserted/mounted` boot diagnostics, config dir
   from the launcher's `argv[0]`.
6. Menu: SD2SP2 and SMB entries in file/playlist/subtitle selectors.

## Filesystem requirement (application path, not hardware)

SD2SP2 hardware is not limited to FAT32. Swiss reads exFAT fine. The
**MPlayer CE / libfat path requires FAT32**: an exFAT card reports
`mounted=0` in the boot diagnostics. Format the card used by MPlayer
CE as FAT32. exFAT support is future work.

## HTTP playback on GameCube (BBA)

Verified profile (Jellyfin-transcoded and static-serve tested):

```
container MPEG-TS | video MPEG-2 4:2:0 <=320x240 1 Mbps 25 fps
audio MP2 128 Kbps 48 kHz stereo
```

### Cache limitation — `nocache=yes` required

The GameCube stream-cache path (cache2 ARAM staging with short-read
fragments) fails during HTTP playback: playback hangs at "Analysing
Stream...". **Local SD playback of the same content works, and HTTP
playback works with the cache disabled.** Until the cache path is
repaired, `mplayer.conf` must contain:

```ini
nocache=yes
```

Verified end-to-end (hardware): JellyCube → Jellyfin transcode →
HTTP playlist → playback with `nocache=yes`.

## Hardware test flow

1. FAT32 card; copy `apps/mplayer-ce/` tree; set `config.ini` credentials.
2. Swiss → `mplayer.dol`; confirm per-device mount diagnostics.
3. Local TS file test: Open → File → SD2SP2.
4. HTTP test: Open → Playlist → SD2SP2 → `mplayer/jellycube.m3u`.

The official `mplayer_r658.dol` is kept only as a comparison binary.
