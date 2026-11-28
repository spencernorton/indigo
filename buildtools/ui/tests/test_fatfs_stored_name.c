/* Exercise the console's FatFs lookup against real directory entries on a
 * small in-memory FAT volume, including a user-visible name and its alias. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"

enum { SECTOR_BYTES = 512, SECTORS = 4096 };
static BYTE media[SECTORS][SECTOR_BYTES];

DSTATUS disk_initialize(BYTE drive)
{
	return drive == DEV_RAM ? 0 : STA_NODISK;
}

DSTATUS disk_status(BYTE drive)
{
	return disk_initialize(drive);
}

DRESULT disk_shutdown(BYTE drive)
{
	return drive == DEV_RAM ? RES_OK : RES_NOTRDY;
}

static int validRange(BYTE drive, LBA_t sector, UINT count)
{
	return drive == DEV_RAM && sector < SECTORS && count <= SECTORS - sector;
}

DRESULT disk_read(BYTE drive, BYTE *out, LBA_t sector, UINT count, BYTE opt)
{
	(void)opt;
	if(!validRange(drive, sector, count)) return RES_PARERR;
	memcpy(out, media[sector], (size_t)count * SECTOR_BYTES);
	return RES_OK;
}

DRESULT disk_write(BYTE drive, const BYTE *in, LBA_t sector, UINT count, BYTE opt)
{
	(void)opt;
	if(!validRange(drive, sector, count)) return RES_PARERR;
	memcpy(media[sector], in, (size_t)count * SECTOR_BYTES);
	return RES_OK;
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *out)
{
	if(drive != DEV_RAM) return RES_NOTRDY;
	switch(command) {
		case CTRL_SYNC: return RES_OK;
		case GET_BLOCK_SIZE: *(DWORD *)out = 1; return RES_OK;
		case GET_SECTOR_SIZE: *(WORD *)out = SECTOR_BYTES; return RES_OK;
		case GET_SECTOR_COUNT: *(LBA_t *)out = SECTORS; return RES_OK;
		default: return RES_PARERR;
	}
}

/* One thread owns the entire test volume. */
int ff_mutex_create(int volume) { (void)volume; return 1; }
void ff_mutex_delete(int volume) { (void)volume; }
int ff_mutex_take(int volume) { (void)volume; return 1; }
void ff_mutex_give(int volume) { (void)volume; }
DWORD get_fattime(void) { return 0x5c210000; }

int main(void)
{
	BYTE scratch[FF_MAX_SS];
	MKFS_PARM format = { .fmt = FM_FAT | FM_SFD };
	FATFS volume;
	FATFS *matched;
	FFDIR directory;
	FILINFO listed;
	FILINFO result;
	char alias[sizeof("ram:/") + FF_SFN_BUF];
	int sawApps = 0;
	int sawLong = 0;

	assert(f_mkfs("ram:", &format, scratch, sizeof(scratch)) == FR_OK);
	assert(f_mount(&volume, "ram:", 1) == FR_OK);
	assert(f_mkdir("ram:/aPpS") == FR_OK);
	assert(f_mkdir("ram:/PrettyLongPrograms") == FR_OK);
	assert(f_chmod("ram:/aPpS", AM_HID, AM_HID) == FR_OK);

	assert(f_opendir(&directory, "ram:/") == FR_OK);
	do {
		assert(f_readdir(&directory, &listed) == FR_OK);
		if(strcmp(listed.fname, "aPpS") == 0) {
			assert(f_stat_stored("ram:/apps", &result, &matched) == FR_OK);
			assert(strcmp(result.fname, listed.fname) == 0);
			assert(result.fclust == listed.fclust && result.fattrib == listed.fattrib);
			assert((result.fattrib & (AM_DIR | AM_HID)) == (AM_DIR | AM_HID));
			sawApps = 1;
		}
		if(strcmp(listed.fname, "PrettyLongPrograms") == 0) {
			assert(listed.altname[0] != '\0' && strcmp(listed.altname, listed.fname) != 0);
			assert(snprintf(alias, sizeof(alias), "ram:/%s", listed.altname) < (int)sizeof(alias));
			assert(f_stat(alias, &result, &matched) == FR_OK);
			assert(f_stat_stored(alias, &result, &matched) == FR_OK);
			assert(strcmp(result.fname, listed.fname) == 0);
			assert(strcmp(result.fname, listed.altname) != 0);
			sawLong = 1;
		}
	} while(listed.fname[0] != '\0');
	assert(sawApps && sawLong);
	assert(f_closedir(&directory) == FR_OK);
	assert(f_unmount("ram:") == FR_OK);
	puts("FatFs stored directory names match enumeration, including lookup by short alias");
	return 0;
}
