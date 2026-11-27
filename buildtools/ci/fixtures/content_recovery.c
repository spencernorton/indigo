/* Removing either name of a cross-linked FAT chain frees the shared data.
 * Model chains separately from directory entries, including rename failures
 * that leave both names or change the caller's source handle. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sha256.h"
#include "ui_content.h"
#include "result.inc"

enum { IS_FILE, IS_DIR, IS_SPECIAL };
enum { FR_DISK_ERR = 1, FR_NO_FILE = 4, FR_NO_PATH = 5, ATTRIB_READONLY = 1 };
enum { UPDATE_CHUNK = 2, LIVE, PART, OLD, NAMES };
typedef int32_t s32;
typedef struct device DEVICEHANDLER_INTERFACE;
typedef struct {
	char name[48];
	uint32_t fileType, fileAttrib, size, offset, fileBase;
	DEVICEHANDLER_INTERFACE *device;
} file_handle;
struct device {
	int (*statFile)(file_handle *);
	int (*deleteFile)(file_handle *);
	int (*renameFile)(file_handle *, char *);
	int (*readFile)(file_handle *, void *, uint32_t);
	int (*closeFile)(file_handle *);
};
typedef struct { struct { char *chunk; } source; } update_check_t;

static const char *const names[NAMES] = {
	[LIVE] = "swiss/ui/features.txt",
	[PART] = "swiss/ui/features.txt.part",
	[OLD] = "swiss/ui/features.txt.indigo-old"
};
typedef struct { bool present; unsigned cluster, type, attrs; } entry;
static entry files[NAMES];
static struct { char byte; uint32_t size; bool allocated; } chains[8];
static int fail_stat, fail_read, fail_erase, fail_move, alias_move, mutations;
static unsigned bad_payload;
static uint32_t capacity;

static int slot(const char *name)
{
	for(int i = LIVE; i < NAMES; ++i)
		if(!strcmp(name, names[i])) return i;
	assert(0); return 0;
}
static void put(int name, unsigned cluster, char byte, uint32_t size)
{
	assert(cluster < 8 && (cluster || size == 0));
	files[name] = (entry){true, cluster, IS_FILE, 0};
	chains[cluster].byte = byte;
	chains[cluster].size = size;
	chains[cluster].allocated = true;
}
static uint32_t free_space(void)
{
	uint32_t used = 0;
	for(size_t i = 0; i < 8; ++i)
		if(chains[i].allocated) used += chains[i].size;
	assert(used <= capacity);
	return capacity - used;
}
static void reset(void)
{
	memset(files, 0, sizeof(files));
	memset(chains, 0, sizeof(chains));
	fail_stat = fail_read = fail_erase = fail_move = alias_move = -1;
	bad_payload = mutations = 0;
	capacity = 100;
}
static void contentFile(file_handle *f, DEVICEHANDLER_INTERFACE *d, const char *path, const char *suffix)
{
	*f = (file_handle){.device = d};
	snprintf(f->name, sizeof(f->name), "%s%s", path, suffix);
}
static int deviceHandler_FAT_statFile(file_handle *f)
{
	int idx = slot(f->name);
	entry e = files[idx];
	if(idx == fail_stat) return FR_DISK_ERR;
	if(!e.present) return FR_NO_FILE;
	f->fileType = e.type;
	f->fileAttrib = e.attrs;
	f->fileBase = e.cluster;
	f->size = chains[e.cluster].size;
	return 0;
}
static int erase(file_handle *f)
{
	int idx = slot(f->name);
	entry e = files[idx];
	if(idx == fail_erase || !e.present || (e.attrs & ATTRIB_READONLY)) return -1;
	/* FatFs also accepts an empty directory here. */
	if(e.cluster) chains[e.cluster].allocated = false;
	files[idx].present = false;
	++mutations;
	return 0;
}
static int move(file_handle *from, char *name)
{
	int src = slot(from->name), dest = slot(name);
	if(!files[src].present || files[dest].present) return -1;
	if(src == fail_move) {
		strcpy(from->name, name);
		return -1;
	}
	files[dest] = files[src];
	++mutations;
	if(src == alias_move) return FR_DISK_ERR;
	files[src].present = false;
	strcpy(from->name, name);
	return 0;
}
static int read_chunk(file_handle *f, void *buf, uint32_t count)
{
	int idx = slot(f->name);
	unsigned c = files[idx].cluster;
	if(idx == fail_read || !files[idx].present || !chains[c].allocated ||
		f->offset + count > chains[c].size) return -1;
	memset(buf, chains[c].byte, count);
	f->offset += count;
	return (int)count;
}
static int close_file(file_handle *f) { (void)f; return 0; }
static update_result_t fetchPayload(update_check_t *check, const char *name, file_handle *f,
	uint32_t size, const uint8_t digest[32], const char *message)
{
	(void)check; (void)name; (void)digest; (void)message;
	assert(slot(f->name) == PART && !files[PART].present);
	if(free_space() < size) return UPDATE_NO_SPACE;
	put(PART, 4, bad_payload ? 'x' : 'n', size);
	++mutations;
	f->offset = size;
	return UPDATE_OK;
}
#include "transactions.inc"

int main(void)
{
	DEVICEHANDLER_INTERFACE d = {deviceHandler_FAT_statFile, erase, move, read_chunk, close_file};
	ui_content_file_t item = {.size = 4};
	strcpy(item.path, names[LIVE]);
	sha256_ctx sha;
	sha256_init(&sha); sha256_update(&sha, (const uint8_t *)"nnnn", 4); sha256_final(&sha, item.sha256);
	char chunk[UPDATE_CHUNK];
	update_check_t check = {.source = {.chunk = chunk}};
	bool installed;

	/* All three possible cross-links, including the second rename with a
	 * distinct old copy. Reading the volume must never free any chain. */
	for(int a = LIVE; a < NAMES; ++a) for(int b = a + 1; b < NAMES; ++b) {
		reset(); put(a, 2, 'n', 4); files[b] = files[a];
		assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_CONTENT_CONFLICT);
		assert(contentFetch(&check, &d, &item, "") == UPDATE_CONTENT_CONFLICT);
		assert(mutations == 0 && chains[2].allocated && files[a].present && files[b].present);
	}
	reset(); put(LIVE, 0, 0, 0); put(PART, 0, 0, 0);
	assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_OK && !installed);

	/* Reclaim staging before checking capacity, even with zero bytes free. */
	reset(); capacity = 100; put(LIVE, 1, 'o', 30); put(PART, 2, 'x', 70);
	assert(free_space() == 0);
	assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_OK && !installed);
	assert(free_space() == 70 && files[LIVE].cluster == 1);
	assert(contentFetch(&check, &d, &item, "") == UPDATE_OK);
	assert(files[LIVE].cluster == 4 && !files[PART].present && !files[OLD].present);

	/* Post-publish cleanup can fail. Only the authenticated live copy
	 * authorizes deleting the previous file on a subsequent check. */
	reset(); put(LIVE, 1, 'o', 30); fail_erase = OLD;
	assert(contentFetch(&check, &d, &item, "") == UPDATE_WRITE_FAIL);
	assert(files[LIVE].cluster == 4 && files[OLD].cluster == 1);
	assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_WRITE_FAIL);
	fail_erase = -1;
	assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_OK && installed);
	assert(!files[OLD].present && free_space() == 96);
	reset(); put(LIVE, 1, 'x', 4); put(OLD, 2, 'o', 15);
	assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_CONTENT_CONFLICT && !mutations);

	/* Protect read-only originals, directories, unstatable entries and
	 * unreadable data. FAT rename alone would allow moving a read-only file. */
	reset(); put(LIVE, 1, 'o', 4); files[LIVE].attrs = ATTRIB_READONLY; put(PART, 2, 'p', 1);
	assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_CONTENT_CONFLICT && !mutations);
	assert(contentFetch(&check, &d, &item, "") == UPDATE_CONTENT_CONFLICT && !mutations);
	reset(); put(LIVE, 1, 'n', 4); files[LIVE].attrs = ATTRIB_READONLY;
	assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_OK && installed && !mutations);
	for(int i = LIVE; i < NAMES; ++i) {
		reset(); put(i, 1, 'd', 4); files[i].type = IS_DIR; fail_stat = i;
		assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_READ_FAIL && !mutations);
		fail_stat = -1;
		assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_CONTENT_CONFLICT && !mutations);
	}
	reset(); put(LIVE, 1, 'n', 4); put(OLD, 2, 'o', 4); fail_read = LIVE;
	assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_READ_FAIL && !mutations);

	/* Failed transfers and card readback never replace the original. */
	reset(); put(LIVE, 1, 'o', 4); bad_payload = 1;
	assert(contentFetch(&check, &d, &item, "") == UPDATE_READ_FAIL);
	assert(files[LIVE].cluster == 1 && files[PART].present && !files[OLD].present);
	assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_OK && !installed);
	assert(!files[PART].present);

	reset(); put(LIVE, 1, 'o', 4); fail_move = PART;
	assert(contentFetch(&check, &d, &item, "") == UPDATE_WRITE_FAIL);
	assert(files[LIVE].cluster == 1 && !files[OLD].present && !files[PART].present);
	reset(); put(OLD, 1, 'o', 4); put(PART, 2, 'n', 4); fail_move = OLD;
	assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_WRITE_FAIL && !mutations);
	fail_move = -1;
	assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_OK && !installed);
	assert(files[LIVE].cluster == 1 && !files[OLD].present && !files[PART].present);
	for(int step = LIVE; step <= PART; ++step) {
		reset(); put(LIVE, 1, 'o', 4); alias_move = step;
		assert(contentFetch(&check, &d, &item, "") == UPDATE_WRITE_FAIL);
		int before = mutations;
		assert(contentPrepare(&d, &item, chunk, &installed) == UPDATE_CONTENT_CONFLICT);
		assert(mutations == before && chains[1].allocated && chains[4].allocated);
	}
	return 0;
}
