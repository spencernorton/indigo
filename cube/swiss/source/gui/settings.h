#ifndef SETTINGS_H_
#define SETTINGS_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include "config.h"
#include "deviceHandler.h"

enum PAGES {
	PAGE_GLOBAL=0,
	PAGE_INTERFACE,
	PAGE_NETWORK,
	PAGE_GAME_GLOBAL,
	PAGE_GAME_DEFAULTS,
	PAGE_GAME
};
#define PAGE_MIN (PAGE_GLOBAL)
#define PAGE_MAX (PAGE_GAME)
/* What Settings shows. The PAGE_ and SET_ enums below stay
 * the value layer: every view row names one (page, option) pair. */
enum SETTINGS_VIEWS {
	VIEW_QUICK=0,       /* tabs */
	VIEW_GAME_DEFAULTS,
	VIEW_SETUP,
	VIEW_DISPLAY,       /* Setup sections */
	VIEW_CONSOLE,
	VIEW_STORAGE,
	VIEW_NETWORK,
	VIEW_LIBRARY,
	VIEW_DEVELOPER,
	VIEW_GAME           /* one game's own settings, opened from that game */
};

enum SETTINGS_GLOBAL {
	SET_SYS_BOOTMODE=0,
	SET_SYS_SOUND,
	SET_SYS_VIDEO,
	SET_SCREEN_POS,
	SET_SYS_LANG,
	SET_CONFIG_DEV,
	SET_LIBRARY_PATH,
	SET_SWISS_VIDEOMODE,
	SET_APPS_VIDEOMODE,
	SET_INIT_DRIVE,
	SET_STOP_MOTOR,
	SET_AUDIO_BUFFER,
	SET_EXI_SPEED,
	SET_AVE_COMPAT,
	SET_FORCE_DTVSTATUS,
	SET_RT4K_OPTIM,
	SET_DISABLE_RECALIB,
	SET_DISABLE_RUMBLE,
	SET_ENABLE_USBGECKO,
	SET_WAIT_USBGECKO,
	SET_SIMMEMSIZE,
	SET_TAU_CALIB,
	SET_SAVE_FOLDER,
	SET_PAGE_1_NEXT,
	SET_PAGE_1_SAVE,
	SET_PAGE_1_CANCEL
};
#define PAGE_GLOBAL_MAX (SET_PAGE_1_CANCEL)

enum SETTINGS_INTERFACE {
	SET_FILE_MGMT,
	SET_RECENT_LIST,
	SET_SHOW_HIDDEN,
	SET_HIDE_UNK,
	SET_UI_ANIMS,
	SET_UI_COLOR,
	SET_LIBRARY_ICON,
	SET_SOURCE_ICON,
	SET_SETTINGS_ICON,
	SET_SYSTEM_ICON,
	SET_PANEL_TRANSPARENCY,
	SET_ANIMATED_BACKDROP,
	SET_MENU_MUSIC,
	SET_MENU_SFX,
	SET_AUTOBOOT,
	SET_AUTOLOAD,
	SET_FLATTEN_DIR,
	SET_LIBRARY_LAYOUT,
	SET_MENU_WIDESCREEN,
	SET_UP_FACE,
	SET_LEFT_FACE,
	SET_RIGHT_FACE,
	SET_DOWN_FACE,
	SET_UI_BACKDROP_COLOR,
	SET_UI_WAVE_COLOR,
	SET_CLOCK_POSITION,
	SET_WAVE_SPEED,
	SET_TEMPERATURE_POSITION,
	SET_LIBRARY_FOLDERS,
	SET_CUBE,
	SET_DETAIL_SAVES,
	SET_IDLE_ANIMATION,
	SET_WAVES,
	SET_HOME_CONTROLS,
	SET_FACE_LABELS,
	SET_PAGE_2_BACK,
	SET_PAGE_2_NEXT,
	SET_PAGE_2_SAVE,
	SET_PAGE_2_CANCEL
};
#define PAGE_INTERFACE_MAX (SET_PAGE_2_CANCEL)

enum SETTINGS_NETWORK {
	SET_INIT_NET=0,
	SET_BBA_LOCALIP,
	SET_BBA_NETMASK,
	SET_BBA_GATEWAY,
	SET_BBA_DHCP,
	SET_NTP_SYNC,
	SET_NTP_HOSTIP,
	SET_TIME_ZONE,
	SET_FSP_HOSTIP,
	SET_FSP_PORT,
	SET_FSP_PASS,
	SET_FSP_PMTU,
	SET_FTP_HOSTIP,
	SET_FTP_PORT,
	SET_FTP_USER,
	SET_FTP_PASS,
	SET_FTP_PASV,
	SET_SMB_HOSTIP,
	SET_SMB_SHARE,
	SET_SMB_USER,
	SET_SMB_PASS,
	SET_RT4K_HOSTIP,
	SET_RT4K_PORT,
	SET_PAGE_3_BACK,
	SET_PAGE_3_NEXT,
	SET_PAGE_3_SAVE,
	SET_PAGE_3_CANCEL
};
#define PAGE_NETWORK_MAX (SET_PAGE_3_CANCEL)

enum SETTINGS_GAME_GLOBAL {
	SET_IGR=0,
	SET_BS2BOOT,
	SET_EMULATE_MEMCARD,
	SET_DISABLE_MCPGAMEID,
	SET_DISABLE_VIDPATCH,
	SET_FORCE_VIDACTIVE,
	SET_PAUSE_AVOUTPUT,
	SET_ALL_CHEATS,
	SET_WIIRDDBG,
	SET_GLOBAL_DEFAULTS,
	SET_PAGE_4_BACK,
	SET_PAGE_4_NEXT,
	SET_PAGE_4_SAVE,
	SET_PAGE_4_CANCEL
};
#define PAGE_GAME_GLOBAL_MAX (SET_PAGE_4_CANCEL)

enum SETTINGS_GAME_DEFAULTS {
	SET_DEFAULT_NTSC_VIDEOMODE=0,
	SET_DEFAULT_PAL_VIDEOMODE,
	SET_DEFAULT_HORIZ_SCALE,
	SET_DEFAULT_VERT_OFFSET,
	SET_DEFAULT_VERT_FILTER,
	SET_DEFAULT_FIELD_RENDER,
	SET_DEFAULT_PIXEL_CENTER,
	SET_DEFAULT_ALPHA_DITHER,
	SET_DEFAULT_ANISO_FILTER,
	SET_DEFAULT_WIDESCREEN,
	SET_DEFAULT_POLL_RATE,
	SET_DEFAULT_INVERT_CAMERA,
	SET_DEFAULT_SWAP_CAMERA,
	SET_DEFAULT_TRIGGER_LEVEL,
	SET_DEFAULT_AUDIO_STREAM,
	SET_DEFAULT_READ_SPEED,
	SET_DEFAULT_EMULATE_ETHERNET,
	SET_DEFAULT_DISABLE_MEMCARD,
	SET_DEFAULT_DISABLE_HYPERVISOR,
	SET_DEFAULT_CLEAN_BOOT,
	SET_DEFAULT_RT4K_PROFILE,
	SET_DEFAULT_DEFAULTS,
	SET_PAGE_5_BACK,
	SET_PAGE_5_NEXT,
	SET_PAGE_5_SAVE,
	SET_PAGE_5_CANCEL
};
#define PAGE_GAME_DEFAULTS_MAX (SET_PAGE_5_CANCEL)

enum SETTINGS_GAME {
	SET_GAME_LANG=0,
	SET_FORCE_VIDEOMODE,
	SET_HORIZ_SCALE,
	SET_VERT_OFFSET,
	SET_VERT_FILTER,
	SET_FIELD_RENDER,
	SET_PIXEL_CENTER,
	SET_ALPHA_DITHER,
	SET_ANISO_FILTER,
	SET_WIDESCREEN,
	SET_POLL_RATE,
	SET_INVERT_CAMERA,
	SET_SWAP_CAMERA,
	SET_TRIGGER_LEVEL,
	SET_AUDIO_STREAM,
	SET_READ_SPEED,
	SET_EMULATE_ETHERNET,
	SET_DISABLE_MEMCARD,
	SET_DISABLE_HYPERVISOR,
	SET_CLEAN_BOOT,
	SET_RT4K_PROFILE,
	SET_DEFAULTS,
	SET_PAGE_6_BACK,
	SET_PAGE_6_SAVE,
	SET_PAGE_6_CANCEL
};
#define PAGE_GAME_MAX (SET_PAGE_6_CANCEL)

extern char *enableUSBGeckoStr[];
extern char *simulatedMemSizeStr[];
extern const int simulatedMemSizeInt[];
extern char *uiVModeStr[];
extern char *gameVModeStr[];
extern char *forceHScaleStr[];
extern char *forceVFilterStr[];
extern char *forceVJitterStr[];
extern char *fixPixelCenterStr[];
extern char *forceWidescreenStr[];
extern char *forcePollRateStr[];
extern char *invertCStickStr[];
extern char *swapCStickStr[];
extern char *configAudioBufferStr[];
extern char *disableMCPGameIDStr[];
extern char *disableVideoPatchesStr[];
extern char *forceDTVStatusStr[];
extern char *emulateAudioStreamStr[];
extern char *emulateReadSpeedStr[];
extern char *disableMemoryCardStr[];
#define SRAM_LANGUAGE_MAX 8
extern char *sramLanguageStr[];
extern char *sramVideoStr[];
extern char *igrTypeStr[];
extern char *aveCompatStr[];
extern char *fileBrowserTypeStr[];
extern char *bs2BootStr[];
extern char *recentListLevelStr[];
extern char *uiColorStr[];
extern char *uiLayerColorStr[];
extern char *clockPositionStr[];
extern char *waveSpeedStr[];
extern char *libraryIconStr[];
extern char *sourceIconStr[];
extern char *settingsIconStr[];
extern char *systemIconStr[];
extern char *upFaceStr[];
extern char *leftFaceStr[];
extern char *rightFaceStr[];
extern char *downFaceStr[];
extern char *libraryLayoutStr[];

int show_settings(int page, int option, ConfigEntry *config);
int show_settings_view(int view, int option, ConfigEntry *config);
int settings_game_custom_count(const ConfigEntry *game);
void settings_game_files_load(void);
void settings_game_files_forget(void);
bool settings_game_has_custom(const char *gameId, char region);
const char *settings_game_custom_first(ConfigEntry *game);

#endif