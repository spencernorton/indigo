/* Target-only regeneration utility; never linked into Indigo. */
#include <gccore.h>
#include <malloc.h>
#include <ogc/lwp_watchdog.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define UI_JEWEL_CASE_REFERENCE 1
#include "ui_jewel_case.c"
#define EXI0 ((vu32 *)0xCC006800)
static void exi_write(u32 data, int bytes)
{
	EXI0[4] = data;
	EXI0[3] = 1 | 1 << 2 | (bytes - 1) << 4;
	while(EXI0[3] & 1) { }
}
static void uart(const char *text)
{
	EXI0[0] = 1 << 8 | 3 << 4;
	exi_write(0xA0010000, 4);
	for(size_t i = 0; text[i];) {
		u32 data = 0;int bytes = 0;
		for(; bytes < 4 && text[i]; ++bytes, ++i)
			data |= (u32)(u8)text[i] << (24 - 8 * bytes);
		exi_write(data, bytes);
	}
	EXI0[0] = 0;
}
void NativeJewelLog(const char *format, ...)
{
	char line[256];va_list args;va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);va_end(args);uart(line);
}
int main(void)
{
 VIDEO_Init();
 void *fifo = memalign(32, 256 * 1024);
 if(!fifo) return 1;
 GX_Init(fifo, 256 * 1024);
 if(!UIJewelCase_Prepare()) { uart("matcap FAILED\r"); return 1; }
 for(unsigned pos = 0; pos < 21760; pos += 64) {
  char hex[129]; unsigned n = 21760 - pos < 64 ? 21760 - pos : 64;
  for(unsigned i = 0; i < n; i++) {
   hex[2*i] = "0123456789abcdef"[matcapData[pos+i] >> 4];
   hex[2*i+1] = "0123456789abcdef"[matcapData[pos+i] & 15];
  }
  hex[n*2] = 0;
  NativeJewelLog("matcap %u %s\r", pos, hex);
 }
 uart("matcap DONE\r");
 for(;;) usleep(1000000);
}
