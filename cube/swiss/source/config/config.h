#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
	char game_id[4 + 1];
	char game_name[64 + 1];
	char comment[128 + 1];
	char status[32 + 1];
	char region;
	int gameLanguage;
	int gameVMode;
	int forceHScale;
	short forceVOffset;
	int forceVFilter;
	int forceVJitter;
	int fixPixelCenter;
	int disableDithering;
	int forceAnisotropy;
	int forceWidescreen;
	int forcePollRate;
	int invertCStick;
	int swapCStick;
	int triggerLevel;
	int emulateAudioStream;
	int emulateReadSpeed;
	int emulateEthernet;
	int disableMemoryCard;
	int disableHypervisor;
	int forceCleanBoot;
	int preferCleanBoot;
	int rt4kProfile;
} ConfigEntry;

/* Last-played tracks completed Swiss handoffs, not browsing or play duration. */
uint64_t config_last_played(const char *gameId, size_t gameIdLength, bool *available);
void config_record_game_handoff(const char *gameId, size_t gameIdLength);

void config_find(ConfigEntry *entry);
void config_parse_game(char *configData, ConfigEntry *entry);
int config_each_game_file(void (*visit)(const char *gameId, char *text, void *context),
	void *context);
void config_defaults(ConfigEntry *entry);
int config_update_game(ConfigEntry *entry, ConfigEntry *defaults, bool checkConfigDevice);
/* Mounts the settings device unless it's the source; config_unset_device
 * lets it go again. */
bool config_set_device(void);
/* Writes a whole file on the settings device (NAME.new, then renamed). */
int config_file_write(char *filename, char *contents);
void config_unset_device(void);
int config_update_global(bool checkConfigDevice);
/* Turns Library Folders on or off. While it is on, FlattenDir is its
 * pattern and the one it replaced is kept to save and to put back. */
void config_set_library_folders(bool on);
/* Memory Cards folder-cube appearance: the whole device-prefixed path identifies it.
 * Saving rolls memory back on failure and preserves other global settings. */
uint8_t config_folder_color(const char *path);
bool config_set_folder_color(const char *path, uint8_t color, bool checkConfigDevice);
int config_update_autoload(bool checkConfigDevice);
int config_update_spin_discs(bool checkConfigDevice);
bool config_global_file_loaded(void);
int config_update_recent(bool checkConfigDevice);
int config_init(void (*progress_indicator)(char*, int, int));
void config_init_environ();
void config_update_environ();
void config_parse_args(int argc, char *argv[]);
void config_load_current(ConfigEntry *entry);
void config_unload_current();

#endif

