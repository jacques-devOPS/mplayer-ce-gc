/*
 * GameCube main-thread stack size.
 *
 * The original mplayer.ogc.ld reserved a 512 KB (0x80000) main stack.
 * Current libogc reserves 128 KB in tuxedo/default_stacks.c and exposes
 * the stack top through the weak pointer __ppc_main_sp. This strong
 * definition restores the 512 KB stack.
 *
 * __app_start (tuxedo/common_crt0.S) loads __ppc_main_sp via @sda21,
 * so the pointer must reside in .sdata.
 */
#include <gctypes.h>

static u8 gc_main_stack[0x80000] __attribute__((aligned(8)));

void *__ppc_main_sp __attribute__((section(".sdata"))) =
	&gc_main_stack[sizeof(gc_main_stack)];
