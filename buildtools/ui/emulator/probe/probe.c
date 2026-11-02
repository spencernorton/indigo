/* probe.c -- the program the emulator test launches, as a game and as an app.
 *
 * It reports what Indigo's hand-off left it: the boot values in low memory,
 * whether the menu music's audio DMA is still running, whether anything is
 * still writing to memory, and the path an app was started with. It reads
 * them in __SYS_PreInit, before libogc touches any of them, then turns the
 * screen a solid signature colour and draws the results as a strip of black
 * and white blocks, 32 to a row and one row per word, so a picture of the
 * screen is all it takes to read them back (run.py's probe_report). Each
 * step is also written to the debug UART, which Dolphin logs.
 *
 * Nothing in it depends on Dolphin: on a console it shows the same screen.
 */
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "iconv_check.h"
#include "unwind_check.h"

#define PROBE_MAGIC 0x1D160B0Eu
#define PROBE_VERSION 2u

/* The words in the strip, top to bottom. run.py's PROBE_WORDS names them. */
enum {
	W_MAGIC, W_VERSION, W_ID0, W_ID1, W_MEMSIZE, W_CONSOLE, W_VIDEO, W_BUS,
	W_CORE, W_ARENA_LO, W_ARENA_HI, W_TOP, W_AI_DMA, W_AI_CR, W_STRAY, W_ARGC,
	W_ARGV0, W_ARGV1, W_ARGV2, W_ARGV3, W_VI_DCR, W_VI_CLK_DTV, W_CRC, W_COUNT
};

static u32 word[W_COUNT];

/* The debug UART on the IPL chip (EXI channel 0, device 1), through the
 * registers, so it works before libogc is up. Dolphin logs each line that
 * ends in '\r'; a console has nothing listening. */
#define EXI0 ((vu32 *)0xCC006800)

static void exi_write(u32 data, int bytes)
{
	EXI0[4] = data;
	EXI0[3] = 1 | 1 << 2 | (bytes - 1) << 4;
	while (EXI0[3] & 1)
		;
}

static void uart(const char *text)
{
	EXI0[0] = 1 << 8 | 3 << 4;
	exi_write(0xA0010000, 4);
	for (size_t i = 0; text[i];) {
		u32 data = 0;
		int bytes = 0;
		for (; bytes < 4 && text[i]; bytes++, i++)
			data |= (u32)(u8)text[i] << (24 - 8 * bytes);
		exi_write(data, bytes);
	}
	EXI0[0] = 0;
}

static u32 crc32(const u32 *words, int count)
{
	u32 crc = 0xFFFFFFFF;
	for (int i = 0; i < count; i++)
		for (int shift = 24; shift >= 0; shift -= 8) {
			crc ^= (words[i] >> shift) & 0xFF;
			for (int k = 0; k < 8; k++)
				crc = crc >> 1 ^ (0xEDB88320 & -(crc & 1));
		}
	return ~crc;
}

/* libogc calls this first in SYS_Init, before it resets the DSP or touches
 * the EXI bus and low memory: what is read here is what the hand-off left. */
void __SYS_PreInit(void)
{
	uart("probe: started\r");
	word[W_ID0] = *(vu32 *)0x80000000;
	word[W_ID1] = *(vu32 *)0x80000004;
	word[W_MEMSIZE] = *(vu32 *)0x80000028;
	word[W_CONSOLE] = *(vu32 *)0x8000002C;
	word[W_ARENA_LO] = *(vu32 *)0x80000030;
	word[W_ARENA_HI] = *(vu32 *)0x80000034;
	word[W_VIDEO] = *(vu32 *)0x800000CC;
	word[W_TOP] = *(vu32 *)0x800000EC;
	word[W_BUS] = *(vu32 *)0x800000F8;
	word[W_CORE] = *(vu32 *)0x800000FC;
	word[W_AI_DMA] = *(vu16 *)0xCC005036;
	word[W_AI_CR] = *(vu32 *)0xCC006C00;
	/* The video mode the screen was left in: the display configuration
	 * (interlaced or not, the TV format), and the 27/54 MHz clock with the
	 * component cable bit beside it. */
	word[W_VI_DCR] = *(vu16 *)0xCC002002;
	word[W_VI_CLK_DTV] = (u32)*(vu16 *)0xCC00206C << 16 | *(vu16 *)0xCC00206E;
}

/* A running DMA, or a thread the hand-off failed to stop, changes memory
 * nobody owns any more: fill a large block, wait a second and a half, and
 * count the words that changed. */
static u32 stray_writes(void)
{
	const size_t words = (8 << 20) / 4;
	u32 *block = malloc(words * 4);
	if (!block)
		return 0xFFFFFFFF;
	for (size_t i = 0; i < words; i++)
		block[i] = 0xA5A50000 ^ (u32)i;
	DCFlushRange(block, words * 4);
	for (int frame = 0; frame < 90; frame++)
		VIDEO_WaitVSync();
	DCInvalidateRange(block, words * 4);
	u32 changed = 0;
	for (size_t i = 0; i < words; i++)
		changed += block[i] != (0xA5A50000 ^ (u32)i);
	free(block);
	return changed;
}

/* YUYV, two pixels to a word. */
#define AZURE 0x69CB693F
#define WHITE 0xEB80EB80
#define BLACK 0x10801080
#include "probe_grid.h"

static void fill(u32 *xfb, int stride, int x, int y, int w, int h, u32 colour)
{
	for (int row = y; row < y + h; row++)
		for (int col = x / 2; col < (x + w) / 2; col++)
			xfb[row * stride + col] = colour;
}

int main(int argc, char **argv)
{
	uart("probe: main\r");
	if (!iconv_check()) {
		uart("probe: iconv FAILED\r");
		return 1;
	}
	uart("probe: iconv passed\r");
	if (!unwind_check()) {
		uart("probe: unwind FAILED\r");
		return 1;
	}
	uart("probe: unwind passed\r");
	word[W_MAGIC] = PROBE_MAGIC;
	word[W_VERSION] = PROBE_VERSION;
	word[W_ARGC] = (u32)argc;
	if (argc > 0 && argv && argv[0]) {
		/* The last 16 bytes of the path the program was started with. */
		const char *path = argv[0];
		size_t length = strlen(path);
		const char *tail = path + (length > 16 ? length - 16 : 0);
		for (int i = 0; tail[i] && i < 16; i++)
			word[W_ARGV0 + i / 4] |= (u32)(u8)tail[i] << (24 - 8 * (i % 4));
	}

	VIDEO_Init();
	GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
	u32 *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
	int stride = mode->fbWidth / 2;
	console_init(xfb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
	VIDEO_Configure(mode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (mode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
	uart("probe: video\r");

	word[W_STRAY] = stray_writes();
	word[W_CRC] = crc32(word, W_CRC);

	char path[17] = {0};
	for (int i = 0; i < 16; i++)
		path[i] = (char)(word[W_ARGV0 + i / 4] >> (24 - 8 * (i % 4)));
	printf(" Indigo probe %u: the launch reached this program.\n", PROBE_VERSION);
	printf(" disc %.6s  memory %08X  video %u  console %08X\n", (const char *)&word[W_ID0],
	       word[W_MEMSIZE], word[W_VIDEO], word[W_CONSOLE]);
	printf(" audio DMA %04X/%08X  stray %u  arena %08X-%08X top %08X\n", word[W_AI_DMA], word[W_AI_CR],
	       word[W_STRAY], word[W_ARENA_LO], word[W_ARENA_HI], word[W_TOP]);
	printf(" VI %04X %08X  argc %u %.16s\n", word[W_VI_DCR], word[W_VI_CLK_DTV], word[W_ARGC], path);
	fill(xfb, stride, 0, 88, mode->fbWidth, mode->xfbHeight - 88, AZURE);
	probeGrid_t grid;
	if(!ProbeGrid_Layout(mode->fbWidth, mode->xfbHeight, W_COUNT, &grid)) {
		uart("probe: framebuffer too small for report\r");
		return 1;
	}
	for (int w = 0; w < W_COUNT; w++)
		for (int b = 0; b < 32; b++)
			fill(xfb, stride, grid.x + b * grid.cell_width, grid.y + w * grid.cell_height,
			     grid.cell_width, grid.cell_height,
			     word[w] >> (31 - b) & 1 ? WHITE : BLACK);
	uart("probe: reported\r");

	for (;;)
		VIDEO_WaitVSync();
}
