#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/exi.h>
#include <ogc/machine/processor.h>
#include "deviceHandler.h"
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "swiss.h"
#include "main.h"
#include "info.h"
#include "config.h"
#include "settings.h"
#include "menuaudio.h"
#include "exi.h"
#include "bba.h"
#include "sram.h"
#include "rt4k.h"
#include "time_sync.h"
#include "input.h"
#include "ui_color.h"
#include "ui_motion.h"
#include "ui_settings_layout.h"
#include "saves.h"

/* Bind the pure layout module's page model to the real option enums: a
 * drifting row count or action index becomes a compile error here, not a
 * silently wrong highlight. */
_Static_assert(UI_SETLAYOUT_PAGE_COUNT == VIEW_GAME + 1, "view count drift");
_Static_assert(UI_SETLAYOUT_TAB_COUNT == VIEW_SETUP + 1, "tab count drift");
_Static_assert(UI_SETLAYOUT_TAB_SETUP == VIEW_SETUP, "Setup tab drift");
/* Game Defaults shows every default but the vertical offset (see gameDefaultsRows). */
_Static_assert(UI_SETLAYOUT_ROWS_GAME_DEFAULTS == SET_DEFAULT_DEFAULTS, "defaults rows drift");
_Static_assert(UI_SETLAYOUT_ROWS_NETWORK == SET_RT4K_PORT + 1, "network rows drift");
_Static_assert(UI_SETLAYOUT_ROWS_GAME == SET_DEFAULTS + 1, "game rows drift");
_Static_assert(UI_SETLAYOUT_ROWS_SETUP == VIEW_DEVELOPER - VIEW_DISPLAY + 1, "Setup sections drift");
_Static_assert((int)UI_SETLAYOUT_MOTION_FULL == (int)UI_MOTION_FULL,
	"Full motion enum drift");
_Static_assert((int)UI_SETLAYOUT_MOTION_REDUCED == (int)UI_MOTION_REDUCED,
	"Reduced motion enum drift");
_Static_assert((int)UI_SETLAYOUT_MOTION_OFF == (int)UI_MOTION_OFF,
	"Off motion enum drift");

ConfigEntry tempConfig;
SwissSettings tempSettings;
char *enableUSBGeckoStr[] = {"No", "Slot A", "Slot B", "Serial Port 2"};
char *simulatedMemSizeStr[] = {"None", "16 MiB", "24 MiB", "32 MiB", "48 MiB", "64 MiB"};
char *uiVModeStr[] = {"Auto", "480i", "480sf", "240p", "480p", "576i", "576sf", "288p", "576p"};
char *gameVModeStr[] = {"Auto", "480i", "480sf", "240p", "960i", "480p", "1080i60", "540p60", "576i", "576sf", "288p", "1152i", "576p", "1080i50", "540p50"};
char *forceHScaleStr[] = {"Auto", "1:1", "11:10", "9:8", "640px", "656px", "672px", "704px", "720px"};
char *forceVFilterStr[] = {"Auto", "0", "1", "2"};
char *forceVJitterStr[] = {"Auto", "On", "Off", "TAA"};
char *fixPixelCenterStr[] = {"No", "1/24", "1/12"};
char *forceWidescreenStr[] = {"No", "3D", "2D+3D"};
char *forcePollRateStr[] = {"No", "VSync", "1000Hz", "500Hz", "350Hz", "300Hz", "250Hz", "200Hz", "150Hz", "150Hz", "120Hz", "120Hz", "100Hz"};
char *invertCStickStr[] = {"No", "X", "Y", "X&Y"};
char *swapCStickStr[] = {"No", "X", "Y", "X&Y"};
char *configAudioBufferStr[] = {"Off", "Auto", "On"};
char *disableMCPGameIDStr[] = {"No", "Slot A", "Slot B", "Slot A&B"};
char *disableVideoPatchesStr[] = {"None", "Game", "All"};
char *forceDTVStatusStr[] = {"No", "Yes", "Region Switch"};
char *emulateAudioStreamStr[] = {"Off", "Auto", "On"};
char *emulateReadSpeedStr[] = {"No", "Yes", "Wii"};
char *disableMemoryCardStr[] = {"No", "Slot A", "Slot B"};
char *sramLanguageStr[] = {"English", "German", "French", "Spanish", "Italian", "Dutch", "Japanese", "English (US)", "Default"};
char *sramVideoStr[] = {"NTSC", "PAL", "PAL-M"};
char *igrTypeStr[] = {"Disabled", "Reboot", "Apploader"};
char *aveCompatStr[] = {"AVE N-DOL", "AVE P-DOL", "CMPV-DOL", "GCDigital", "GCVideo", "AVE-RVL"};
char *fileBrowserTypeStr[] = {"Standard", "Fullwidth", "Carousel"};
char *bs2BootStr[] = {"No", "Yes", "Sound 1", "Sound 2"};
char *recentListLevelStr[] = {"Off", "Lazy", "On"};
char *uiColorStr[] = {"Indigo", "Azure", "Emerald", "Gold", "Spice", "Crimson", "Rose", "Jet Black"};
_Static_assert(sizeof(uiColorStr) / sizeof(uiColorStr[0]) == UI_COLOR_MAX, "Menu Color names drift");
/* Backdrop and Wave Color: 0 follows Menu Color, then uiColorStr's colors. */
char *uiLayerColorStr[] = {"Menu Color", "Indigo", "Azure", "Emerald", "Gold", "Spice", "Crimson", "Rose", "Jet Black"};
_Static_assert(sizeof(uiLayerColorStr) / sizeof(uiLayerColorStr[0]) == UI_COLOR_MAX + 1, "layer color names drift");
char *clockPositionStr[] = {"Right", "Left", "Off"};
_Static_assert(sizeof(clockPositionStr) / sizeof(clockPositionStr[0]) == CLOCK_POSITION_MAX, "Clock names drift");
char *waveSpeedStr[] = {"Normal", "Fast", "Slow"};
_Static_assert(sizeof(waveSpeedStr) / sizeof(waveSpeedStr[0]) == WAVE_SPEED_MAX, "Wave Speed names drift");
char *libraryIconStr[] = {"Controller", "Books", "Covers", "Play"};
char *sourceIconStr[] = {"Hub", "Disc", "SD Card", "Folder"};
char *settingsIconStr[] = {"Sliders", "Gear", "Toggles", "Dial"};
char *systemIconStr[] = {"Clock", "Info", "Power", "Chip"};
_Static_assert(sizeof(libraryIconStr) / sizeof(libraryIconStr[0]) == UI_HOME_ICON_CHOICES, "library icon names drift");
_Static_assert(sizeof(sourceIconStr) / sizeof(sourceIconStr[0]) == UI_HOME_ICON_CHOICES, "source icon names drift");
_Static_assert(sizeof(settingsIconStr) / sizeof(settingsIconStr[0]) == UI_HOME_ICON_CHOICES, "settings icon names drift");
_Static_assert(sizeof(systemIconStr) / sizeof(systemIconStr[0]) == UI_HOME_ICON_CHOICES, "system icon names drift");
/* What each of Library's sides can show, its default first and then each
 * face after it round (UIHome_SideFace), so a zeroed setting is the cube of
 * before. */
char *upFaceStr[] = {"Source", "Settings", "System", "Apps", "Memory Cards", "Emulators", "File Browser", "Web", "None"};
char *leftFaceStr[] = {"Settings", "System", "Apps", "Memory Cards", "Emulators", "File Browser", "Web", "None", "Source"};
char *rightFaceStr[] = {"System", "Apps", "Memory Cards", "Emulators", "File Browser", "Web", "None", "Source", "Settings"};
char *downFaceStr[] = {"Apps", "Memory Cards", "Emulators", "File Browser", "Web", "None", "Source", "Settings", "System"};
_Static_assert(sizeof(upFaceStr) / sizeof(upFaceStr[0]) == UI_HOME_FACE_COUNT, "up face names drift");
_Static_assert(sizeof(leftFaceStr) / sizeof(leftFaceStr[0]) == UI_HOME_FACE_COUNT, "left face names drift");
_Static_assert(sizeof(rightFaceStr) / sizeof(rightFaceStr[0]) == UI_HOME_FACE_COUNT, "right face names drift");
_Static_assert(sizeof(downFaceStr) / sizeof(downFaceStr[0]) == UI_HOME_FACE_COUNT, "down face names drift");
char *libraryLayoutStr[] = {"Horizontal", "Vertical", "Grid", "Spotlight", "Jewel Case"};
_Static_assert(sizeof(libraryLayoutStr) / sizeof(libraryLayoutStr[0]) == UI_GAMEFLOW_LAYOUT_COUNT, "library layout names drift");
static const char *uiMotionModeStr[] = {"Full", "Reduced", "Off"};

const int simulatedMemSizeInt[] = {
	0,
	16 << 20,
	24 << 20,
	32 << 20,
	48 << 20,
	64 << 20
};

static char *tooltips_global[PAGE_GLOBAL_MAX+1] = {
	[SET_SYS_BOOTMODE] = "System Boot Mode:\n\nSets development or production mode on development hardware.\nOn retail hardware with GC Loader or PicoLoader, the default\nskips the GameCube logo screen.",
	[SET_APPS_VIDEOMODE] = "Apps Video Mode:\n\nSets the video mode passed to homebrew apps. Auto keeps Indigo's\nmode. Applies to libogc2 apps; other apps may choose their own.\nA program sent over the network keeps Indigo's mode.",
	[SET_SYS_SOUND] = "System Sound:\n\nSets the default audio output type used by most games",
	[SET_SYS_VIDEO] = "System Video:\n\nIntended to select between NTSC and PAL-M on DOL-002(BRA)",
	[SET_SCREEN_POS] = "Screen Position:\n\nAdjusts the horizontal screen position in games",
	[SET_SYS_LANG] = "System Language:\n\nSystem language used in games, primarily multi-5 PAL games",
	[SET_CONFIG_DEV] = "Configuration Device:\n\nThe device Indigo loads settings from and saves them to, in\nswiss/settings/global.ini. The choice is stored in SRAM, and it\nchanges only once Save & Exit has written the settings there.\nThe line under the Storage title says whether that file loaded.",
	[SET_LIBRARY_PATH] = "Library Path:\n\nWhere the Library finds your games. Auto looks for a /games\nfolder on the boot device, then on the other storage connected,\nand starts on the first it finds. Or choose a folder, such as\nsdc:/games: Left and Right step through devices, A opens the\nfolder chooser, where X chooses a folder and Y switches devices.",
	[SET_SAVE_FOLDER] = "Save Folder:\n\nThe folder Memory Cards (Home > System) opens on the SD\ncard: saves copied off a memory card go there, unless you\nopen or pick another. It's a folder on the Configuration\nDevice; swiss/saves until you choose another.\n\nA opens its folders: A goes into one, X chooses the one open.",
	[SET_INIT_DRIVE] = "Init DVD Drive at startup:\n\nDisabled - Leave it as-is (default)\nEnabled - Deassert reset signal when Swiss starts\n\nThis is necessary for the eject button to function on the\nPanasonic Q when Swiss is used as IPL replacement.",
	[SET_STOP_MOTOR] = "Stop DVD Drive motor:\n\nDisabled - Leave it as-is (default)\nEnabled - Stop the disc from spinning when Swiss starts\n\nThis option is mostly for users booting from game save exploits\nwhere the disc will already be spinning.",
	[SET_AUDIO_BUFFER] = "Configure Audio Buffer:\n\nOff - Disable audio streaming\nAuto - Enable audio streaming if the disc is known to use it\nOn - Enable audio streaming if the disc asks for it (default)\n\nThe audio buffer consumes a large portion of the GameCube\ndisc drive's read-ahead cache, lengthening load times.",
	[SET_EXI_SPEED] = "SD/IDE-EXI Speed:\n\nThe clock speed to try using on the EXI bus for SD cards and\nIDE-EXI devices. 27 MHz may not work with some SD cards or\nSD card adapters.",
	[SET_AVE_COMPAT] = "AVE Compatibility:\n\nSets the compatibility mode for the used audio/video encoder.\n\nAVE N-DOL - Output PAL as NTSC 50\nAVE P-DOL - Disable progressive scan mode\nCMPV-DOL - Enable 1080i & 540p\nGCDigital - Apply input filtering in OSD\nGCVideo - Apply general workarounds for GCVideo (default)\nAVE-RVL - Support 960i & 1152i without WiiVideo",
	[SET_FORCE_DTVSTATUS] = "Force DTV Status:\n\nDisabled - Use detect signal from the Digital AV Out (default)\nEnabled - Force detection in the case of a hardware fault\nRegion Switch - Ease transition between SD/ED TV setups",
	[SET_RT4K_OPTIM] = "RetroTINK-4K HDMI Input:\n\nFor GCDigital compatibility mode:\n Requires FX-Framework firmware version 3.9.46.178 or later\n and RetroTINK-4K firmware version 1.9.4 or later, and using\n DV1-Direct mode.\n\nFor GCVideo compatibility mode:\n Requires GCVideo-DVI firmware version 3.0 or later.",
	[SET_DISABLE_RECALIB] = "Controller Recalibration:\n\nOn - Controllers are recalibrated as usual (default)\nOff - Skips recalibration. This avoids problems with\nnon-compliant GameCube Controller implementations.",
	[SET_ENABLE_USBGECKO] = "Enable USB Gecko:\n\nIf a USB Gecko is present, messages output to the debug UART\nwill be redirected. When the USB host isn't actively reading from\nthe USB Gecko, it may cause the system to hang.\n\nwiiload is also made available for iterative development.",
	[SET_WAIT_USBGECKO] = "Wait for USB Gecko:\n\nWait for the transmit buffer to be read by the USB host when full.",
	[SET_SIMMEMSIZE] = "Simulated MRAM Size:\n\nLimits the amount of memory available on development hardware.",
	[SET_TAU_CALIB] = "CPU Temperature Calibration:\n\nOn a cold boot, adjust this value so that the CPU temperature\nreading in the title bar is around room temperature.\n\nThere is no factory calibration.",
	[SET_SWISS_VIDEOMODE] = "Swiss Video Mode:\n\nThe video mode Indigo's own screens use. Auto picks 480p with a\ndigital cable and otherwise follows the console's region.\n\nLeft and Right choose a mode and A switches to it. Press A\nagain within 10 seconds to keep it; otherwise it changes back\nby itself.",
	[SET_DISABLE_RUMBLE] = "Controller Rumble:\n\nOn - Controllers can rumble in games (default)\nOff - Rumble is turned off in games"
};

static char *tooltips_interface[PAGE_INTERFACE_MAX+1] = {
	[SET_FILE_MGMT] = "File Management:\n\nWhen enabled, pressing Z on an entry in the file browser will\nallow it to be managed.",
	[SET_RECENT_LIST] = "Recent List:\n\n(On) - Press Start while browsing to show a recent list.\n(Lazy) - Same as On but list updates only for new entries.\n(Off) - Recent list is completely disabled.\n\nThe lazy/off options exist to minimise SD card writes.",
	[SET_HIDE_UNK] = "Hide unknown file types:\n\nDisabled - Show all files (default)\nEnabled - Hide unknown file types from being displayed\n\nKnown file types are:\n GameCube Executables (.bin/.dol/.elf)\n Disc images (.gcm/.iso/.nkit.iso/.tgc)\n MP3 Music (.mp3)\n WASP/WKF Flash files (.fzn)\n GameCube Memory Card files (.gci/.gcs/.sav)\n GameCube Executables with parameters appended (.dol+cli)",
	[SET_UI_ANIMS] = "UI Motion:\n\nFull - Calm spatial motion and ambient detail (default)\nReduced - Faster transitions without decorative travel\nOff - Menu elements move instantly\n\nMotion never delays input or changes the destination.",
	[SET_UI_COLOR] = "Menu Color:\n\nColors the cube, its light, the panels and the text, and\nthe backdrop and waves while they follow it.\nIndigo (default) is the GameCube's own color; Spice and\nJet Black are GameCube colors too.\n\nCover art, button icons, warnings and enabled cheats\nkeep their own colors.",
	[SET_UI_BACKDROP_COLOR] = "Backdrop Color:\n\nColors the backdrop behind the cube and its faint rings.\nMenu Color (default) - The backdrop follows Menu Color.\nA color - The backdrop keeps that color whatever Menu\nColor is.\n\nEvery color keeps the backdrop as dark, so the cube and\nthe text stand out as before.",
	[SET_UI_WAVE_COLOR] = "Wave Color:\n\nColors the waves that drift behind the cube.\nMenu Color (default) - The waves follow Menu Color.\nA color - The waves keep that color whatever Menu Color\nis.",
	[SET_LIBRARY_ICON] = "Library Icon:\n\nThe picture on the cube's Library face: Controller (default),\nBooks, Covers or Play. Each face has its own four icons.\n\nController mirrors your controller.\nEvery icon glows in the same shade, and the face keeps its\nname and what A opens.",
	[SET_SOURCE_ICON] = "Source Icon:\n\nThe picture on the cube's Source face: Hub (default),\nDisc, SD Card or Folder. Each face has its own four icons.\n\nThe Disc's glints turn while the menu moves.\nEvery icon glows in the same shade, and the face keeps its\nname and what A opens.",
	[SET_SETTINGS_ICON] = "Settings Icon:\n\nThe picture on the cube's Settings face: Sliders (default),\nGear, Toggles or Dial. Each face has its own four icons.\n\nSliders, Gear and Dial move while the menu does.\nEvery icon glows in the same shade, and the face keeps its\nname and what A opens.",
	[SET_SYSTEM_ICON] = "System Icon:\n\nThe picture on the cube's System face: Clock (default),\nInfo, Power or Chip. Each face has its own four icons.\n\nClock tells the time.\nEvery icon glows in the same shade, and the face keeps its\nname and what A opens.",
	[SET_PANEL_TRANSPARENCY] = "Panel Transparency:\n\nEnabled - Menu panels are translucent so the backdrop\nshows through, GameCube-menu style (default)\nDisabled - Panels are solid.",
	[SET_ANIMATED_BACKDROP] = "Animated Backdrop:\n\nEnabled - The backdrop gently drifts (default)\nDisabled - The backdrop is static.",
	[SET_MENU_MUSIC] = "Menu Music:\n\nEnabled - Play Up in the Sky, Indigo's menu music (default)\nDisabled - Silent.\n\nChanges take effect immediately.",
	[SET_MENU_SFX] = "Menu Sounds:\n\nEnabled - Soft blip/confirm sounds on navigation (default)\nDisabled - Silent.",
	[SET_AUTOLOAD] = "Load at startup:\n\nWhat Indigo opens when it starts.\n\nA chooses a device, then a folder on it: X picks the folder\nyou are in. Z on a game's details sets a game instead; Z on\nthat game again, or on .. in the folder's file list, turns\nit off.",
	[SET_FLATTEN_DIR] = "Flatten directory:\n\nFlattens a directory structure matching a glob pattern.\n\nWhile Library Folders is on, it sets this itself.",
	[SET_SHOW_HIDDEN] = "Show hidden files:\n\nLists files and folders marked hidden, such as the /swiss folder\nthat holds Indigo's settings.",
	[SET_MENU_WIDESCREEN] = "Menu Widescreen:\n\nYes - Drawn for a TV set to 16:9: the background fills the\nscreen and the menus keep their shape.\nNo - Drawn for a 4:3 picture (default)\n\nSet your TV or HDMI adapter to 16:9 too. Games follow Force\nWidescreen in Game Defaults, not this.",
	[SET_LIBRARY_LAYOUT] = "Library Layout:\n\nHorizontal - A row of covers; Left and Right move (default)\nVertical - A column of covers; Up and Down move\nGrid - Rows of five covers; every direction moves\nSpotlight - A gameplay still over banners; Left and Right move\nJewel Case - A CD case, its disc spinning; Left and Right move\n\nThe selected game's title and details show beside its cover in\nVertical and its still in Spotlight, and above the controls in Grid.\nEvery layout wraps round from the last game to the first; L and R\njump a page. Y opens the focused game's settings. In Jewel Case the\nC-stick turns the case.",
	[SET_LIBRARY_FOLDERS] = "Library Folders:\n\nOff - The Library is one list of every game in /games, however\nthey sit in folders there (default)\nOn - The Library shows the folders in /games beside the games:\nA opens one, B goes back. A folder in a folder is the deepest;\nit lists every game below it.\n\nKeep a game's discs in the same folder.",
	[SET_DETAIL_SAVES] = "Saves on Details:\n\nOn - Show saves when there are two or more copies (default).\nAlways - Show saves even with one copy or none.\nOff - Leave the memory cards alone.\n\nOn and Always read both slots and the Save Folder.\nWith multiple copies, Left and Right choose one to start with.",
	[SET_IDLE_ANIMATION] = "Idle Animation:\n\nHow the cube moves while Home rests.\nCalm - A slight drift, turned a little to the right (default)\nSway - It turns slowly left and right, so the faces beside\nthe one in front peek round its edges\n\nUI Motion Reduced or Off keeps the cube still.",
	[SET_WAVES] = "Waves:\n\nOn - Waves drift behind the cube (default)\nOff - No waves: the backdrop and its rings only\n\nWave Color and Wave Speed apply while they are on.",
	[SET_HOME_CONTROLS] = "On-screen Controls:\n\nOn - Home shows its button hints under the cube (default)\nOff - Home shows none\n\nOther screens, and Restart's question, keep theirs.\nY on Home always opens Settings.",
	[SET_FACE_LABELS] = "Face Labels:\n\nOn - Home names the face in front under the cube (default)\nOff - The cube shows only its icons.",
	[SET_WAVE_SPEED] = "Wave Speed:\n\nHow fast the waves behind the cube drift.\nSlow - Half as fast\nNormal - As they always have (default)\nFast - Three times as fast\n\nUI Motion Off or Reduced, or Animated Backdrop off (Setup >\nLibrary), still stops them.",
	[SET_CLOCK_POSITION] = "Clock:\n\nRight - The time sits in the top right corner (default)\nLeft - It sits in the top left corner instead\nOff - It is not shown\n\nTemperature places the temperature dial on its own.",
	[SET_TEMPERATURE_POSITION] = "Temperature:\n\nRight - The CPU temperature dial sits in the top right\ncorner (default)\nLeft - It sits in the top left corner instead\nOff - It is not shown\n\nIn the clock's corner, the dial sits nearer the edge.",
	[SET_UP_FACE] = "Up Face:\n\nThe menu above Library: Source (default), another\nmenu, or None.\n\nThe menus: Source, Settings, System, Apps, Memory Cards,\nEmulators, File Browser and Web. Classic turns Up to it; Infinite\nturns through the faces above, left, right and below Library,\nin that order. A menu on no side is off the cube. Y on Home\nalways opens Settings, and Apps and Emulators show while the\nsource has a program in /apps or /emulators.",
	[SET_LEFT_FACE] = "Left Face:\n\nThe menu left of Library: Settings (default), another\nmenu, or None.\n\nThe menus: Source, Settings, System, Apps, Memory Cards,\nEmulators, File Browser and Web. Classic turns Left to it; Infinite\nturns through the faces above, left, right and below Library,\nin that order. A menu on no side is off the cube. Y on Home\nalways opens Settings, and Apps and Emulators show while the\nsource has a program in /apps or /emulators.",
	[SET_RIGHT_FACE] = "Right Face:\n\nThe menu right of Library: System (default), another\nmenu, or None.\n\nThe menus: Source, Settings, System, Apps, Memory Cards,\nEmulators, File Browser and Web. Classic turns Right to it; Infinite\nturns through the faces above, left, right and below Library,\nin that order. A menu on no side is off the cube. Y on Home\nalways opens Settings, and Apps and Emulators show while the\nsource has a program in /apps or /emulators.",
	[SET_DOWN_FACE] = "Down Face:\n\nThe menu below Library: Apps (default), another\nmenu, or None.\n\nThe menus: Source, Settings, System, Apps, Memory Cards,\nEmulators, File Browser and Web. Classic turns Down to it; Infinite\nturns through the faces above, left, right and below Library,\nin that order. A menu on no side is off the cube. Y on Home\nalways opens Settings, and Apps and Emulators show while the\nsource has a program in /apps or /emulators.",
	[SET_CUBE] = "Cube:\n\nInfinite - Left and Right turn round every face in a ring (default)\nClassic - Every menu sits on its own side of the cube, as on\nthe GameCube\n\nIn Classic, turn to a side, and back to Library to reach the\nothers; Up Face, Left Face, Right Face and Down Face say\nwhat is on each side. B turns back to Library.",
	[SET_AUTOBOOT] = "Boot without prompts:\n\nStarts a game as soon as you choose it, without its detail screen.\nHold B while choosing a game to see the screen instead; that turns\nthis off for the rest of the session."
};

static char *tooltips_network[PAGE_NETWORK_MAX+1] = {
	[SET_INIT_NET] = "Init network at startup:\n\nDisabled - Do not initialise the BBA even if present (default)\nEnabled - If a BBA is present, it will be initialised at startup\n\nIf initialised, navigate to the IP in a web browser to backup\nvarious data. wiiload is available for iterative development.",
	[SET_FSP_PMTU] = "FSP Path MTU:\n\nThe valid range is between 576 and 2030 bytes (default: 1500).\nIncreasing this value may provide a performance enhancement.\n\nThe maximum packet size on the server should be set to at least\nthis value, minus 40 bytes. The Layer 2 MTU on all involved\nnetwork interfaces must be set to at least this value.\n\nMisconfiguration will result in read errors. Users of USB Dolphin\nneed to observe limitations specific to their USB Ethernet adapter.",
	[SET_BBA_LOCALIP] = "IPv4 Address:\n\nThe console's address when DHCP is off. With DHCP on, the address\nyour router hands out is shown here instead.\n\nTakes effect the next time the network starts.",
	[SET_BBA_NETMASK] = "IPv4 Netmask:\n\nThe network's prefix length when DHCP is off, such as 24 on a\ntypical home network.",
	[SET_BBA_GATEWAY] = "IPv4 Gateway:\n\nYour router's address when DHCP is off.",
	[SET_BBA_DHCP] = "IPv4 uses DHCP:\n\nYes - Your router assigns the address, netmask and gateway (default)\nNo - Use the address, netmask and gateway set above",
	[SET_NTP_SYNC] = "Sync Clock:\n\nYes - Set the clock from the NTP server when the network starts\nNo - Leave the clock as it is (default)\n\nThe IPL and games see the new time too. The network starts with\nIndigo when Init network at startup is on.",
	[SET_NTP_HOSTIP] = "NTP Server IP:\n\nAddress of the time server Sync Clock asks. Empty asks\nCloudflare's, 162.159.200.123 (default). A name won't work: the\nconsole has no DNS.",
	[SET_TIME_ZONE] = "Time Zone:\n\nA POSIX TZ string, such as MST7MDT,M3.2.0,M11.1.0 (US Mountain),\nGMT0BST,M3.5.0/1,M10.5.0 (UK) or IST-5:30 (India). Daylight time\nchanges by itself.\n\nEmpty keeps the zone the clock is already in: its offset from UTC,\nsnapped to a real time zone. Set daylight time in the IPL.",
	[SET_FSP_HOSTIP] = "FSP Host IP:\n\nAddress of the computer running the FSP server with your games.",
	[SET_FSP_PORT] = "FSP Port:\n\nPort of the FSP server (21 by default).",
	[SET_FSP_PASS] = "FSP Password:\n\nPassword for the FSP server, if it has one. It is stored as plain\ntext in global.ini.",
	[SET_FTP_HOSTIP] = "FTP Host IP:\n\nAddress of the FTP server with your games.",
	[SET_FTP_PORT] = "FTP Port:\n\nPort of the FTP server (21 by default).",
	[SET_FTP_USER] = "FTP Username:\n\nUser name for the FTP server.",
	[SET_FTP_PASS] = "FTP Password:\n\nPassword for the FTP server. It is stored as plain text in\nglobal.ini.",
	[SET_FTP_PASV] = "FTP PASV Mode:\n\nUse passive mode, where the console opens the data connection.\nTry it if listings stall behind a firewall or router.",
	[SET_SMB_HOSTIP] = "SMB Host IP:\n\nAddress of the computer sharing your games over SMB.",
	[SET_SMB_SHARE] = "SMB Share:\n\nName of the shared folder on that computer.",
	[SET_SMB_USER] = "SMB Username:\n\nUser name for the share.",
	[SET_SMB_PASS] = "SMB Password:\n\nPassword for the share. It is stored as plain text in global.ini.",
	[SET_RT4K_HOSTIP] = "RetroTINK-4K Host IP:\n\nAddress of a RetroTINK-4K on your network. Indigo switches it to\neach game's RetroTINK-4K profile.",
	[SET_RT4K_PORT] = "RetroTINK-4K Port:\n\nPort of the RetroTINK-4K's network control."
};

static char *tooltips_game_global[PAGE_GAME_GLOBAL_MAX+1] = {
	[SET_IGR] = "In-Game Reset: (A + Z + Start)\n\nReboot - Perform hot reset with a compatible device\nApploader - Requires /swiss/patches/apploader.img",
	[SET_BS2BOOT] = "Load GameCube Main Menu:\n\nWhen enabled, games will be booted with the GameCube logo\nscreen and Main Menu accessible with patches applied.\n\nRequires /swiss/patches/ipl.bin on Wii.",
	[SET_FORCE_VIDACTIVE] = "Force Video Active:\n\nA workaround for GCVideo-DVI firmware version series 3.0,\nrendered obsolete by 3.1 and later.",
	[SET_PAUSE_AVOUTPUT] = "Pause for resolution change:\n\nWhen enabled, a change in active video resolution will pause the\ngame for 2 seconds.",
	[SET_ALL_CHEATS] = "Auto-load cheats:\n\nIf enabled, and a cheats file for a particular game is found\ne.g. /swiss/cheats/GPOP8D.txt (on a compatible device)\nthen all previously enabled cheats will be re-enabled",
	[SET_WIIRDDBG] = "WiiRD debugging:\n\nDisabled - Boot as normal (default)\nEnabled - This will start a game with the WiiRD debugger enabled\n& paused\n\nThe WiiRD debugger takes up more memory and can cause issues.",
	[SET_EMULATE_MEMCARD] = "Emulate Memory Card:\n\nGames save to a memory card image on the device they start from\ninstead of a real memory card. Needs a device that supports it.",
	[SET_DISABLE_MCPGAMEID] = "Disable MemCard PRO GameID:\n\nStops Indigo telling a MemCard PRO or compatible card in that\nslot which game is starting, so it stays on its current card.",
	[SET_DISABLE_VIDPATCH] = "Disable Video Patches:\n\nNone - Apply Swiss's video patches, including fixes for\nparticular games (default)\nGame - Skip only the fixes for particular games\nAll - Patch no video at all. Forced video modes and the other\npicture settings then have no effect."
};

static char *tooltips_game[PAGE_GAME_DEFAULTS_MAX+1] = {
	[SET_VERT_OFFSET] = "Force Vertical Offset:\n\n+0 - Standard value (default)\n-2 - GCVideo-DVI compatible (480i)\n-3 - GCVideo-DVI compatible\n-4 - GCVideo-DVI compatible (240p)\n-12 - Datapath VisionRGB (480p)",
	[SET_VERT_FILTER] = "Force Vertical Filter:\n\nFor 480i & 576i:\n Auto - Do nothing (default)\n\nFor 240p & 288p:\n Auto - Equivalent to 0 (default)\n 0 - 50%/50% blend with lower lines\n 1 - 50%/50% blend with upper lines\n 2 - Discard even lines\n\nFor other video modes:\n Auto - Equivalent to 0 (default)\n 0 - 3\327MSAA resolve only\n 1 - 18.75%/62.5%/18.75% blend\n 2 - 25%/50%/25% blend (deflicker)",
	[SET_PIXEL_CENTER] = "Fix Pixel Center:\n\nNot to be confused with the \223480p Pixel Fix\224 on Wii.",
	[SET_ANISO_FILTER] = "Force Anisotropic Filter:\n\nThe GameCube's texture sampling hardware is optimised for the\ntrilinear filtering of 16 bpp textures and the bilinear filtering of\n32 bpp textures.\n\nIt is not unusual for the performance to randomly plummet once\nanisotropic filtering is enabled, so do so sparingly.",
	[SET_POLL_RATE] = "Force Polling Rate:\n\nVSync - Highest compatibility\n1000Hz - Lowest input latency",
	[SET_INVERT_CAMERA] = "Invert Camera Stick:\n\nNo - Leave C Stick as-is (default)\nX - Invert X-axis of the C Stick\nY - Invert Y-axis of the C Stick\nX&Y - Invert both axes of the C Stick",
	[SET_SWAP_CAMERA] = "Swap Camera Stick:\n\nNo - Leave C Stick as-is (default)\nX - Swap X-axis of the C Stick with the Control Stick\nY - Swap Y-axis of the C Stick with the Control Stick\nX&Y - Swap both axes of the C Stick with the Control Stick",
	[SET_TRIGGER_LEVEL] = "Digital Trigger Level:\n\nSets the threshold where the L/R Button is fully pressed.",
	[SET_AUDIO_STREAM] = "Emulate Audio Streaming:\n\nAudio streaming is a hardware feature that allows a compressed\naudio track to be played in the background by the disc drive.\n\nEmulation is necessary for devices not attached to the\nDVD Interface, or for those not implementing it regardless.",
	[SET_READ_SPEED] = "Emulate Read Speed:\n\nNo - Start transfer immediately (default)\nYes - Delay transfer to simulate the GameCube disc drive\nWii - Delay transfer to simulate the Wii disc drive\n\nThis is necessary to avoid programming mistakes obfuscated by\nthe original medium, or for speedrunning.",
	[SET_EMULATE_ETHERNET] = "Emulate Broadband Adapter:\n\nOnly available with the File Service Protocol or an initialised\nETH2GC/GCNET module, where memory constraints permit.\n\nPackets not destined for the hypervisor are forwarded to the\nvirtual MAC. The virtual MAC address is the same as the\nphysical MAC. The physical MAC/PHY retain their configuration\nfrom Swiss, including link speed.",
	[SET_DISABLE_MEMCARD] = "Disable Memory Card:\n\nSome games misbehave when unexpected devices are present in\nthe memory card slots. When selected, the device will be hidden\nfrom the game if present at boot time.",
	[SET_DISABLE_HYPERVISOR] = "Hypervisor:\n\nOn - Features and bugfixes that rely on the hypervisor work,\nalong with prepatching and patch persistence (default)\nOff - Turns all of them off\n\nOnly available to devices attached to the DVD Interface.",
	[SET_CLEAN_BOOT] = "Prefer Clean Boot:\n\nWhen enabled, the GameCube will be reset and the game booted\nthrough normal processes with no changes applied.\nRegion restrictions may be applicable.\n\nOnly available to devices attached to the DVD Interface.",
	[SET_RT4K_PROFILE] = "RetroTINK-4K Profile:\n\nPresses a profile button through a configured ser2net TCP\nconnection to the RetroTINK-4K's serial port.",
	[SET_GAME_LANG] = "Game Language:\n\nThe language this game uses. Default follows System Language.\nMostly matters for PAL games that include several languages.",
	[SET_FORCE_VIDEOMODE] = "Force Video Mode:\n\nThe video mode this game starts in. Auto keeps the game's own\nmode. Some modes only appear with a component or digital cable.",
	[SET_HORIZ_SCALE] = "Force Horizontal Scale:\n\nHow the video output scales the picture across.\nAuto - Keep the game's own scaling (default)\n1:1 - No horizontal scaling\n11:10, 9:8 - Fixed ratios\n640px to 720px - A fixed output width",
	[SET_FIELD_RENDER] = "Force Field Rendering:\n\nAuto - Keep the game's own choice (default)\nOn - Draw each field of an interlaced picture separately\nOff - Draw whole frames\nTAA - The jittered rendering some games use for anti-aliasing",
	[SET_ALPHA_DITHER] = "Alpha Dithering:\n\nOn - The dithering the GameCube adds to blended effects stays\n(default)\nOff - Turns it off. It can show as a fine dot pattern on digital\nvideo.",
	[SET_WIDESCREEN] = "Force Widescreen:\n\nStretches games made for 4:3 to fill a 16:9 screen.\n3D - Widen the 3D view only\n2D+3D - Also widen 2D menus and on-screen displays\nSet your TV to 16:9. Edges can look wrong in some games.",
	[SET_DEFAULTS] = "Reset to defaults:\n\nPuts every setting on this screen back to its default.\nIt asks first, and Discard & Exit still undoes it."
};

// Number of settings (including Back, Next, Save, Exit buttons) per page

char* getConfigDeviceName(SwissSettings *settings) {
	DEVICEHANDLER_INTERFACE *configDevice = getDeviceByUniqueId(settings->configDeviceId);
	return configDevice != NULL ? (char*)(configDevice->deviceName) : "None";
}

char* getAutoLoadDeviceName(SwissSettings *settings) {
	DEVICEHANDLER_INTERFACE *entryDevice = getDeviceFromPath(settings->autoload);
	return entryDevice != NULL ? (char*)(entryDevice->deviceName) : "None";
}

char* getLibraryPathName(SwissSettings *settings) {
	static char formatted[UI_SETLAYOUT_VALUE_TEXT_MAX + 1];
	DEVICEHANDLER_INTERFACE *sourceDevice = getDeviceFromPath(settings->libraryPath);
	size_t length;

	if(sourceDevice == NULL || settings->libraryPath[0] == '\0') {
		return "Auto";
	}
	length = strlen(settings->libraryPath);
	snprintf(formatted, sizeof(formatted), "%s%s",
		length > UI_SETLAYOUT_VALUE_TEXT_MAX ? "\205" : "",
		length > UI_SETLAYOUT_VALUE_TEXT_MAX ?
			settings->libraryPath + length - (UI_SETLAYOUT_VALUE_TEXT_MAX - 1) :
			settings->libraryPath);
	return formatted;
}

char* getGameVideoModeString(int gameVMode) {
	return gameVMode <= 0 ? getScanMode() == VI_PROGRESSIVE ? "Auto (Progressive)" : "Auto (Interlaced)" : gameVModeStr[gameVMode];
}

/* Game Defaults' first two rows are the NTSC and PAL video modes; the shared
 * table holds a game's language and video mode at those indices. */
static char *tooltips_game_defaults_video[SET_DEFAULT_PAL_VIDEOMODE + 1] = {
	[SET_DEFAULT_NTSC_VIDEOMODE] = "Force NTSC Video Mode:\n\nThe video mode games that aren't PAL start in, unless a game's\nown settings say otherwise. Auto keeps each game's own mode.",
	[SET_DEFAULT_PAL_VIDEOMODE] = "Force PAL Video Mode:\n\nThe video mode PAL games start in, unless a game's own settings\nsay otherwise. Auto keeps each game's own mode."
};

char* get_tooltip(int page_num, int option) {
	char *textPtr = NULL;
	if(page_num == PAGE_GLOBAL) {
		textPtr = tooltips_global[option];
	}
	else if(page_num == PAGE_INTERFACE) {
		textPtr = tooltips_interface[option];
	}
	else if(page_num == PAGE_NETWORK) {
		textPtr = tooltips_network[option];
	}
	else if(page_num == PAGE_GAME_GLOBAL) {
		textPtr = tooltips_game_global[option];
	}
	else if(page_num == PAGE_GAME_DEFAULTS) {
		textPtr = option <= SET_DEFAULT_PAL_VIDEOMODE ?
			tooltips_game_defaults_video[option] : tooltips_game[option];
	}
	else if(page_num == PAGE_GAME) {
		textPtr = tooltips_game[option];
	}
	return textPtr;
}

/*
 * Presentation. Each page is described as a snapshot with every string
 * fitted to its place, and FrameBufferMagic draws it in the cheat browser's
 * language from one page event kept for the whole Settings session: an
 * input updates that event in place, so no frame is ever drawn without a
 * page. Bounded: one static snapshot, no per-frame allocation, and nothing
 * the renderer reads points back here.
 */
static uiDrawObj_t *settingsPageEvent;

typedef enum {
	SET_ROWKIND_CYCLE = 0, /* Left/Right cycles the value */
	SET_ROWKIND_TOGGLE,    /* two values, drawn as ON or OFF */
	SET_ROWKIND_TEXT,      /* opens the on-screen text editor */
	SET_ROWKIND_ACTION,    /* row-level action (e.g. Reset to defaults) */
	SET_ROWKIND_LINK       /* Setup row that opens a section */
} uiSettingRowKind_t;

static int measureSettingText(const char *text)
{
	return GetTextSizeInPixels(text);
}

static size_t boundedSettingTextLength(const char *text)
{
	size_t length = 0u;

	while(length < UI_SETLAYOUT_VALUE_SOURCE_LIMIT && text[length] != '\0') {
		length++;
	}
	return length;
}

/* A presentation copy of source that fits width: at scale, shrinking to
 * floor, then shortened with an ellipsis. The label or value itself is never
 * modified. Fails closed with an empty copy. */
static bool prepareSettingText(const char *source, size_t maxSourceBytes,
	uiSetLayoutEllipsizeMode_t mode, int width, float scale, float floor,
	char *out, size_t outCapacity, float *outScale, short *outWidth)
{
	uiSetLayoutTextFit_t fit;

	if(!UISetLayout_PrepareText(source, boundedSettingTextLength(source),
		maxSourceBytes, mode, UI_SETLAYOUT_TEXT_PLAIN, false, true, width,
		scale, floor, measureSettingText, out, outCapacity, &fit)) {
		out[0] = '\0';
		*outScale = floor;
		fit.renderedWidth = 0;
	}
	else {
		*outScale = fit.scale;
	}
	if(outWidth != NULL) {
		*outWidth = (short)fit.renderedWidth;
	}
	return out[0] != '\0';
}

/* A button hint fits by its drawn width, icons included. */
static bool prepareHintText(const char *hint, int width, float preferredScale,
	char *out, size_t outCapacity, float *scale)
{
	uiSetLayoutTextFit_t fit;
	size_t length = strlen(hint);

	if(!UISetLayout_PrepareText(hint, length, length,
		UI_SETLAYOUT_ELLIPSIZE_TAIL, UI_SETLAYOUT_TEXT_PLAIN, false, true,
		width, preferredScale, preferredScale,
		GetHintSizeInPixels, out, outCapacity, &fit)) {
		out[0] = '\0';
		*scale = preferredScale;
		return false;
	}
	*scale = fit.scale;
	return true;
}

/* What one row shows. `text` holds formatted values. */
typedef struct {
	const char *label;
	const char *value;
	uiSettingRowKind_t kind;
	bool enabled;
	bool on;	/* a toggle's state */
	char text[32];
} settingRowView_t;

/* The UI_COLOR_LAYER_ a color row sets, or -1 for any other row. */
static int settingsColorLayer(int page, int option)
{
	if(page == PAGE_INTERFACE) {
		switch(option) {
			case SET_UI_COLOR: return UI_COLOR_LAYER_MENU;
			case SET_UI_BACKDROP_COLOR: return UI_COLOR_LAYER_BACKDROP;
			case SET_UI_WAVE_COLOR: return UI_COLOR_LAYER_WAVES;
		}
	}
	return -1;
}

/* One row of the snapshot. The value is drawn as its kind reads: a toggle's
 * ON/OFF pill, a choice's value pill (< > while focused), a text field, a
 * Setup section's summary, or an action's label alone. A color row's pill
 * starts with a dot of its layer's color: swatch is 1 + the layer, else 0. */
static void drawSettingRow(uiSetPageSnapshot_t *page, int slot,
	const settingRowView_t *row, bool custom, int swatch)
{
	const uiSetLayout_t *layout = &page->layout;
	uiSetPageRow_t *out = &page->rows[slot];
	char label[UI_SETLAYOUT_LABEL_BUFFER_SIZE];
	int valueWidth = layout->rowValueWidth - UI_SETLAYOUT_PILL_PAD * 2 -
		UI_SETLAYOUT_ARROW_W * 2;

	out->enabled = row->enabled;
	out->custom = custom;
	out->swatch = swatch;
	out->on = row->on;
	(void)UISetLayout_Label(row->label, label, sizeof(label));
	(void)prepareSettingText(label, sizeof(label) - 1u,
		UI_SETLAYOUT_ELLIPSIZE_TAIL, layout->rowLabelMaxWidth,
		UI_SETLAYOUT_LABEL_SCALE, UI_SETLAYOUT_ROW_TEXT_FLOOR, out->label,
		sizeof(out->label), &out->labelScale, NULL);
	switch(row->kind) {
		case SET_ROWKIND_TOGGLE:
			out->kind = UI_SETLAYOUT_ROW_TOGGLE;
			return;
		case SET_ROWKIND_ACTION:
			out->kind = UI_SETLAYOUT_ROW_ACTION;
			return;
		case SET_ROWKIND_TEXT:
			out->kind = UI_SETLAYOUT_ROW_TEXT;
			valueWidth = UI_SETLAYOUT_FIELD_W - UI_SETLAYOUT_PILL_PAD * 2;
			break;
		case SET_ROWKIND_LINK:
			out->kind = UI_SETLAYOUT_ROW_LINK;
			valueWidth = layout->rowValueWidth - 16;
			break;
		default:
			out->kind = UI_SETLAYOUT_ROW_CHOICE;
			/* A color row's swatch sits in the pill, before the name. */
			if(swatch) {
				valueWidth -= UI_SETLAYOUT_SWATCH + UI_SETLAYOUT_SWATCH_GAP;
			}
			break;
	}
	/* An empty text value reads "Not set" in the dimmed color. */
	out->placeholder = row->kind == SET_ROWKIND_TEXT &&
		(row->value == NULL || row->value[0] == '\0');
	(void)prepareSettingText(out->placeholder ? "Not set" : row->value,
		UI_SETLAYOUT_VALUE_TEXT_MAX, row->kind == SET_ROWKIND_TEXT ?
		UI_SETLAYOUT_ELLIPSIZE_MIDDLE : UI_SETLAYOUT_ELLIPSIZE_TAIL,
		valueWidth, UI_SETLAYOUT_VALUE_SCALE, UI_SETLAYOUT_VALUE_SCALE,
		out->value, sizeof(out->value), &out->valueScale, &out->valueWidth);
}

/* The header, section line and exits: where this page is and how to leave. */
static void drawSettingsChrome(uiSetPageSnapshot_t *page, int page_num,
	int option, ConfigEntry *gameConfig)
{
	static const char *actionText[UI_SETLAYOUT_MAX_ACTIONS] = {
		"Save & Exit", "Discard & Exit"
	};
	const uiSetLayout_t *layout = &page->layout;
	const uiSetLayoutPage_t *desc = UISetLayout_PageDesc(page_num);
	const char *subtitleText = desc->subtitle;
	char section[UI_SETLAYOUT_LABEL_BUFFER_SIZE];
	char text[UI_SETLAYOUT_SHORT_CAPACITY];
	float scale;
	size_t i;
	int n;

	(void)prepareSettingText(page_num == VIEW_GAME && gameConfig != NULL &&
		gameConfig->game_name[0] != '\0' ? gameConfig->game_name :
		desc->title, UI_SETLAYOUT_LABEL_BUFFER_SIZE - 1u,
		UI_SETLAYOUT_ELLIPSIZE_TAIL, layout->titleMaxWidth,
		UI_SETLAYOUT_TITLE_SCALE, UI_SETLAYOUT_TITLE_FLOOR, page->title,
		sizeof(page->title), &page->titleScale, NULL);
	if(page_num == VIEW_STORAGE) {
		subtitleText = UISetLayout_SettingsFileText(
			devices[DEVICE_CONFIG] == NULL ? UI_SETLAYOUT_SETTINGS_FILE_NO_DEVICE :
			config_global_file_loaded() ? UI_SETLAYOUT_SETTINGS_FILE_SAVED :
			UI_SETLAYOUT_SETTINGS_FILE_MISSING);
	}
	(void)prepareSettingText(subtitleText, UI_SETLAYOUT_LABEL_BUFFER_SIZE - 1u,
		UI_SETLAYOUT_ELLIPSIZE_TAIL, layout->subtitleMaxWidth,
		UI_SETLAYOUT_SUBTITLE_SCALE, UI_SETLAYOUT_SUBTITLE_SCALE,
		page->subtitle, sizeof(page->subtitle), &page->subtitleScale, NULL);

	/* The tabs, or on a game's page how many of its rows are its own. */
	for(n = 0; n < layout->tabCount; n++) {
		(void)prepareSettingText(UISetLayout_PageDesc(n)->tabLabel,
			sizeof(page->tab[n]) - 1u, UI_SETLAYOUT_ELLIPSIZE_TAIL,
			layout->tabCell[n].w - 12, UI_SETLAYOUT_TAB_SCALE,
			UI_SETLAYOUT_TAB_SCALE, page->tab[n], sizeof(page->tab[n]),
			&page->tabScale[n], NULL);
	}
	if(layout->tabCount == 0) {
		n = settings_game_custom_count(gameConfig);
		if(n > 0) {
			snprintf(text, sizeof(text), "%d custom", n);
		}
		(void)prepareSettingText(n > 0 ? text : "No custom settings",
			sizeof(text) - 1u, UI_SETLAYOUT_ELLIPSIZE_TAIL,
			layout->badgeMaxWidth, UI_SETLAYOUT_BADGE_SCALE,
			UI_SETLAYOUT_BADGE_SCALE, page->badge, sizeof(page->badge),
			&page->badgeScale, NULL);
	}

	/* "SETUP  >>  DISPLAY": the section line says where this page is. */
	snprintf(section, sizeof(section), "%s%s", page_num > VIEW_SETUP &&
		page_num < VIEW_GAME ? "SETUP  \273  " : "", desc->title);
	for(i = 0; section[i] != '\0'; i++) {
		if(section[i] >= 'a' && section[i] <= 'z') {
			section[i] = (char)(section[i] - 'a' + 'A');
		}
	}
	(void)prepareSettingText(section, sizeof(page->section) - 1u,
		UI_SETLAYOUT_ELLIPSIZE_TAIL, layout->sectionMaxWidth,
		UI_SETLAYOUT_SECTION_SCALE, UI_SETLAYOUT_SECTION_SCALE,
		page->section, sizeof(page->section), &scale, NULL);
	if(layout->selectedRow >= 0) {
		snprintf(page->position, sizeof(page->position), "%d / %d",
			option + 1, layout->rowCount);
	}
	for(n = 0; n < layout->actionCount; n++) {
		(void)prepareSettingText(actionText[layout->actionKind[n]],
			sizeof(page->action[n]) - 1u, UI_SETLAYOUT_ELLIPSIZE_TAIL,
			layout->actionRect[n].w - 12, UI_SETLAYOUT_ACTION_SCALE,
			UI_SETLAYOUT_ROW_TEXT_FLOOR - 0.12f, page->action[n],
			sizeof(page->action[n]), &page->actionScale[n], NULL);
	}
}

/* One view row: the (page, option) setting it shows, or, when page is
 * SETTINGS_ROW_LINK, the Setup section it opens (option = view). */
typedef struct {
	u8 page;
	u8 option;
} settingsRowRef_t;

#define SETTINGS_ROW_LINK 0xFF

static const settingsRowRef_t quickRows[] = {
	{PAGE_INTERFACE, SET_MENU_MUSIC},
	{PAGE_INTERFACE, SET_MENU_SFX},
	{PAGE_INTERFACE, SET_UI_ANIMS},
	{PAGE_GLOBAL, SET_DISABLE_RUMBLE},
	{PAGE_GAME_GLOBAL, SET_IGR},
	{PAGE_GAME_GLOBAL, SET_BS2BOOT},
	{PAGE_GAME_GLOBAL, SET_EMULATE_MEMCARD},
	{PAGE_GAME_GLOBAL, SET_ALL_CHEATS},
	{PAGE_INTERFACE, SET_AUTOBOOT},
};

/* Game Defaults and a game's own settings share one order: what people
 * change most (video, widescreen, controls), then compatibility, picture
 * tuning and the RetroTINK-4K profile. */
static const settingsRowRef_t gameDefaultsRows[] = {
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_NTSC_VIDEOMODE},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_PAL_VIDEOMODE},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_WIDESCREEN},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_POLL_RATE},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_INVERT_CAMERA},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_SWAP_CAMERA},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_TRIGGER_LEVEL},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_CLEAN_BOOT},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_DISABLE_HYPERVISOR},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_READ_SPEED},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_AUDIO_STREAM},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_EMULATE_ETHERNET},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_DISABLE_MEMCARD},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_HORIZ_SCALE},
	/* No SET_DEFAULT_VERT_OFFSET: every game starts at global.ini's Force
	 * Vertical Offset (+0 unless set there), and a game's own settings can
	 * change it. */
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_VERT_FILTER},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_FIELD_RENDER},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_PIXEL_CENTER},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_ALPHA_DITHER},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_ANISO_FILTER},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_RT4K_PROFILE},
	{PAGE_GAME_DEFAULTS, SET_DEFAULT_DEFAULTS},
};

static const settingsRowRef_t setupRows[] = {
	{SETTINGS_ROW_LINK, VIEW_DISPLAY},
	{SETTINGS_ROW_LINK, VIEW_CONSOLE},
	{SETTINGS_ROW_LINK, VIEW_STORAGE},
	{SETTINGS_ROW_LINK, VIEW_NETWORK},
	{SETTINGS_ROW_LINK, VIEW_LIBRARY},
	{SETTINGS_ROW_LINK, VIEW_DEVELOPER},
};

static const settingsRowRef_t displayRows[] = {
	{PAGE_GLOBAL, SET_SWISS_VIDEOMODE},
	{PAGE_GLOBAL, SET_APPS_VIDEOMODE},
	{PAGE_INTERFACE, SET_MENU_WIDESCREEN},
	{PAGE_GLOBAL, SET_SYS_VIDEO},
	{PAGE_GLOBAL, SET_SCREEN_POS},
	{PAGE_GLOBAL, SET_AVE_COMPAT},
	{PAGE_GLOBAL, SET_FORCE_DTVSTATUS},
	{PAGE_GLOBAL, SET_RT4K_OPTIM},
	{PAGE_GAME_GLOBAL, SET_DISABLE_VIDPATCH},
	{PAGE_GAME_GLOBAL, SET_FORCE_VIDACTIVE},
	{PAGE_GAME_GLOBAL, SET_PAUSE_AVOUTPUT},
};

static const settingsRowRef_t consoleRows[] = {
	{PAGE_INTERFACE, SET_UI_COLOR},
	{PAGE_INTERFACE, SET_UI_BACKDROP_COLOR},
	{PAGE_INTERFACE, SET_WAVES},
	{PAGE_INTERFACE, SET_UI_WAVE_COLOR},
	{PAGE_INTERFACE, SET_WAVE_SPEED},
	{PAGE_INTERFACE, SET_LIBRARY_ICON},
	{PAGE_INTERFACE, SET_SOURCE_ICON},
	{PAGE_INTERFACE, SET_SETTINGS_ICON},
	{PAGE_INTERFACE, SET_SYSTEM_ICON},
	{PAGE_INTERFACE, SET_UP_FACE},
	{PAGE_INTERFACE, SET_LEFT_FACE},
	{PAGE_INTERFACE, SET_RIGHT_FACE},
	{PAGE_INTERFACE, SET_DOWN_FACE},
	{PAGE_INTERFACE, SET_CUBE},
	{PAGE_INTERFACE, SET_IDLE_ANIMATION},
	{PAGE_INTERFACE, SET_FACE_LABELS},
	{PAGE_INTERFACE, SET_HOME_CONTROLS},
	{PAGE_INTERFACE, SET_CLOCK_POSITION},
	{PAGE_INTERFACE, SET_TEMPERATURE_POSITION},
	{PAGE_GLOBAL, SET_SYS_SOUND},
	{PAGE_GLOBAL, SET_SYS_LANG},
	{PAGE_GLOBAL, SET_SYS_BOOTMODE},
	{PAGE_GLOBAL, SET_DISABLE_RECALIB},
	{PAGE_GLOBAL, SET_TAU_CALIB},
};

static const settingsRowRef_t storageRows[] = {
	{PAGE_GLOBAL, SET_CONFIG_DEV},
	{PAGE_GLOBAL, SET_LIBRARY_PATH},
	{PAGE_GLOBAL, SET_SAVE_FOLDER},
	{PAGE_GLOBAL, SET_EXI_SPEED},
	{PAGE_GLOBAL, SET_INIT_DRIVE},
	{PAGE_GLOBAL, SET_STOP_MOTOR},
	{PAGE_GLOBAL, SET_AUDIO_BUFFER},
	{PAGE_GAME_GLOBAL, SET_DISABLE_MCPGAMEID},
};

static const settingsRowRef_t networkRows[] = {
	{PAGE_NETWORK, SET_INIT_NET},
	{PAGE_NETWORK, SET_BBA_LOCALIP},
	{PAGE_NETWORK, SET_BBA_NETMASK},
	{PAGE_NETWORK, SET_BBA_GATEWAY},
	{PAGE_NETWORK, SET_BBA_DHCP},
	{PAGE_NETWORK, SET_FSP_HOSTIP},
	{PAGE_NETWORK, SET_FSP_PORT},
	{PAGE_NETWORK, SET_FSP_PASS},
	{PAGE_NETWORK, SET_FSP_PMTU},
	{PAGE_NETWORK, SET_FTP_HOSTIP},
	{PAGE_NETWORK, SET_FTP_PORT},
	{PAGE_NETWORK, SET_FTP_USER},
	{PAGE_NETWORK, SET_FTP_PASS},
	{PAGE_NETWORK, SET_FTP_PASV},
	{PAGE_NETWORK, SET_SMB_HOSTIP},
	{PAGE_NETWORK, SET_SMB_SHARE},
	{PAGE_NETWORK, SET_SMB_USER},
	{PAGE_NETWORK, SET_SMB_PASS},
	{PAGE_NETWORK, SET_RT4K_HOSTIP},
	{PAGE_NETWORK, SET_RT4K_PORT},
	/* Sync Clock's rows stay last: without a network adapter, Settings hides
	 * them (SETTINGS_SYNC_CLOCK_ROWS, UISetLayout_HideTail). */
	{PAGE_NETWORK, SET_NTP_SYNC},
	{PAGE_NETWORK, SET_NTP_HOSTIP},
	{PAGE_NETWORK, SET_TIME_ZONE},
};
#define SETTINGS_SYNC_CLOCK_ROWS 3

/* Whether a network adapter is there, read once as Settings opens. */
static bool settingsNetAdapter;

static const settingsRowRef_t libraryRows[] = {
	{PAGE_INTERFACE, SET_LIBRARY_LAYOUT},
	{PAGE_INTERFACE, SET_LIBRARY_FOLDERS},
	{PAGE_INTERFACE, SET_DETAIL_SAVES},
	{PAGE_INTERFACE, SET_RECENT_LIST},
	{PAGE_INTERFACE, SET_AUTOLOAD},
	{PAGE_INTERFACE, SET_FLATTEN_DIR},
	{PAGE_INTERFACE, SET_SHOW_HIDDEN},
	{PAGE_INTERFACE, SET_HIDE_UNK},
	{PAGE_INTERFACE, SET_FILE_MGMT},
	{PAGE_INTERFACE, SET_PANEL_TRANSPARENCY},
	{PAGE_INTERFACE, SET_ANIMATED_BACKDROP},
};

static const settingsRowRef_t developerRows[] = {
	{PAGE_GLOBAL, SET_ENABLE_USBGECKO},
	{PAGE_GLOBAL, SET_WAIT_USBGECKO},
	{PAGE_GLOBAL, SET_SIMMEMSIZE},
	{PAGE_GAME_GLOBAL, SET_WIIRDDBG},
};

static const settingsRowRef_t gameRows[] = {
	{PAGE_GAME, SET_FORCE_VIDEOMODE},
	{PAGE_GAME, SET_WIDESCREEN},
	{PAGE_GAME, SET_GAME_LANG},
	{PAGE_GAME, SET_POLL_RATE},
	{PAGE_GAME, SET_INVERT_CAMERA},
	{PAGE_GAME, SET_SWAP_CAMERA},
	{PAGE_GAME, SET_TRIGGER_LEVEL},
	{PAGE_GAME, SET_CLEAN_BOOT},
	{PAGE_GAME, SET_DISABLE_HYPERVISOR},
	{PAGE_GAME, SET_READ_SPEED},
	{PAGE_GAME, SET_AUDIO_STREAM},
	{PAGE_GAME, SET_EMULATE_ETHERNET},
	{PAGE_GAME, SET_DISABLE_MEMCARD},
	{PAGE_GAME, SET_HORIZ_SCALE},
	{PAGE_GAME, SET_VERT_OFFSET},
	{PAGE_GAME, SET_VERT_FILTER},
	{PAGE_GAME, SET_FIELD_RENDER},
	{PAGE_GAME, SET_PIXEL_CENTER},
	{PAGE_GAME, SET_ALPHA_DITHER},
	{PAGE_GAME, SET_ANISO_FILTER},
	{PAGE_GAME, SET_RT4K_PROFILE},
	{PAGE_GAME, SET_DEFAULTS},
};

/* Where each game row keeps its value, so it can be compared with the
 * default and put back. The game file stores exactly these fields. */
#define GAME_FIELD(field) {offsetof(ConfigEntry, field), sizeof(((ConfigEntry *)0)->field)}
static const struct {
	size_t offset;
	size_t size;
} gameRowFields[] = {
	[SET_GAME_LANG] = GAME_FIELD(gameLanguage),
	[SET_FORCE_VIDEOMODE] = GAME_FIELD(gameVMode),
	[SET_HORIZ_SCALE] = GAME_FIELD(forceHScale),
	[SET_VERT_OFFSET] = GAME_FIELD(forceVOffset),
	[SET_VERT_FILTER] = GAME_FIELD(forceVFilter),
	[SET_FIELD_RENDER] = GAME_FIELD(forceVJitter),
	[SET_PIXEL_CENTER] = GAME_FIELD(fixPixelCenter),
	[SET_ALPHA_DITHER] = GAME_FIELD(disableDithering),
	[SET_ANISO_FILTER] = GAME_FIELD(forceAnisotropy),
	[SET_WIDESCREEN] = GAME_FIELD(forceWidescreen),
	[SET_POLL_RATE] = GAME_FIELD(forcePollRate),
	[SET_INVERT_CAMERA] = GAME_FIELD(invertCStick),
	[SET_SWAP_CAMERA] = GAME_FIELD(swapCStick),
	[SET_TRIGGER_LEVEL] = GAME_FIELD(triggerLevel),
	[SET_AUDIO_STREAM] = GAME_FIELD(emulateAudioStream),
	[SET_READ_SPEED] = GAME_FIELD(emulateReadSpeed),
	[SET_EMULATE_ETHERNET] = GAME_FIELD(emulateEthernet),
	[SET_DISABLE_MEMCARD] = GAME_FIELD(disableMemoryCard),
	[SET_DISABLE_HYPERVISOR] = GAME_FIELD(disableHypervisor),
	[SET_CLEAN_BOOT] = GAME_FIELD(preferCleanBoot),
	[SET_RT4K_PROFILE] = GAME_FIELD(rt4kProfile),
};
#undef GAME_FIELD
_Static_assert(sizeof(gameRowFields) / sizeof(gameRowFields[0]) == SET_DEFAULTS, "game fields drift");

/* The defaults this game would get from the current Game Defaults. While a
 * game's settings are open the global settings cannot change, so these are
 * also what Save compares against. */
static void settingsGameDefaults(const ConfigEntry *game, ConfigEntry *defaults)
{
	memcpy(defaults, game, sizeof(ConfigEntry));
	config_defaults_from(defaults, &swissSettings);
}

static bool settingsGameRowCustom(const ConfigEntry *game,
	const ConfigEntry *defaults, int option)
{
	return option >= 0 && option < SET_DEFAULTS &&
		memcmp((const char *)game + gameRowFields[option].offset,
			(const char *)defaults + gameRowFields[option].offset,
			gameRowFields[option].size) != 0;
}

/* How many of a game's rows differ from Game Defaults (Game Detail shows it). */
int settings_game_custom_count(const ConfigEntry *game)
{
	static ConfigEntry defaults;
	int count = 0;
	int option;

	if(game == NULL) {
		return 0;
	}
	settingsGameDefaults(game, &defaults);
	for(option = 0; option < SET_DEFAULTS; option++) {
		if(settingsGameRowCustom(game, &defaults, option)) {
			count++;
		}
	}
	return count;
}

/* The Library marks the covers of games that have settings of their own.
 * Each card on screen asks again on every step of the carousel, so the
 * settings files are read once (one mount, one folder listing) and kept,
 * each with its answer once worked out, until the next save changes them. */
typedef struct {
	char gameId[4];
	char *text;
	char markRegion;	/* the region mark was worked out for; 0 before */
	bool mark;
} settingsGameFile_t;

static settingsGameFile_t *settingsGameFiles;
static int settingsGameFileCount;
static bool settingsGameFilesRead;
static bool settingsGameFilesFailed;
static u64 settingsGameFilesFailedAt;

static void settingsKeepGameFile(const char *gameId, char *text, void *context)
{
	settingsGameFile_t *grown;

	(void)context;
	grown = realloc(settingsGameFiles,
		(size_t)(settingsGameFileCount + 1) * sizeof(*grown));
	if(grown == NULL) {
		return;
	}
	settingsGameFiles = grown;
	memcpy(grown[settingsGameFileCount].gameId, gameId, 4);
	grown[settingsGameFileCount].markRegion = 0;
	grown[settingsGameFileCount].text = strdup(text);
	if(grown[settingsGameFileCount].text != NULL) {
		settingsGameFileCount++;
	}
}

/* A save changed the files: read them again next time. */
void settings_game_files_forget(void)
{
	int i;

	for(i = 0; i < settingsGameFileCount; i++) {
		free(settingsGameFiles[i].text);
	}
	free(settingsGameFiles);
	settingsGameFiles = NULL;
	settingsGameFileCount = 0;
	settingsGameFilesRead = false;
	settingsGameFilesFailed = false;
}

/* A settings device that can't be mounted is tried again, but at most this
 * often: the carousel asks on every step, and probing a missing card is
 * slow. */
#define SETTINGS_GAME_FILES_RETRY_MS 30000u

void settings_game_files_load(void)
{
	if(settingsGameFilesRead || (settingsGameFilesFailed &&
		ticks_to_millisecs(SYS_GetSystemTime() - settingsGameFilesFailedAt) <
			SETTINGS_GAME_FILES_RETRY_MS)) {
		return;
	}
	settingsGameFilesFailed = config_each_game_file(settingsKeepGameFile, NULL) < 0;
	if(settingsGameFilesFailed) {
		settingsGameFilesFailedAt = SYS_GetSystemTime();
	}
	else {
		settingsGameFilesRead = true;
	}
}

/* Whether a game has any row that differs from Game Defaults, as Game
 * Detail counts it. region is 'P' for PAL discs, whose video mode default
 * differs. Reads nothing: settings_game_files_load did; and parses a game's
 * file once, until a save forgets the files. */
bool settings_game_has_custom(const char *gameId, char region)
{
	static ConfigEntry game;
	char *text;
	int i;

	if(gameId == NULL || gameId[0] == '\0') {
		return false;
	}
	for(i = 0; i < settingsGameFileCount; i++) {
		if(memcmp(settingsGameFiles[i].gameId, gameId, 4) == 0) {
			break;
		}
	}
	if(i == settingsGameFileCount) {
		return false;
	}
	if(settingsGameFiles[i].markRegion == region) {
		return settingsGameFiles[i].mark;
	}
	/* config_parse_game cuts its input into lines, so parse a copy. */
	text = strdup(settingsGameFiles[i].text);
	if(text == NULL) {
		return false;
	}
	memset(&game, 0, sizeof(game));
	memcpy(game.game_id, gameId, 4);
	game.region = region;
	config_defaults(&game);
	config_parse_game(text, &game);
	free(text);
	settingsGameFiles[i].mark = settings_game_custom_count(&game) > 0;
	settingsGameFiles[i].markRegion = region;
	return settingsGameFiles[i].mark;
}

/* X in a game's settings: this row follows Game Defaults again. */
static void settingsUseDefault(ConfigEntry *game, int option)
{
	static ConfigEntry defaults;

	if(option < 0 || option >= SET_DEFAULTS) {
		return;
	}
	settingsGameDefaults(game, &defaults);
	memcpy((char *)game + gameRowFields[option].offset,
		(const char *)&defaults + gameRowFields[option].offset,
		gameRowFields[option].size);
}

/* Reset to defaults in a game's settings: every row follows Game Defaults
 * again. The game's Comment and Status aren't settings, so they stay. */
static void settingsResetGame(ConfigEntry *game)
{
	int option;

	for(option = 0; option < SET_DEFAULTS; option++) {
		settingsUseDefault(game, option);
	}
}

#define SETTINGS_ROWS(rows) {rows, sizeof(rows) / sizeof(rows[0])}
static const struct {
	const settingsRowRef_t *rows;
	int count;
} settingsViews[UI_SETLAYOUT_PAGE_COUNT] = {
	[VIEW_QUICK] = SETTINGS_ROWS(quickRows),
	[VIEW_GAME_DEFAULTS] = SETTINGS_ROWS(gameDefaultsRows),
	[VIEW_SETUP] = SETTINGS_ROWS(setupRows),
	[VIEW_DISPLAY] = SETTINGS_ROWS(displayRows),
	[VIEW_CONSOLE] = SETTINGS_ROWS(consoleRows),
	[VIEW_STORAGE] = SETTINGS_ROWS(storageRows),
	[VIEW_NETWORK] = SETTINGS_ROWS(networkRows),
	[VIEW_LIBRARY] = SETTINGS_ROWS(libraryRows),
	[VIEW_DEVELOPER] = SETTINGS_ROWS(developerRows),
	[VIEW_GAME] = SETTINGS_ROWS(gameRows),
};
#undef SETTINGS_ROWS

_Static_assert(sizeof(quickRows) / sizeof(quickRows[0]) == UI_SETLAYOUT_ROWS_QUICK, "quick rows drift");
_Static_assert(sizeof(gameDefaultsRows) / sizeof(gameDefaultsRows[0]) == UI_SETLAYOUT_ROWS_GAME_DEFAULTS, "defaults rows drift");
_Static_assert(sizeof(setupRows) / sizeof(setupRows[0]) == UI_SETLAYOUT_ROWS_SETUP, "setup rows drift");
_Static_assert(sizeof(displayRows) / sizeof(displayRows[0]) == UI_SETLAYOUT_ROWS_DISPLAY, "display rows drift");
_Static_assert(sizeof(consoleRows) / sizeof(consoleRows[0]) == UI_SETLAYOUT_ROWS_CONSOLE, "console rows drift");
_Static_assert(sizeof(storageRows) / sizeof(storageRows[0]) == UI_SETLAYOUT_ROWS_STORAGE, "storage rows drift");
_Static_assert(sizeof(networkRows) / sizeof(networkRows[0]) == UI_SETLAYOUT_ROWS_NETWORK, "network rows drift");
_Static_assert(sizeof(libraryRows) / sizeof(libraryRows[0]) == UI_SETLAYOUT_ROWS_LIBRARY, "library rows drift");
_Static_assert(sizeof(developerRows) / sizeof(developerRows[0]) == UI_SETLAYOUT_ROWS_DEVELOPER, "developer rows drift");
_Static_assert(sizeof(gameRows) / sizeof(gameRows[0]) == UI_SETLAYOUT_ROWS_GAME, "game rows drift");

/* Short summaries shown beside each Setup section; each fits the value
 * column's UI_SETLAYOUT_VALUE_TEXT_MAX without an ellipsis. */
static const char *setupSummaries[] = {
	"Video mode, cable, TV",
	"Color, icons, language",
	"Settings file, SD, disc",
	"Adapter, file servers",
	"Browser, recent, look",
	"USB Gecko, memory, debug",
};
_Static_assert(sizeof(setupSummaries) / sizeof(setupSummaries[0]) == UI_SETLAYOUT_ROWS_SETUP, "setup summaries drift");

static const settingsRowRef_t *settingsViewRow(int view, int option)
{
	if(view < 0 || view >= UI_SETLAYOUT_PAGE_COUNT ||
		option < 0 || option >= UISetLayout_RowCount(view)) {
		return NULL;
	}
	return &settingsViews[view].rows[option];
}

static void rowShow(settingRowView_t *row, uiSettingRowKind_t kind,
	const char *label, const char *value, bool enabled)
{
	row->kind = kind;
	row->label = label;
	row->value = value;
	row->enabled = enabled;
	row->on = false;
}

static void rowCycle(settingRowView_t *row, const char *label,
	const char *value, bool enabled)
{
	rowShow(row, SET_ROWKIND_CYCLE, label, value, enabled);
}

static void rowYesNo(settingRowView_t *row, const char *label, bool value,
	bool enabled)
{
	rowShow(row, SET_ROWKIND_TOGGLE, label, value ? "Yes" : "No", enabled);
	row->on = value;
}

/* A setting stored as "Disable X" reads the positive way round. */
static void rowOnOff(settingRowView_t *row, const char *label, bool on,
	bool enabled)
{
	rowShow(row, SET_ROWKIND_TOGGLE, label, on ? "On" : "Off", enabled);
	row->on = on;
}

static void rowNumber(settingRowView_t *row, const char *label,
	const char *format, int value, bool enabled)
{
	snprintf(row->text, sizeof(row->text), format, value);
	rowShow(row, SET_ROWKIND_CYCLE, label, row->text, enabled);
}

static void rowText(settingRowView_t *row, const char *label,
	const char *value, bool enabled)
{
	rowShow(row, SET_ROWKIND_TEXT, label, value, enabled);
}

static void rowTextNumber(settingRowView_t *row, const char *label,
	int value, bool enabled)
{
	snprintf(row->text, sizeof(row->text), "%i", value);
	rowShow(row, SET_ROWKIND_TEXT, label, row->text, enabled);
}

static void rowAction(settingRowView_t *row, const char *label, bool enabled)
{
	rowShow(row, SET_ROWKIND_ACTION, label, NULL, enabled);
}

/* Label, value and enabled state of one setting, as the six legacy pages
 * drew them. */
static void settingsDescribeRow(int page, int option, ConfigEntry *gameConfig,
	settingRowView_t *row)
{
	bool enabledVideoPatches = swissSettings.disableVideoPatches < 2;
	bool enabledHypervisor = devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->features & FEAT_HYPERVISOR);
	bool emulatedAudioStream = devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->emulable & EMU_AUDIO_STREAMING);
	bool emulatedReadSpeed = devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->emulable & EMU_READ_SPEED);
	bool emulatedEthernet = devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->emulable & EMU_ETHERNET);
	bool enabledCleanBoot = devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR);

	rowAction(row, "", false);
	if(page == PAGE_GLOBAL) {
		bool tvEnable = swissSettings.aveCompat != AVE_RVL_COMPAT;
		bool dvdEnable = deviceHandler_getDeviceAvailable(&__device_dvd);
		bool dtvEnable = !in_range(swissSettings.aveCompat, AVE_N_DOL_COMPAT, AVE_P_DOL_COMPAT);
		bool rt4kEnable = in_range(swissSettings.aveCompat, GCDIGITAL_COMPAT, GCVIDEO_COMPAT);
		switch(option) {
			case SET_SYS_BOOTMODE: rowCycle(row, "System Boot Mode:", swissSettings.sramBoot ? "Production" : "Default", true); break;
			case SET_SYS_SOUND: rowCycle(row, "System Sound:", swissSettings.sramStereo ? "Stereo" : "Mono", true); break;
			case SET_SYS_VIDEO: rowCycle(row, "System Video:", sramVideoStr[swissSettings.sramVideo], tvEnable); break;
			case SET_SCREEN_POS: rowNumber(row, "Screen Position:", "%+i", swissSettings.sramHOffset, true); break;
			case SET_SYS_LANG: rowCycle(row, "System Language:", sramLanguageStr[swissSettings.sramLanguage], true); break;
			case SET_CONFIG_DEV: rowCycle(row, "Configuration Device:", getConfigDeviceName(&swissSettings), true); break;
			case SET_LIBRARY_PATH: rowCycle(row, "Library Path:", getLibraryPathName(&swissSettings), true); break;
			case SET_SAVE_FOLDER: {
				/* The value holds 24 bytes; a longer path shows its end. */
				static char folder[UI_SETLAYOUT_VALUE_TEXT_MAX + 1];
				char path[PATHNAME_MAX + 1];
				size_t length;

				snprintf(path, sizeof(path), "/%s", strcmp(saves_folder(), "/") ? saves_folder() : "");
				length = strlen(path);
				snprintf(folder, sizeof(folder), "%s%s", length > UI_SETLAYOUT_VALUE_TEXT_MAX ? "\205" : "",
					length > UI_SETLAYOUT_VALUE_TEXT_MAX ? path + length - (UI_SETLAYOUT_VALUE_TEXT_MAX - 1) : path);
				rowText(row, "Save Folder:", folder, devices[DEVICE_CONFIG] != NULL);
			}
			break;
			case SET_SWISS_VIDEOMODE:
				snprintf(row->text, sizeof(row->text), "%s%s", getVideoModeString(getVideoModeFromSwissSetting(swissSettings.uiVMode)), swissSettings.uiVMode == 0 ? " (Auto) " : "");
				rowCycle(row, "Swiss Video Mode:", row->text, true);
			break;
			case SET_APPS_VIDEOMODE:
				snprintf(row->text, sizeof(row->text), "%s%s", getVideoModeString(getVideoModeFromSwissSetting(swissSettings.appsVMode ? swissSettings.appsVMode : swissSettings.uiVMode)), swissSettings.appsVMode == 0 ? " (Auto) " : "");
				rowCycle(row, "Apps Video Mode:", row->text, true);
			break;
			case SET_INIT_DRIVE: rowYesNo(row, "Init DVD Drive at startup:", swissSettings.initDVDDriveAtStart, dvdEnable); break;
			case SET_STOP_MOTOR: rowYesNo(row, "Stop DVD Drive motor:", swissSettings.stopMotor, dvdEnable); break;
			case SET_AUDIO_BUFFER: rowCycle(row, "Configure Audio Buffer:", configAudioBufferStr[swissSettings.configAudioBuffer], dvdEnable); break;
			case SET_EXI_SPEED: rowCycle(row, "SD/IDE-EXI Speed:", swissSettings.exiSpeed ? "27 MHz" : "13.5 MHz", true); break;
			case SET_AVE_COMPAT: rowCycle(row, "AVE Compatibility:", aveCompatStr[swissSettings.aveCompat], true); break;
			case SET_FORCE_DTVSTATUS: rowCycle(row, "Force DTV Status:", forceDTVStatusStr[swissSettings.forceDTVStatus], dtvEnable); break;
			case SET_RT4K_OPTIM: rowYesNo(row, "RetroTINK-4K HDMI Input:", swissSettings.rt4kOptim, rt4kEnable); break;
			case SET_DISABLE_RECALIB: rowOnOff(row, "Controller Recalibration:", !swissSettings.disableRecalibration, true); break;
			/* Shown the positive way round; the file keeps Disable PAD Rumble. */
			case SET_DISABLE_RUMBLE: rowOnOff(row, "Controller Rumble:", !swissSettings.disableRumble, true); break;
			case SET_ENABLE_USBGECKO: rowCycle(row, "Enable USB Gecko:", enableUSBGeckoStr[swissSettings.enableUSBGecko], true); break;
			case SET_WAIT_USBGECKO: rowYesNo(row, "Wait for USB Gecko:", swissSettings.waitForUSBGecko, true); break;
			case SET_SIMMEMSIZE: rowCycle(row, "Simulated MRAM Size:", simulatedMemSizeStr[swissSettings.simulatedMemSize], true); break;
			case SET_TAU_CALIB: rowNumber(row, "CPU Temperature Calibration:", "%+i\260C", swissSettings.sramTemperature, is_gamecube()); break;
		}
	}
	else if(page == PAGE_INTERFACE) {
		switch(option) {
			case SET_FILE_MGMT: rowYesNo(row, "File Management:", swissSettings.enableFileManagement, true); break;
			case SET_RECENT_LIST: rowCycle(row, "Recent List:", recentListLevelStr[swissSettings.recentListLevel], true); break;
			case SET_SHOW_HIDDEN: rowYesNo(row, "Show hidden files:", swissSettings.showHiddenFiles, true); break;
			case SET_HIDE_UNK: rowYesNo(row, "Hide unknown file types:", swissSettings.hideUnknownFileTypes, true); break;
			case SET_UI_ANIMS:
				rowCycle(row, "UI Motion:", uiMotionModeStr[UIMotion_ModeFromFlags(
					swissSettings.disableUIAnimations, swissSettings.reduceUIAnimations)], true);
			break;
			case SET_UI_COLOR: rowCycle(row, "Menu Color:", uiColorStr[swissSettings.uiColor], true); break;
			case SET_UI_BACKDROP_COLOR: rowCycle(row, "Backdrop Color:", uiLayerColorStr[swissSettings.uiBackdropColor], true); break;
			case SET_UI_WAVE_COLOR: rowCycle(row, "Wave Color:", uiLayerColorStr[swissSettings.uiWaveColor], true); break;
			case SET_LIBRARY_ICON: rowCycle(row, "Library Icon:", libraryIconStr[swissSettings.libraryIcon], true); break;
			case SET_SOURCE_ICON: rowCycle(row, "Source Icon:", sourceIconStr[swissSettings.sourceIcon], true); break;
			case SET_SETTINGS_ICON: rowCycle(row, "Settings Icon:", settingsIconStr[swissSettings.settingsIcon], true); break;
			case SET_SYSTEM_ICON: rowCycle(row, "System Icon:", systemIconStr[swissSettings.systemIcon], true); break;
			case SET_UP_FACE: rowCycle(row, "Up Face:", upFaceStr[swissSettings.upFace], true); break;
			case SET_LEFT_FACE: rowCycle(row, "Left Face:", leftFaceStr[swissSettings.leftFace], true); break;
			case SET_RIGHT_FACE: rowCycle(row, "Right Face:", rightFaceStr[swissSettings.rightFace], true); break;
			case SET_DOWN_FACE: rowCycle(row, "Down Face:", downFaceStr[swissSettings.downFace], true); break;
			case SET_CUBE: rowCycle(row, "Cube:", swissSettings.cubeStyle ? "Classic" : "Infinite", true); break;
			case SET_IDLE_ANIMATION: rowCycle(row, "Idle Animation:", swissSettings.idleAnimation ? "Sway" : "Calm", true); break;
			case SET_WAVES: rowOnOff(row, "Waves:", !swissSettings.hideWaves, true); break;
			case SET_HOME_CONTROLS: rowOnOff(row, "On-screen Controls:", !swissSettings.hideHomeControls, true); break;
			case SET_FACE_LABELS: rowOnOff(row, "Face Labels:", !swissSettings.hideFaceLabels, true); break;
			case SET_CLOCK_POSITION: rowCycle(row, "Clock:", clockPositionStr[swissSettings.clockPosition], true); break;
			case SET_TEMPERATURE_POSITION: rowCycle(row, "Temperature:", clockPositionStr[swissSettings.temperaturePosition], true); break;
			case SET_WAVE_SPEED: rowCycle(row, "Wave Speed:", waveSpeedStr[swissSettings.waveSpeed], true); break;
			case SET_PANEL_TRANSPARENCY: rowYesNo(row, "Panel Transparency:", !swissSettings.disablePanelTransparency, true); break;
			case SET_ANIMATED_BACKDROP: rowYesNo(row, "Animated Backdrop:", !swissSettings.disableAnimatedBackdrop, true); break;
			case SET_MENU_MUSIC: rowYesNo(row, "Menu Music:", !swissSettings.disableMenuMusic, true); break;
			case SET_MENU_SFX: rowYesNo(row, "Menu Sounds:", !swissSettings.disableMenuSFX, true); break;
			case SET_AUTOBOOT: rowYesNo(row, "Boot without prompts:", swissSettings.autoBoot, true); break;
			case SET_AUTOLOAD: rowText(row, "Load at startup:", getAutoLoadDeviceName(&swissSettings), true); break;
			case SET_FLATTEN_DIR: rowText(row, "Flatten directory:", swissSettings.flattenDir, !swissSettings.libraryFolders); break;
			case SET_LIBRARY_LAYOUT: rowCycle(row, "Library Layout:", libraryLayoutStr[swissSettings.libraryLayout], true); break;
			case SET_LIBRARY_FOLDERS: rowOnOff(row, "Library Folders:", swissSettings.libraryFolders, true); break;
			case SET_DETAIL_SAVES: rowCycle(row, "Saves on Details:", swissSettings.hideDetailSaves ? "Off" : swissSettings.alwaysShowDetailSaves ? "Always" : "On", true); break;
			case SET_MENU_WIDESCREEN: rowYesNo(row, "Menu Widescreen:", swissSettings.menuWidescreen, true); break;
		}
	}
	else if(page == PAGE_NETWORK) {
		bool netEnable = settingsNetAdapter;
		switch(option) {
			case SET_INIT_NET: rowYesNo(row, "Init network at startup:", swissSettings.initNetworkAtStart, !bba_requires_init()); break;
			case SET_BBA_LOCALIP: rowText(row, "IPv4 Address:", swissSettings.bbaLocalIp, netEnable); break;
			case SET_BBA_NETMASK: rowTextNumber(row, "IPv4 Netmask:", swissSettings.bbaNetmask, netEnable); break;
			case SET_BBA_GATEWAY: rowText(row, "IPv4 Gateway:", swissSettings.bbaGateway, netEnable); break;
			case SET_BBA_DHCP: rowYesNo(row, "IPv4 uses DHCP:", swissSettings.bbaUseDhcp, netEnable); break;
			case SET_NTP_SYNC: rowYesNo(row, "Sync Clock:", swissSettings.ntpSync, netEnable); break;
			case SET_NTP_HOSTIP: rowText(row, "NTP Server IP:", swissSettings.ntpHostIp[0] ? swissSettings.ntpHostIp : TIME_SYNC_DEFAULT_SERVER, netEnable); break;
			case SET_TIME_ZONE: rowText(row, "Time Zone:", swissSettings.timeZone[0] ? swissSettings.timeZone : "Keep console's", netEnable); break;
			case SET_FSP_HOSTIP: rowText(row, "FSP Host IP:", swissSettings.fspHostIp, netEnable); break;
			case SET_FSP_PORT: rowTextNumber(row, "FSP Port:", swissSettings.fspPort, netEnable); break;
			case SET_FSP_PASS: rowText(row, "FSP Password:", "*****", netEnable); break;
			case SET_FSP_PMTU: rowTextNumber(row, "FSP Path MTU:", swissSettings.fspPathMtu, netEnable); break;
			case SET_FTP_HOSTIP: rowText(row, "FTP Host IP:", swissSettings.ftpHostIp, netEnable); break;
			case SET_FTP_PORT: rowTextNumber(row, "FTP Port:", swissSettings.ftpPort, netEnable); break;
			case SET_FTP_USER: rowText(row, "FTP Username:", swissSettings.ftpUserName, netEnable); break;
			case SET_FTP_PASS: rowText(row, "FTP Password:", "*****", netEnable); break;
			case SET_FTP_PASV: rowYesNo(row, "FTP PASV Mode:", swissSettings.ftpUsePasv, netEnable); break;
			case SET_SMB_HOSTIP: rowText(row, "SMB Host IP:", swissSettings.smbServerIp, netEnable); break;
			case SET_SMB_SHARE: rowText(row, "SMB Share:", swissSettings.smbShare, netEnable); break;
			case SET_SMB_USER: rowText(row, "SMB Username:", swissSettings.smbUser, netEnable); break;
			case SET_SMB_PASS: rowText(row, "SMB Password:", "*****", netEnable); break;
			case SET_RT4K_HOSTIP: rowText(row, "RetroTINK-4K Host IP:", swissSettings.rt4kHostIp, netEnable); break;
			case SET_RT4K_PORT: rowTextNumber(row, "RetroTINK-4K Port:", swissSettings.rt4kPort, netEnable); break;
		}
	}
	else if(page == PAGE_GAME_GLOBAL) {
		bool emulatedMemoryCard = devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->emulable & EMU_MEMCARD);
		bool dbgEnable = devices[DEVICE_CUR] != &__device_usbgecko && deviceHandler_getDeviceAvailable(&__device_usbgecko);
		switch(option) {
			case SET_IGR: rowCycle(row, "In-Game Reset:", igrTypeStr[swissSettings.igrType], enabledHypervisor); break;
			case SET_BS2BOOT: rowCycle(row, "Load GameCube Main Menu:", bs2BootStr[swissSettings.bs2Boot], true); break;
			case SET_EMULATE_MEMCARD: rowYesNo(row, "Emulate Memory Card:", swissSettings.emulateMemoryCard, emulatedMemoryCard); break;
			case SET_DISABLE_MCPGAMEID: rowCycle(row, "Disable MemCard PRO GameID:", disableMCPGameIDStr[swissSettings.disableMCPGameID], true); break;
			case SET_DISABLE_VIDPATCH: rowCycle(row, "Disable Video Patches:", disableVideoPatchesStr[swissSettings.disableVideoPatches], true); break;
			case SET_FORCE_VIDACTIVE: rowYesNo(row, "Force Video Active:", swissSettings.forceVideoActive, enabledVideoPatches); break;
			case SET_PAUSE_AVOUTPUT: rowYesNo(row, "Pause for resolution change:", swissSettings.pauseAVOutput, enabledHypervisor); break;
			case SET_ALL_CHEATS: rowYesNo(row, "Auto-load cheats:", swissSettings.autoCheats, true); break;
			case SET_WIIRDDBG: rowYesNo(row, "WiiRD debugging:", swissSettings.wiirdDebug, dbgEnable); break;
			case SET_GLOBAL_DEFAULTS: rowAction(row, "Reset to defaults", true); break;
		}
	}
	else if(page == PAGE_GAME_DEFAULTS) {
		switch(option) {
			case SET_DEFAULT_NTSC_VIDEOMODE: rowCycle(row, "Force NTSC Video Mode:", getGameVideoModeString(swissSettings.gameVModeNtsc), enabledVideoPatches); break;
			case SET_DEFAULT_PAL_VIDEOMODE: rowCycle(row, "Force PAL Video Mode:", getGameVideoModeString(swissSettings.gameVModePal), enabledVideoPatches); break;
			case SET_DEFAULT_HORIZ_SCALE: rowCycle(row, "Force Horizontal Scale:", forceHScaleStr[swissSettings.forceHScale], enabledVideoPatches); break;
			case SET_DEFAULT_VERT_OFFSET: rowNumber(row, "Force Vertical Offset:", "%+i", swissSettings.forceVOffset, enabledVideoPatches); break;
			case SET_DEFAULT_VERT_FILTER: rowCycle(row, "Force Vertical Filter:", forceVFilterStr[swissSettings.forceVFilter], enabledVideoPatches); break;
			case SET_DEFAULT_FIELD_RENDER: rowCycle(row, "Force Field Rendering:", forceVJitterStr[swissSettings.forceVJitter], enabledVideoPatches); break;
			case SET_DEFAULT_PIXEL_CENTER: rowCycle(row, "Fix Pixel Center:", fixPixelCenterStr[swissSettings.fixPixelCenter], enabledVideoPatches); break;
			case SET_DEFAULT_ALPHA_DITHER: rowOnOff(row, "Alpha Dithering:", !swissSettings.disableDithering, enabledVideoPatches); break;
			case SET_DEFAULT_ANISO_FILTER: rowYesNo(row, "Force Anisotropic Filter:", swissSettings.forceAnisotropy, true); break;
			case SET_DEFAULT_WIDESCREEN: rowCycle(row, "Force Widescreen:", forceWidescreenStr[swissSettings.forceWidescreen], true); break;
			case SET_DEFAULT_POLL_RATE: rowCycle(row, "Force Polling Rate:", forcePollRateStr[swissSettings.forcePollRate], true); break;
			case SET_DEFAULT_INVERT_CAMERA: rowCycle(row, "Invert Camera Stick:", invertCStickStr[swissSettings.invertCStick], true); break;
			case SET_DEFAULT_SWAP_CAMERA: rowCycle(row, "Swap Camera Stick:", swapCStickStr[swissSettings.swapCStick], true); break;
			case SET_DEFAULT_TRIGGER_LEVEL: rowNumber(row, "Digital Trigger Level:", "%i", swissSettings.triggerLevel, true); break;
			case SET_DEFAULT_AUDIO_STREAM: rowCycle(row, "Emulate Audio Streaming:", emulateAudioStreamStr[swissSettings.emulateAudioStream], emulatedAudioStream); break;
			case SET_DEFAULT_READ_SPEED: rowCycle(row, "Emulate Read Speed:", emulateReadSpeedStr[swissSettings.emulateReadSpeed], emulatedReadSpeed); break;
			case SET_DEFAULT_EMULATE_ETHERNET: rowYesNo(row, "Emulate Broadband Adapter:", swissSettings.emulateEthernet, emulatedEthernet); break;
			case SET_DEFAULT_DISABLE_MEMCARD: rowCycle(row, "Disable Memory Card:", disableMemoryCardStr[swissSettings.disableMemoryCard], enabledHypervisor); break;
			case SET_DEFAULT_DISABLE_HYPERVISOR: rowOnOff(row, "Hypervisor:", !swissSettings.disableHypervisor, enabledCleanBoot); break;
			case SET_DEFAULT_CLEAN_BOOT: rowYesNo(row, "Prefer Clean Boot:", swissSettings.preferCleanBoot, enabledCleanBoot); break;
			case SET_DEFAULT_RT4K_PROFILE: rowNumber(row, "RetroTINK-4K Profile:", "%i", swissSettings.rt4kProfile, is_rt4k_alive()); break;
			case SET_DEFAULT_DEFAULTS: rowAction(row, "Reset to defaults", true); break;
		}
	}
	else if(page == PAGE_GAME) {
		/* Without a game (or one forced to clean boot) the rows are dimmed
		 * and show what a game would start with. */
		static ConfigEntry fallback;
		bool enabled = gameConfig != NULL && !gameConfig->forceCleanBoot;
		const ConfigEntry *game = gameConfig;
		if(!enabled) {
			memset(&fallback, 0, sizeof(fallback));
			config_defaults(&fallback);
			game = &fallback;
		}
		switch(option) {
			case SET_GAME_LANG: rowCycle(row, "Game Language:", sramLanguageStr[game->gameLanguage], enabled); break;
			case SET_FORCE_VIDEOMODE: rowCycle(row, "Force Video Mode:", getGameVideoModeString(game->gameVMode), enabled && enabledVideoPatches); break;
			case SET_HORIZ_SCALE: rowCycle(row, "Force Horizontal Scale:", forceHScaleStr[game->forceHScale], enabled && enabledVideoPatches); break;
			case SET_VERT_OFFSET: rowNumber(row, "Force Vertical Offset:", "%+i", game->forceVOffset, enabled && enabledVideoPatches); break;
			case SET_VERT_FILTER: rowCycle(row, "Force Vertical Filter:", forceVFilterStr[game->forceVFilter], enabled && enabledVideoPatches); break;
			case SET_FIELD_RENDER: rowCycle(row, "Force Field Rendering:", forceVJitterStr[game->forceVJitter], enabled && enabledVideoPatches); break;
			case SET_PIXEL_CENTER: rowCycle(row, "Fix Pixel Center:", fixPixelCenterStr[game->fixPixelCenter], enabled && enabledVideoPatches); break;
			case SET_ALPHA_DITHER: rowOnOff(row, "Alpha Dithering:", !game->disableDithering, enabled && enabledVideoPatches); break;
			case SET_ANISO_FILTER: rowYesNo(row, "Force Anisotropic Filter:", game->forceAnisotropy, enabled); break;
			case SET_WIDESCREEN: rowCycle(row, "Force Widescreen:", forceWidescreenStr[game->forceWidescreen], enabled); break;
			case SET_POLL_RATE: rowCycle(row, "Force Polling Rate:", forcePollRateStr[game->forcePollRate], enabled); break;
			case SET_INVERT_CAMERA: rowCycle(row, "Invert Camera Stick:", invertCStickStr[game->invertCStick], enabled); break;
			case SET_SWAP_CAMERA: rowCycle(row, "Swap Camera Stick:", swapCStickStr[game->swapCStick], enabled); break;
			case SET_TRIGGER_LEVEL: rowNumber(row, "Digital Trigger Level:", "%i", game->triggerLevel, enabled); break;
			case SET_AUDIO_STREAM: rowCycle(row, "Emulate Audio Streaming:", emulateAudioStreamStr[game->emulateAudioStream], enabled && emulatedAudioStream); break;
			case SET_READ_SPEED: rowCycle(row, "Emulate Read Speed:", emulateReadSpeedStr[game->emulateReadSpeed], enabled && emulatedReadSpeed); break;
			case SET_EMULATE_ETHERNET: rowYesNo(row, "Emulate Broadband Adapter:", game->emulateEthernet, enabled && emulatedEthernet); break;
			case SET_DISABLE_MEMCARD: rowCycle(row, "Disable Memory Card:", disableMemoryCardStr[game->disableMemoryCard], enabled && enabledHypervisor); break;
			case SET_DISABLE_HYPERVISOR: rowOnOff(row, "Hypervisor:", !game->disableHypervisor, enabled && enabledCleanBoot); break;
			case SET_CLEAN_BOOT: rowYesNo(row, "Prefer Clean Boot:", game->preferCleanBoot, enabled && enabledCleanBoot); break;
			case SET_RT4K_PROFILE: rowNumber(row, "RetroTINK-4K Profile:", "%i", game->rt4kProfile, enabled && is_rt4k_alive()); break;
			case SET_DEFAULTS: rowAction(row, "Reset to defaults", enabled); break;
		}
	}
}

static bool settingsRowHasList(int page, int option);
static bool settingsIsLiveVideoRow(int page, int option);

/* Left and Right on a video row only step its value: the row shows it and
 * the picture stays as it is until A applies it. These are the five
 * settings that choose the mode as they were before the first step. */
static struct {
	bool pending;
	u8 sramVideo;
	int uiVMode;
	int aveCompat;
	int forceDTVStatus;
	bool rt4kOptim;
} settingsVideo;

/* Whether a video row shows a value that isn't on screen yet. */
static bool settingsVideoStepped(void)
{
	return settingsVideo.pending &&
		(swissSettings.sramVideo != settingsVideo.sramVideo ||
		swissSettings.uiVMode != settingsVideo.uiVMode ||
		swissSettings.aveCompat != settingsVideo.aveCompat ||
		swissSettings.forceDTVStatus != settingsVideo.forceDTVStatus ||
		swissSettings.rt4kOptim != settingsVideo.rt4kOptim);
}

/* The footer's button hints, and the line about the focused row from its
 * help: what its current value does, or what the setting is for. */
static void settingsDescribeFocus(uiSetPageSnapshot_t *page, int view,
	int option, ConfigEntry *gameConfig, const ConfigEntry *gameDefaults)
{
	const uiSetLayout_t *layout = &page->layout;
	const settingsRowRef_t *ref = settingsViewRow(view, option);
	const char *hints[UI_SETLAYOUT_HINT_ITEMS];
	const char *about = NULL;
	char summary[256];
	int count = 0;
	int x = layout->hintX;
	int i;

	if(ref != NULL && ref->page == SETTINGS_ROW_LINK) {
		hints[count++] = "A  Open";
	}
	else if(ref != NULL) {
		settingRowView_t row;
		const char *help = get_tooltip(ref->page, ref->option);

		settingsDescribeRow(ref->page, ref->option, gameConfig, &row);
		if(row.kind == SET_ROWKIND_TEXT) {
			/* A dimmed network row still opens the editor; a dimmed Save
			 * Folder has no Configuration Device to list folders on. */
			if(row.enabled || ref->page == PAGE_NETWORK) {
				hints[count++] = "A  Edit";
			}
		}
		else if(row.kind == SET_ROWKIND_ACTION) {
			if(row.enabled) {
				hints[count++] = "A  Reset";
			}
		}
		else if(settingsIsLiveVideoRow(ref->page, ref->option)) {
			/* A only applies a value Left or Right stepped to. */
			if(settingsVideoStepped()) {
				hints[count++] = "A  Apply";
			}
		}
		else if(row.enabled) {
			hints[count++] = (settingsRowHasList(ref->page, ref->option) ||
				(ref->page == PAGE_GLOBAL && ref->option == SET_LIBRARY_PATH)) ?
				"A  Choose" : "A  Change";
		}
		if(help != NULL) {
			hints[count++] = "Y  Help";
			if(UISetLayout_HelpSummary(help, row.value, summary,
				sizeof(summary)) > 0) {
				about = summary;
			}
		}
		if(ref->page == PAGE_GLOBAL && ref->option == SET_LIBRARY_PATH) {
			DEVICEHANDLER_INTERFACE *sourceDevice =
				getDeviceFromPath(swissSettings.libraryPath);
			if(sourceDevice != NULL) {
				snprintf(summary, sizeof(summary),
					"%s (%s) - A chooses a folder; Left/Right changes device.",
					DeviceDisplayName(sourceDevice),
					swissSettings.libraryPath);
			}
			else {
				snprintf(summary, sizeof(summary),
					"Auto: the first /games found - A chooses a folder instead.");
			}
			about = summary;
		}
		if(settingsIsLiveVideoRow(ref->page, ref->option) &&
			settingsVideoStepped()) {
			about = "Not applied yet: A switches to it, then asks whether to keep it.";
		}
		/* X puts a game's own value back to Game Defaults. */
		if(gameDefaults != NULL && row.enabled &&
			settingsGameRowCustom(gameConfig, gameDefaults, ref->option)) {
			hints[count++] = "X  Default";
		}
	}
	else if(option == UISetLayout_RowCount(view)) {
		hints[count++] = "A  Save";
		about = "Keeps every change and closes Settings.";
	}
	else {
		hints[count++] = "A  Discard";
		about = "Puts back everything changed since Settings opened, and closes it.";
	}
	hints[count++] = view > VIEW_SETUP && view < VIEW_GAME ? "B  Back" : "B  Done";

	if(about != NULL) {
		(void)prepareSettingText(about, sizeof(summary) - 1u,
			UI_SETLAYOUT_ELLIPSIZE_TAIL, layout->descriptionMaxWidth,
			UI_SETLAYOUT_DESCRIPTION_SCALE, UI_SETLAYOUT_DESCRIPTION_SCALE,
			page->description, sizeof(page->description),
			&page->descriptionScale, NULL);
	}
	/* The cheat browser's footer: each hint on its own, spaced apart. */
	for(i = 0; i < count; i++) {
		float scale;

		if(!prepareHintText(hints[i], layout->hintX + layout->hintMaxWidth - x,
			UI_SETLAYOUT_HINT_SCALE, page->hint[page->hintCount],
			sizeof(page->hint[0]), &scale)) {
			break;
		}
		page->hintX[page->hintCount++] = (short)x;
		x += (int)((float)GetHintSizeInPixels(hints[i]) * scale + 0.999f) + 22;
	}
}

uiDrawObj_t* settings_draw_page(int view, int option, ConfigEntry *gameConfig) {
	static uiSetPageSnapshot_t page;
	static ConfigEntry gameDefaults;
	/* Mirrors _CurrentMotionMode() (FrameBufferMagic): Animations off snaps.
	 * Backdrop animation is decorative and cannot weaken primary focus travel. */
	int motionMode = (int)UIMotion_ModeFromFlags(
		swissSettings.disableUIAnimations, swissSettings.reduceUIAnimations);
	bool tagged = view == VIEW_GAME && gameConfig != NULL;
	int i;

	memset(&page, 0, sizeof(page));
	UISetLayout_Compute(view, option, motionMode, &page.layout);
	page.view = view;
	if(tagged) {
		settingsGameDefaults(gameConfig, &gameDefaults);
	}
	drawSettingsChrome(&page, view, option, gameConfig);
	for(i = 0; i < page.layout.visibleRowCount; i++) {
		const settingsRowRef_t *ref =
			&settingsViews[view].rows[page.layout.firstVisibleRow + i];
		settingRowView_t row;

		if(ref->page == SETTINGS_ROW_LINK) {
			rowShow(&row, SET_ROWKIND_LINK, UISetLayout_PageDesc(ref->option)->title,
				setupSummaries[ref->option - VIEW_DISPLAY], true);
		}
		else {
			settingsDescribeRow(ref->page, ref->option, gameConfig, &row);
		}
		/* A game's own values are marked; the rest follow Game Defaults. */
		drawSettingRow(&page, i, &row, tagged &&
			settingsGameRowCustom(gameConfig, &gameDefaults, ref->option),
			settingsColorLayer(ref->page, ref->option) + 1);
	}
	settingsDescribeFocus(&page, view, option, gameConfig,
		tagged ? &gameDefaults : NULL);

	/* One page for the whole session: published once, then updated in
	 * place with the color it shows (see show_settings_view). */
	if(settingsPageEvent == NULL &&
		(settingsPageEvent = DrawSettingsPage(&page)) != NULL) {
		DrawPublish(settingsPageEvent);
	}
	DrawUpdateSettingsPage(settingsPageEvent, &page, (const int[UI_COLOR_LAYERS]) {
		swissSettings.uiColor, swissSettings.uiBackdropColor, swissSettings.uiWaveColor});
	return settingsPageEvent;
}

/* Game Detail's SETTINGS line: the first of a game's own rows as it reads on
 * its page ("Force Video Mode: 480p"), or "" when none is. */
const char *settings_game_custom_first(ConfigEntry *game)
{
	static char text[64];
	static ConfigEntry defaults;
	size_t i;

	text[0] = '\0';
	if(game == NULL) {
		return text;
	}
	settingsGameDefaults(game, &defaults);
	for(i = 0; i < sizeof(gameRows) / sizeof(gameRows[0]); i++) {
		settingRowView_t row;

		if(settingsGameRowCustom(game, &defaults, gameRows[i].option)) {
			settingsDescribeRow(PAGE_GAME, gameRows[i].option, game, &row);
			snprintf(text, sizeof(text), "%s %s", row.label, row.value);
			break;
		}
	}
	return text;
}

void settings_toggle(int page, int option, int direction, ConfigEntry *gameConfig) {
	if(page == PAGE_GLOBAL) {
		switch(option) {
			case SET_SYS_BOOTMODE:
				swissSettings.sramBoot ^= SYS_BOOT_PRODUCTION;
			break;
			case SET_SYS_SOUND:
				swissSettings.sramStereo ^= SYS_SOUND_STEREO;
			break;
			case SET_SYS_VIDEO:
				if(swissSettings.aveCompat != AVE_RVL_COMPAT) {
					swissSettings.sramVideo += direction;
					swissSettings.sramVideo = ((s8)swissSettings.sramVideo + 3) % 3;
				}
			break;
			case SET_SCREEN_POS:
				if(in_range(swissSettings.aveCompat, GCDIGITAL_COMPAT, GCVIDEO_COMPAT)) {
					swissSettings.sramHOffset /= 2;
					swissSettings.sramHOffset += direction;
					swissSettings.sramHOffset *= 2;
				}
				else {
					swissSettings.sramHOffset += direction;
				}
				VIDEO_SetAdjustingValues(swissSettings.sramHOffset, 0);
			break;
			case SET_SYS_LANG:
				swissSettings.sramLanguage += direction;
				swissSettings.sramLanguage = ((s8)swissSettings.sramLanguage + SRAM_LANGUAGE_MAX) % SRAM_LANGUAGE_MAX;
			break;
			case SET_SAVE_FOLDER:
			{
				char folder[PATHNAME_MAX];

				if(devices[DEVICE_CONFIG] != NULL && saves_choose_folder(folder, sizeof(folder))) {
					strlcpy(swissSettings.saveFolder, folder, sizeof(swissSettings.saveFolder));
				}
			}
			break;
			case SET_CONFIG_DEV:
			{
				int curDevicePos = -1;

				// Set it to the first writable device available
				if(swissSettings.configDeviceId == DEVICE_ID_UNK) {
					for(int i = 0; i < MAX_DEVICES; i++) {
						if(allDevices[i] != NULL && (allDevices[i]->features & FEAT_CONFIG_DEVICE)) {
							swissSettings.configDeviceId = allDevices[i]->deviceUniqueId;
							return;
						}
					}
				}

				// get position in allDevices for current save device
				for(int i = 0; i < MAX_DEVICES; i++) {
					if(allDevices[i] != NULL && allDevices[i]->deviceUniqueId == swissSettings.configDeviceId) {
						curDevicePos = i;
						break;
					}
				}

				if(curDevicePos >= 0) {
					if(direction > 0) {
						curDevicePos = allDevices[curDevicePos+1] == NULL ? 0 : curDevicePos+1;
					}
					else {
						curDevicePos = curDevicePos > 0 ? curDevicePos-1 : MAX_DEVICES-1;
					}
					// Go to next writable device
					while((allDevices[curDevicePos] == NULL) || !(allDevices[curDevicePos]->features & FEAT_CONFIG_DEVICE)) {
						curDevicePos += direction;
						curDevicePos = (curDevicePos + MAX_DEVICES) % MAX_DEVICES;
					}
					if(allDevices[curDevicePos] != NULL) {
						swissSettings.configDeviceId = allDevices[curDevicePos]->deviceUniqueId;
					}
				}
			}
			break;
			case SET_LIBRARY_PATH:
			{
				if(direction == 0) {
					choose_library_path(swissSettings.libraryPath,
						sizeof(swissSettings.libraryPath));
					break;
				}
				int curDevicePos = MAX_DEVICES;
				DEVICEHANDLER_INTERFACE *currentSource = getDeviceFromPath(swissSettings.libraryPath);
				if(currentSource != NULL) {
					for(int i = 0; i < MAX_DEVICES; i++) {
						if(allDevices[i] == currentSource) {
							curDevicePos = i;
							break;
						}
					}
				}
				do {
					curDevicePos = (curDevicePos + direction + (MAX_DEVICES + 1)) % (MAX_DEVICES + 1);
				} while(curDevicePos < MAX_DEVICES && (allDevices[curDevicePos] == NULL ||
					!(allDevices[curDevicePos]->features & FEAT_BOOT_GCM) ||
					allDevices[curDevicePos]->initial == NULL ||
					allDevices[curDevicePos] == &__device_dvd));
				if(curDevicePos < MAX_DEVICES) {
					concat_path(swissSettings.libraryPath,
						allDevices[curDevicePos]->initial->name, "games");
				}
				else {
					swissSettings.libraryPath[0] = '\0';
				}
			}
			break;
			case SET_SWISS_VIDEOMODE:
				swissSettings.uiVMode += direction;
				swissSettings.uiVMode = (swissSettings.uiVMode + 9) % 9;
			break;
			case SET_APPS_VIDEOMODE:
				swissSettings.appsVMode += direction;
				swissSettings.appsVMode = (swissSettings.appsVMode + 9) % 9;
			break;
			case SET_INIT_DRIVE:
				if(deviceHandler_getDeviceAvailable(&__device_dvd))
					swissSettings.initDVDDriveAtStart ^= 1;
			break;
			case SET_STOP_MOTOR:
				if(deviceHandler_getDeviceAvailable(&__device_dvd))
					swissSettings.stopMotor ^= 1;
			break;
			case SET_AUDIO_BUFFER:
				if(deviceHandler_getDeviceAvailable(&__device_dvd)) {
					swissSettings.configAudioBuffer += direction;
					swissSettings.configAudioBuffer = (swissSettings.configAudioBuffer + 3) % 3;
				}
			break;
			case SET_EXI_SPEED:
				swissSettings.exiSpeed ^= 1;
			break;
			case SET_AVE_COMPAT:
				swissSettings.aveCompat += direction;
				swissSettings.aveCompat = (swissSettings.aveCompat + AVE_COMPAT_MAX) % AVE_COMPAT_MAX;
			break;
			case SET_FORCE_DTVSTATUS:
				if(!in_range(swissSettings.aveCompat, AVE_N_DOL_COMPAT, AVE_P_DOL_COMPAT)) {
					swissSettings.forceDTVStatus += direction;
					swissSettings.forceDTVStatus = (swissSettings.forceDTVStatus + 3) % 3;
				}
			break;
			case SET_RT4K_OPTIM:
				if(in_range(swissSettings.aveCompat, GCDIGITAL_COMPAT, GCVIDEO_COMPAT)) {
					swissSettings.rt4kOptim ^= 1;
					switch(swissSettings.aveCompat) {
						case GCDIGITAL_COMPAT:
							sprintf(txtbuffer, "In the \223GCDigital Settings\224 menu,\nset \223Use console DE\224 to %s.", swissSettings.rt4kOptim ? "on (4:3)" : "off");
						break;
						case GCVIDEO_COMPAT:
							sprintf(txtbuffer, "In the GCVideo \223Advanced Settings\224\nmenu, set \223Fix Resolution\224 to %s.", swissSettings.rt4kOptim ? "Off" : "On");
						break;
					}
					uiDrawObj_t *msgBox = DrawPublish(DrawMessageBox(D_INFO, txtbuffer));
					wait_press_A();
					DrawDispose(msgBox);
				}
			break;
			case SET_DISABLE_RECALIB:
				swissSettings.disableRecalibration ^= 1;
			break;
			case SET_DISABLE_RUMBLE:
				swissSettings.disableRumble ^= 1;
			break;
			case SET_ENABLE_USBGECKO:
				swissSettings.enableUSBGecko += direction;
				swissSettings.enableUSBGecko = (swissSettings.enableUSBGecko + USBGECKO_MAX) % USBGECKO_MAX;
			break;
			case SET_WAIT_USBGECKO:
				swissSettings.waitForUSBGecko ^= 1;
			break;
			case SET_SIMMEMSIZE:
				do {
					swissSettings.simulatedMemSize += direction;
					swissSettings.simulatedMemSize = (swissSettings.simulatedMemSize + 6) % 6;
				} while(simulatedMemSizeInt[swissSettings.simulatedMemSize] > SYS_GetPhysicalMemSize());
			break;
			case SET_TAU_CALIB:
				if(is_gamecube()) {
					swissSettings.sramTemperature += direction * 4;
					if(swissSettings.sramTemperature < -80) swissSettings.sramTemperature = -80;
					if(swissSettings.sramTemperature > +80) swissSettings.sramTemperature = +80;
					__SYS_SetTAUCalibration(swissSettings.sramTemperature);
				}
			break;
		}
		switch(option) {
			case SET_SYS_VIDEO:
			case SET_SWISS_VIDEOMODE:
			case SET_AVE_COMPAT:
			case SET_FORCE_DTVSTATUS:
			case SET_RT4K_OPTIM:
			{
				// Change Swiss video mode if it was modified.
				GXRModeObj *forcedMode = getVideoModeFromSwissSetting(swissSettings.uiVMode);
				DrawVideoMode(forcedMode);
			}
			break;
		}
	}
	else if(page == PAGE_INTERFACE) {
		switch(option) {
			case SET_FILE_MGMT:
				swissSettings.enableFileManagement ^= 1;
			break;
			case SET_RECENT_LIST:
				swissSettings.recentListLevel += direction;
				swissSettings.recentListLevel = (swissSettings.recentListLevel + 3) % 3;
			break;
			case SET_SHOW_HIDDEN:
				swissSettings.showHiddenFiles ^= 1;
			break;
			case SET_HIDE_UNK:
				swissSettings.hideUnknownFileTypes ^= 1;
			break;
			case SET_UI_ANIMS:
			{
				uiMotionMode_t mode = UIMotion_CycleMode(
					UIMotion_ModeFromFlags(swissSettings.disableUIAnimations,
						swissSettings.reduceUIAnimations), direction);
				swissSettings.disableUIAnimations = mode == UI_MOTION_OFF;
				swissSettings.reduceUIAnimations = mode == UI_MOTION_REDUCED;
			}
			break;
			case SET_UI_COLOR:
				swissSettings.uiColor += direction;
				swissSettings.uiColor = (swissSettings.uiColor + UI_COLOR_MAX) % UI_COLOR_MAX;
			break;
			case SET_UI_BACKDROP_COLOR:
				swissSettings.uiBackdropColor += direction;
				swissSettings.uiBackdropColor = (swissSettings.uiBackdropColor + UI_COLOR_MAX + 1) % (UI_COLOR_MAX + 1);
			break;
			case SET_UI_WAVE_COLOR:
				swissSettings.uiWaveColor += direction;
				swissSettings.uiWaveColor = (swissSettings.uiWaveColor + UI_COLOR_MAX + 1) % (UI_COLOR_MAX + 1);
			break;
			case SET_LIBRARY_ICON:
				swissSettings.libraryIcon += direction;
				swissSettings.libraryIcon = (swissSettings.libraryIcon + UI_HOME_ICON_CHOICES) % UI_HOME_ICON_CHOICES;
			break;
			case SET_SOURCE_ICON:
				swissSettings.sourceIcon += direction;
				swissSettings.sourceIcon = (swissSettings.sourceIcon + UI_HOME_ICON_CHOICES) % UI_HOME_ICON_CHOICES;
			break;
			case SET_SETTINGS_ICON:
				swissSettings.settingsIcon += direction;
				swissSettings.settingsIcon = (swissSettings.settingsIcon + UI_HOME_ICON_CHOICES) % UI_HOME_ICON_CHOICES;
			break;
			case SET_SYSTEM_ICON:
				swissSettings.systemIcon += direction;
				swissSettings.systemIcon = (swissSettings.systemIcon + UI_HOME_ICON_CHOICES) % UI_HOME_ICON_CHOICES;
			break;
			case SET_UP_FACE:
				swissSettings.upFace += direction;
				swissSettings.upFace = (swissSettings.upFace + UI_HOME_FACE_COUNT) % UI_HOME_FACE_COUNT;
			break;
			case SET_LEFT_FACE:
				swissSettings.leftFace += direction;
				swissSettings.leftFace = (swissSettings.leftFace + UI_HOME_FACE_COUNT) % UI_HOME_FACE_COUNT;
			break;
			case SET_RIGHT_FACE:
				swissSettings.rightFace += direction;
				swissSettings.rightFace = (swissSettings.rightFace + UI_HOME_FACE_COUNT) % UI_HOME_FACE_COUNT;
			break;
			case SET_DOWN_FACE:
				swissSettings.downFace += direction;
				swissSettings.downFace = (swissSettings.downFace + UI_HOME_FACE_COUNT) % UI_HOME_FACE_COUNT;
			break;
			case SET_CUBE:
				swissSettings.cubeStyle ^= 1;
			break;
			case SET_CLOCK_POSITION:
				swissSettings.clockPosition += direction;
				swissSettings.clockPosition = (swissSettings.clockPosition + CLOCK_POSITION_MAX) % CLOCK_POSITION_MAX;
			break;
			case SET_TEMPERATURE_POSITION:
				swissSettings.temperaturePosition += direction;
				swissSettings.temperaturePosition = (swissSettings.temperaturePosition + CLOCK_POSITION_MAX) % CLOCK_POSITION_MAX;
			break;
			case SET_WAVE_SPEED:
				swissSettings.waveSpeed += direction;
				swissSettings.waveSpeed = (swissSettings.waveSpeed + WAVE_SPEED_MAX) % WAVE_SPEED_MAX;
			break;
			case SET_LIBRARY_FOLDERS:
				config_set_library_folders(!swissSettings.libraryFolders);
			break;
			case SET_DETAIL_SAVES:
			{
				int mode = swissSettings.hideDetailSaves ? 2 : swissSettings.alwaysShowDetailSaves ? 1 : 0;
				mode = (mode + direction + 3) % 3;
				swissSettings.hideDetailSaves = mode == 2;
				swissSettings.alwaysShowDetailSaves = mode == 1;
			}
			break;
			case SET_IDLE_ANIMATION:
				swissSettings.idleAnimation ^= 1;
			break;
			case SET_WAVES:
				swissSettings.hideWaves ^= 1;
			break;
			case SET_HOME_CONTROLS:
				swissSettings.hideHomeControls ^= 1;
			break;
			case SET_FACE_LABELS:
				swissSettings.hideFaceLabels ^= 1;
			break;
			case SET_PANEL_TRANSPARENCY:
				swissSettings.disablePanelTransparency ^= 1;
			break;
			case SET_ANIMATED_BACKDROP:
				swissSettings.disableAnimatedBackdrop ^= 1;
			break;
			case SET_MENU_MUSIC:
				swissSettings.disableMenuMusic ^= 1;
				menuaudio_apply_settings();
			break;
			case SET_MENU_SFX:
				swissSettings.disableMenuSFX ^= 1;
				menuaudio_apply_settings();
			break;
			case SET_AUTOBOOT:
				swissSettings.autoBoot ^= 1;
			break;
			case SET_AUTOLOAD:
				select_device(DEVICE_DEST);
				if(devices[DEVICE_DEST] != NULL) {
					if(devices[DEVICE_DEST] != devices[DEVICE_CUR]) {
						deviceHandler_setStatEnabled(0);
						if(devices[DEVICE_DEST]->init(devices[DEVICE_DEST]->initial)) {
							deviceHandler_setStatEnabled(1);
							strlcpy(swissSettings.autoload, devices[DEVICE_DEST]->initial->name, sizeof(swissSettings.autoload));
							return;
						}
						deviceHandler_setStatEnabled(1);
					}
					select_dest_dir(devices[DEVICE_DEST]->initial, swissSettings.autoload);

					if(devices[DEVICE_DEST] != devices[DEVICE_CUR]) {
						devices[DEVICE_DEST]->deinit(devices[DEVICE_DEST]->initial);
					}
					devices[DEVICE_DEST] = NULL;
				}
			break;
			case SET_FLATTEN_DIR:
				DrawGetTextEntry(ENTRYMODE_NUMERIC|ENTRYMODE_ALPHA, "Flatten directory", &swissSettings.flattenDir, sizeof(swissSettings.flattenDir) - 1);
			break;
			case SET_LIBRARY_LAYOUT:
				swissSettings.libraryLayout += direction;
				swissSettings.libraryLayout = (swissSettings.libraryLayout + UI_GAMEFLOW_LAYOUT_COUNT) % UI_GAMEFLOW_LAYOUT_COUNT;
			break;
			case SET_MENU_WIDESCREEN:
				swissSettings.menuWidescreen ^= 1;
			break;
		}
	}
	else if(page == PAGE_NETWORK) {
		switch(option) {
			case SET_INIT_NET:
				if(!bba_requires_init())
					swissSettings.initNetworkAtStart ^= 1;
			break;
			case SET_BBA_LOCALIP:
				DrawGetTextEntry(ENTRYMODE_IP, "IPv4 Address", &swissSettings.bbaLocalIp, sizeof(swissSettings.bbaLocalIp) - 1);
			break;
			case SET_BBA_NETMASK:
				DrawGetTextEntry(ENTRYMODE_NUMERIC, "IPv4 Netmask", &swissSettings.bbaNetmask, 2);
			break;
			case SET_BBA_GATEWAY:
				DrawGetTextEntry(ENTRYMODE_IP, "IPv4 Gateway", &swissSettings.bbaGateway, sizeof(swissSettings.bbaGateway) - 1);
			break;
			case SET_BBA_DHCP:
				swissSettings.bbaUseDhcp ^= 1;
			break;
			case SET_NTP_SYNC:
				swissSettings.ntpSync ^= 1;
			break;
			case SET_NTP_HOSTIP:
				DrawGetTextEntry(ENTRYMODE_IP, "NTP Server IP", &swissSettings.ntpHostIp, sizeof(swissSettings.ntpHostIp) - 1);
			break;
			case SET_TIME_ZONE:
				DrawGetTextEntry(ENTRYMODE_NUMERIC|ENTRYMODE_ALPHA, "Time Zone", &swissSettings.timeZone, sizeof(swissSettings.timeZone) - 1);
			break;
			case SET_FSP_HOSTIP:
				DrawGetTextEntry(ENTRYMODE_IP, "FSP Host IP", &swissSettings.fspHostIp, sizeof(swissSettings.fspHostIp) - 1);
			break;
			case SET_FSP_PORT:
				DrawGetTextEntry(ENTRYMODE_NUMERIC, "FSP Port", &swissSettings.fspPort, 5);
			break;
			case SET_FSP_PASS:
				DrawGetTextEntry(ENTRYMODE_NUMERIC|ENTRYMODE_ALPHA|ENTRYMODE_MASKED, "FSP Password", &swissSettings.fspPassword, sizeof(swissSettings.fspPassword) - 1);
			break;
			case SET_FSP_PMTU:
				DrawGetTextEntry(ENTRYMODE_NUMERIC, "FSP Path MTU", &swissSettings.fspPathMtu, 4);
			break;
			case SET_FTP_HOSTIP:
				DrawGetTextEntry(ENTRYMODE_IP, "FTP Host IP", &swissSettings.ftpHostIp, sizeof(swissSettings.ftpHostIp) - 1);
			break;
			case SET_FTP_PORT:
				DrawGetTextEntry(ENTRYMODE_NUMERIC, "FTP Port", &swissSettings.ftpPort, 5);
			break;
			case SET_FTP_USER:
				DrawGetTextEntry(ENTRYMODE_NUMERIC|ENTRYMODE_ALPHA, "FTP Username", &swissSettings.ftpUserName, sizeof(swissSettings.ftpUserName) - 1);
			break;
			case SET_FTP_PASS:
				DrawGetTextEntry(ENTRYMODE_NUMERIC|ENTRYMODE_ALPHA|ENTRYMODE_MASKED, "FTP Password", &swissSettings.ftpPassword, sizeof(swissSettings.ftpPassword) - 1);
			break;
			case SET_FTP_PASV:
				swissSettings.ftpUsePasv ^= 1;
			break;
			case SET_SMB_HOSTIP:
				DrawGetTextEntry(ENTRYMODE_IP, "SMB Host IP", &swissSettings.smbServerIp, sizeof(swissSettings.smbServerIp) - 1);
			break;
			case SET_SMB_SHARE:
				DrawGetTextEntry(ENTRYMODE_NUMERIC|ENTRYMODE_ALPHA, "SMB Share", &swissSettings.smbShare, sizeof(swissSettings.smbShare) - 1);
			break;
			case SET_SMB_USER:
				DrawGetTextEntry(ENTRYMODE_NUMERIC|ENTRYMODE_ALPHA, "SMB Username", &swissSettings.smbUser, sizeof(swissSettings.smbUser) - 1);
			break;
			case SET_SMB_PASS:
				DrawGetTextEntry(ENTRYMODE_NUMERIC|ENTRYMODE_ALPHA|ENTRYMODE_MASKED, "SMB Password", &swissSettings.smbPassword, sizeof(swissSettings.smbPassword) - 1);
			break;
			case SET_RT4K_HOSTIP:
				DrawGetTextEntry(ENTRYMODE_IP, "RetroTINK-4K Host IP", &swissSettings.rt4kHostIp, sizeof(swissSettings.rt4kHostIp) - 1);
			break;
			case SET_RT4K_PORT:
				DrawGetTextEntry(ENTRYMODE_NUMERIC, "RetroTINK-4K Port", &swissSettings.rt4kPort, 5);
			break;
		}
	}
	else if(page == PAGE_GAME_GLOBAL) {
		switch(option) {
			case SET_IGR:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->features & FEAT_HYPERVISOR)) {
					swissSettings.igrType += direction;
					swissSettings.igrType = (swissSettings.igrType + 3) % 3;
				}
			break;
			case SET_BS2BOOT:
				swissSettings.bs2Boot += direction;
				swissSettings.bs2Boot = (swissSettings.bs2Boot + 4) % 4;
			break;
			case SET_EMULATE_MEMCARD:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->emulable & EMU_MEMCARD))
					swissSettings.emulateMemoryCard ^= 1;
			break;
			case SET_DISABLE_MCPGAMEID:
				swissSettings.disableMCPGameID += direction;
				swissSettings.disableMCPGameID = (swissSettings.disableMCPGameID + 4) % 4;
			break;
			case SET_DISABLE_VIDPATCH:
				swissSettings.disableVideoPatches += direction;
				swissSettings.disableVideoPatches = (swissSettings.disableVideoPatches + 3) % 3;
			break;
			case SET_FORCE_VIDACTIVE:
				if(swissSettings.disableVideoPatches < 2)
					swissSettings.forceVideoActive ^= 1;
			break;
			case SET_PAUSE_AVOUTPUT:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->features & FEAT_HYPERVISOR))
					swissSettings.pauseAVOutput ^=1;
			break;
			case SET_ALL_CHEATS:
				swissSettings.autoCheats ^=1;
			break;
			case SET_WIIRDDBG:
				if(devices[DEVICE_CUR] != &__device_usbgecko && deviceHandler_getDeviceAvailable(&__device_usbgecko))
					swissSettings.wiirdDebug ^=1;
			break;
			case SET_GLOBAL_DEFAULTS:
				if(direction == 0) {
					swissSettings.igrType = 0;
					swissSettings.bs2Boot = 0;
					swissSettings.emulateMemoryCard = 0;
					swissSettings.disableMCPGameID = 0;
					swissSettings.disableVideoPatches = 0;
					swissSettings.forceVideoActive = 0;
					swissSettings.pauseAVOutput = 0;
					swissSettings.autoCheats = 0;
					swissSettings.wiirdDebug = 0;
				}
			break;
		}
	}
	else if(page == PAGE_GAME_DEFAULTS) {
		switch(option) {
			case SET_DEFAULT_NTSC_VIDEOMODE:
				if(swissSettings.disableVideoPatches < 2) {
					swissSettings.gameVModeNtsc += direction;
					swissSettings.gameVModeNtsc = (swissSettings.gameVModeNtsc + 8) % 8;
					if(!getDTVStatus()) {
						while(in_range(swissSettings.gameVModeNtsc, 4, 7)) {
							swissSettings.gameVModeNtsc += direction;
							swissSettings.gameVModeNtsc = (swissSettings.gameVModeNtsc + 8) % 8;
						}
					}
					else if(swissSettings.aveCompat != CMPV_DOL_COMPAT) {
						while(in_range(swissSettings.gameVModeNtsc, 6, 7)) {
							swissSettings.gameVModeNtsc += direction;
							swissSettings.gameVModeNtsc = (swissSettings.gameVModeNtsc + 8) % 8;
						}
					}
				}
			break;
			case SET_DEFAULT_PAL_VIDEOMODE:
				if(swissSettings.disableVideoPatches < 2) {
					swissSettings.gameVModePal += direction;
					swissSettings.gameVModePal = (swissSettings.gameVModePal + 15) % 15;
					if(!getDTVStatus()) {
						while(in_range(swissSettings.gameVModePal, 4, 7) || in_range(swissSettings.gameVModePal, 11, 14)) {
							swissSettings.gameVModePal += direction;
							swissSettings.gameVModePal = (swissSettings.gameVModePal + 15) % 15;
						}
					}
					else if(swissSettings.aveCompat != CMPV_DOL_COMPAT) {
						while(in_range(swissSettings.gameVModePal, 6, 7) || in_range(swissSettings.gameVModePal, 13, 14)) {
							swissSettings.gameVModePal += direction;
							swissSettings.gameVModePal = (swissSettings.gameVModePal + 15) % 15;
						}
					}
				}
			break;
			case SET_DEFAULT_HORIZ_SCALE:
				if(swissSettings.disableVideoPatches < 2) {
					swissSettings.forceHScale += direction;
					swissSettings.forceHScale = (swissSettings.forceHScale + 9) % 9;
				}
			break;
			case SET_DEFAULT_VERT_OFFSET:
				if(swissSettings.disableVideoPatches < 2)
					swissSettings.forceVOffset += direction;
			break;
			case SET_DEFAULT_VERT_FILTER:
				if(swissSettings.disableVideoPatches < 2) {
					swissSettings.forceVFilter += direction;
					swissSettings.forceVFilter = (swissSettings.forceVFilter + 4) % 4;
				}
			break;
			case SET_DEFAULT_FIELD_RENDER:
				if(swissSettings.disableVideoPatches < 2) {
					swissSettings.forceVJitter += direction;
					swissSettings.forceVJitter = (swissSettings.forceVJitter + 4) % 4;
				}
			break;
			case SET_DEFAULT_PIXEL_CENTER:
				if(swissSettings.disableVideoPatches < 2) {
					swissSettings.fixPixelCenter += direction;
					swissSettings.fixPixelCenter = (swissSettings.fixPixelCenter + 3) % 3;
				}
			break;
			case SET_DEFAULT_ALPHA_DITHER:
				if(swissSettings.disableVideoPatches < 2)
					swissSettings.disableDithering ^= 1;
			break;
			case SET_DEFAULT_ANISO_FILTER:
				swissSettings.forceAnisotropy ^= 1;
			break;
			case SET_DEFAULT_WIDESCREEN:
				swissSettings.forceWidescreen += direction;
				swissSettings.forceWidescreen = (swissSettings.forceWidescreen + 3) % 3;
			break;
			case SET_DEFAULT_POLL_RATE:
				swissSettings.forcePollRate += direction;
				swissSettings.forcePollRate = (swissSettings.forcePollRate + 13) % 13;
			break;
			case SET_DEFAULT_INVERT_CAMERA:
				swissSettings.invertCStick += direction;
				swissSettings.invertCStick = (swissSettings.invertCStick + 4) % 4;
			break;
			case SET_DEFAULT_SWAP_CAMERA:
				swissSettings.swapCStick += direction;
				swissSettings.swapCStick = (swissSettings.swapCStick + 4) % 4;
			break;
			case SET_DEFAULT_TRIGGER_LEVEL:
				swissSettings.triggerLevel += direction * 10;
				swissSettings.triggerLevel = (swissSettings.triggerLevel + 210) % 210;
			break;
			case SET_DEFAULT_AUDIO_STREAM:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->emulable & EMU_AUDIO_STREAMING)) {
					swissSettings.emulateAudioStream += direction;
					swissSettings.emulateAudioStream = (swissSettings.emulateAudioStream + 3) % 3;
				}
			break;
			case SET_DEFAULT_READ_SPEED:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->emulable & EMU_READ_SPEED)) {
					swissSettings.emulateReadSpeed += direction;
					swissSettings.emulateReadSpeed = (swissSettings.emulateReadSpeed + 3) % 3;
				}
			break;
			case SET_DEFAULT_EMULATE_ETHERNET:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->emulable & EMU_ETHERNET))
					swissSettings.emulateEthernet ^= 1;
			break;
			case SET_DEFAULT_DISABLE_MEMCARD:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->features & FEAT_HYPERVISOR)) {
					swissSettings.disableMemoryCard += direction;
					swissSettings.disableMemoryCard = (swissSettings.disableMemoryCard + 3) % 3;
				}
			break;
			case SET_DEFAULT_DISABLE_HYPERVISOR:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR))
					swissSettings.disableHypervisor ^= 1;
			break;
			case SET_DEFAULT_CLEAN_BOOT:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR))
					swissSettings.preferCleanBoot ^= 1;
			break;
			case SET_DEFAULT_RT4K_PROFILE:
				if(is_rt4k_alive()) {
					swissSettings.rt4kProfile += direction;
					swissSettings.rt4kProfile = (swissSettings.rt4kProfile + 13) % 13;
				}
			break;
			case SET_DEFAULT_DEFAULTS:
				if(direction == 0) {
					swissSettings.gameVModeNtsc = 0;
					swissSettings.gameVModePal = 0;
					swissSettings.forceHScale = 0;
					swissSettings.forceVOffset = 0;
					swissSettings.forceVFilter = 0;
					swissSettings.forceVJitter = 0;
					swissSettings.fixPixelCenter = 0;
					swissSettings.disableDithering = 0;
					swissSettings.forceAnisotropy = 0;
					swissSettings.forceWidescreen = 0;
					swissSettings.forcePollRate = 0;
					swissSettings.invertCStick = 0;
					swissSettings.swapCStick = 0;
					swissSettings.triggerLevel = 0;
					swissSettings.emulateAudioStream = 1;
					swissSettings.emulateReadSpeed = 0;
					swissSettings.emulateEthernet = 0;
					swissSettings.disableMemoryCard = 0;
					swissSettings.disableHypervisor = 0;
					swissSettings.preferCleanBoot = 0;
					swissSettings.rt4kProfile = 0;
				}
			break;
		}
		if ((option == SET_DEFAULT_AUDIO_STREAM && swissSettings.emulateAudioStream > 1) ||
			(option == SET_DEFAULT_EMULATE_ETHERNET && swissSettings.emulateEthernet)) {
			uiDrawObj_t *msgBox = DrawPublish(DrawMessageBox(D_WARN, "Turning this on globally may cause certain\ngames to crash from resource exhaustion."));
			wait_press_A();
			DrawDispose(msgBox);
		}
	}
	else if(page == PAGE_GAME && gameConfig != NULL && !gameConfig->forceCleanBoot) {
		switch(option) {
			case SET_GAME_LANG:
				gameConfig->gameLanguage += direction;
				gameConfig->gameLanguage = (gameConfig->gameLanguage + 9) % 9;
			break;
			case SET_FORCE_VIDEOMODE:
				if(swissSettings.disableVideoPatches < 2) {
					gameConfig->gameVMode += direction;
					gameConfig->gameVMode = (gameConfig->gameVMode + 15) % 15;
					if(!getDTVStatus()) {
						while(in_range(gameConfig->gameVMode, 4, 7) || in_range(gameConfig->gameVMode, 11, 14)) {
							gameConfig->gameVMode += direction;
							gameConfig->gameVMode = (gameConfig->gameVMode + 15) % 15;
						}
					}
					else if(swissSettings.aveCompat != CMPV_DOL_COMPAT) {
						while(in_range(gameConfig->gameVMode, 6, 7) || in_range(gameConfig->gameVMode, 13, 14)) {
							gameConfig->gameVMode += direction;
							gameConfig->gameVMode = (gameConfig->gameVMode + 15) % 15;
						}
					}
				}
			break;
			case SET_HORIZ_SCALE:
				if(swissSettings.disableVideoPatches < 2) {
					gameConfig->forceHScale += direction;
					gameConfig->forceHScale = (gameConfig->forceHScale + 9) % 9;
				}
			break;
			case SET_VERT_OFFSET:
				if(swissSettings.disableVideoPatches < 2)
					gameConfig->forceVOffset += direction;
			break;
			case SET_VERT_FILTER:
				if(swissSettings.disableVideoPatches < 2) {
					gameConfig->forceVFilter += direction;
					gameConfig->forceVFilter = (gameConfig->forceVFilter + 4) % 4;
				}
			break;
			case SET_FIELD_RENDER:
				if(swissSettings.disableVideoPatches < 2) {
					gameConfig->forceVJitter += direction;
					gameConfig->forceVJitter = (gameConfig->forceVJitter + 4) % 4;
				}
			break;
			case SET_PIXEL_CENTER:
				if(swissSettings.disableVideoPatches < 2) {
					gameConfig->fixPixelCenter += direction;
					gameConfig->fixPixelCenter = (gameConfig->fixPixelCenter + 3) % 3;
				}
			break;
			case SET_ALPHA_DITHER:
				if(swissSettings.disableVideoPatches < 2)
					gameConfig->disableDithering ^= 1;
			break;
			case SET_ANISO_FILTER:
				gameConfig->forceAnisotropy ^= 1;
			break;
			case SET_WIDESCREEN:
				gameConfig->forceWidescreen += direction;
				gameConfig->forceWidescreen = (gameConfig->forceWidescreen + 3) % 3;
			break;
			case SET_POLL_RATE:
				gameConfig->forcePollRate += direction;
				gameConfig->forcePollRate = (gameConfig->forcePollRate + 13) % 13;
			break;
			case SET_INVERT_CAMERA:
				gameConfig->invertCStick += direction;
				gameConfig->invertCStick = (gameConfig->invertCStick + 4) % 4;
			break;
			case SET_SWAP_CAMERA:
				gameConfig->swapCStick += direction;
				gameConfig->swapCStick = (gameConfig->swapCStick + 4) % 4;
			break;
			case SET_TRIGGER_LEVEL:
				gameConfig->triggerLevel += direction * 10;
				gameConfig->triggerLevel = (gameConfig->triggerLevel + 210) % 210;
			break;
			case SET_AUDIO_STREAM:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->emulable & EMU_AUDIO_STREAMING)) {
					gameConfig->emulateAudioStream += direction;
					gameConfig->emulateAudioStream = (gameConfig->emulateAudioStream + 3) % 3;
				}
			break;
			case SET_READ_SPEED:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->emulable & EMU_READ_SPEED)) {
					gameConfig->emulateReadSpeed += direction;
					gameConfig->emulateReadSpeed = (gameConfig->emulateReadSpeed + 3) % 3;
				}
			break;
			case SET_EMULATE_ETHERNET:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->emulable & EMU_ETHERNET))
					gameConfig->emulateEthernet ^= 1;
			break;
			case SET_DISABLE_MEMCARD:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->features & FEAT_HYPERVISOR)) {
					gameConfig->disableMemoryCard += direction;
					gameConfig->disableMemoryCard = (gameConfig->disableMemoryCard + 3) % 3;
				}
			break;
			case SET_DISABLE_HYPERVISOR:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR))
					gameConfig->disableHypervisor ^= 1;
			break;
			case SET_CLEAN_BOOT:
				if(devices[DEVICE_CUR] == NULL || (devices[DEVICE_CUR]->location & LOC_DVD_CONNECTOR))
					gameConfig->preferCleanBoot ^= 1;
			break;
			case SET_RT4K_PROFILE:
				if(is_rt4k_alive()) {
					gameConfig->rt4kProfile += direction;
					gameConfig->rt4kProfile = (gameConfig->rt4kProfile + 13) % 13;
				}
			break;
			case SET_DEFAULTS:
				if(direction == 0)
					settingsResetGame(gameConfig);
			break;
		}
	}
}

#define SETTINGS_DIGITAL_INPUT_MASK (BUTTON_RIGHT | BUTTON_LEFT | \
	BUTTON_UP | BUTTON_DOWN | BUTTON_B | BUTTON_A | BUTTON_Y | \
	BUTTON_R | BUTTON_L | BUTTON_X)
#define SETTINGS_MENU_INPUT_POLICY (UI_MENU_INPUT_AXIS_BOTH | \
	UI_MENU_INPUT_REPEAT)

static u32 settingsMenuInputElapsedMicroseconds(u32 *lastRetrace)
{
	u32 currentRetrace;
	u32 elapsedRetraces;
	float retraceRate;
	float elapsed;

	if(lastRetrace == NULL) {
		return 0u;
	}
	currentRetrace = VIDEO_GetRetraceCount();
	elapsedRetraces = currentRetrace - *lastRetrace;
	*lastRetrace = currentRetrace;
	if(elapsedRetraces == 0u) {
		return 0u;
	}
	retraceRate = VIDEO_GetRetraceRate();
	if(!isfinite(retraceRate) || retraceRate < 1.0f) {
		retraceRate = 60.0f;
	}
	elapsed = (float)elapsedRetraces * 1000000.0f / retraceRate;
	/* The mapper intentionally consumes no more than 50 ms per poll. Clamp
	 * before lrintf so a long menu pause cannot overflow target 32-bit long. */
	if(elapsed >= (float)UI_MENU_INPUT_MAX_ELAPSED_US) {
		return UI_MENU_INPUT_MAX_ELAPSED_US;
	}
	return elapsed <= 0.0f ? 0u : (u32)lrintf(elapsed);
}

static u32 settingsButtonForMenuDirection(uiMenuInputDirection_t direction)
{
	switch(direction) {
		case UI_MENU_INPUT_LEFT:
			return BUTTON_LEFT;
		case UI_MENU_INPUT_RIGHT:
			return BUTTON_RIGHT;
		case UI_MENU_INPUT_UP:
			return BUTTON_UP;
		case UI_MENU_INPUT_DOWN:
			return BUTTON_DOWN;
		case UI_MENU_INPUT_NONE:
		default:
			return 0u;
	}
}

static u32 settingsWaitForInput(uiMenuInputState_t *menuInput,
	u32 *lastRetrace, bool *wasDigital)
{
	while(1) {
		u32 buttons = padsButtonsHeld() & SETTINGS_DIGITAL_INPUT_MASK;
		uiMenuInputDirection_t analog = padsMenuInputPoll(menuInput,
			settingsMenuInputElapsedMicroseconds(lastRetrace),
			SETTINGS_MENU_INPUT_POLICY, buttons != 0u);

		if(buttons != 0u) {
			*wasDigital = true;
			return buttons;
		}
		buttons = settingsButtonForMenuDirection(analog);
		if(buttons != 0u) {
			*wasDigital = false;
			return buttons;
		}
		VIDEO_WaitVSync();
	}
}

/* Keep sampling inhibited through the release edge. This consumes a stick
 * moved during a digital hold instead of leaking a deferred analog action. */
static void settingsInhibitThroughDigitalRelease(
	uiMenuInputState_t *menuInput, u32 *lastRetrace)
{
	while(1) {
		u32 buttons = padsButtonsHeld() & SETTINGS_DIGITAL_INPUT_MASK;
		(void)padsMenuInputPoll(menuInput,
			settingsMenuInputElapsedMicroseconds(lastRetrace),
			SETTINGS_MENU_INPUT_POLICY, true);
		if(buttons == 0u) {
			return;
		}
		VIDEO_WaitVSync();
	}
}

/* A held D-pad direction repeats on the stick's schedule: once after
 * UI_MENU_INPUT_INITIAL_REPEAT_US, then every UI_MENU_INPUT_REPEAT_US. Returns
 * true when the same directions are still held at that point; any other
 * button, or letting go, ends the repeat. */
static bool settingsHoldToRepeat(uiMenuInputState_t *menuInput,
	u32 *lastRetrace, u32 held, bool *repeating)
{
	const u32 directions = BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT |
		BUTTON_RIGHT;
	u32 due = *repeating ? UI_MENU_INPUT_REPEAT_US :
		UI_MENU_INPUT_INITIAL_REPEAT_US;
	u32 waited = 0u;

	*repeating = false;
	if(held == 0u || (held & ~directions) != 0u) {
		return false;
	}
	while((padsButtonsHeld() & SETTINGS_DIGITAL_INPUT_MASK) == held) {
		u32 elapsed = settingsMenuInputElapsedMicroseconds(lastRetrace);

		(void)padsMenuInputPoll(menuInput, elapsed,
			SETTINGS_MENU_INPUT_POLICY, true);
		waited += elapsed;
		if(waited >= due) {
			*repeating = true;
			return true;
		}
		VIDEO_WaitVSync();
	}
	return false;
}

/* Rows whose value chooses the Swiss video mode: A applies it. */
static bool settingsIsLiveVideoRow(int page, int option)
{
	return page == PAGE_GLOBAL && (option == SET_SYS_VIDEO ||
		option == SET_SWISS_VIDEOMODE || option == SET_AVE_COMPAT ||
		option == SET_FORCE_DTVSTATUS || option == SET_RT4K_OPTIM);
}

/* Rows A activates as an action (text entry, reset) instead of changing a
 * value; every other row is a choice that A advances like Right. */
static bool settingsRowIsAction(int page, int option)
{
	switch(page) {
		case PAGE_GLOBAL:
			return option == SET_SAVE_FOLDER || option == SET_LIBRARY_PATH;
		case PAGE_INTERFACE:
			return in_range(option, SET_AUTOLOAD, SET_FLATTEN_DIR);
		case PAGE_NETWORK:
			return in_range(option, SET_BBA_LOCALIP, SET_BBA_GATEWAY) ||
				in_range(option, SET_NTP_HOSTIP, SET_FTP_PASS) ||
				in_range(option, SET_SMB_HOSTIP, SET_RT4K_PORT);
		case PAGE_GAME_GLOBAL:
			return option == SET_GLOBAL_DEFAULTS;
		case PAGE_GAME_DEFAULTS:
			return option == SET_DEFAULT_DEFAULTS;
		case PAGE_GAME:
			return option == SET_DEFAULTS;
	}
	return false;
}

/* Rows that put a whole screen back to its defaults. */
static bool settingsRowIsReset(int page, int option)
{
	return (page == PAGE_GAME_GLOBAL && option == SET_GLOBAL_DEFAULTS) ||
		(page == PAGE_GAME_DEFAULTS && option == SET_DEFAULT_DEFAULTS) ||
		(page == PAGE_GAME && option == SET_DEFAULTS);
}

/* A reset undoes every row on the screen at once, so it asks first. */
static bool settingsConfirmReset(void)
{
	bool released = false;
	bool confirmed = false;
	uiDrawObj_t *box = DrawPublish(DrawMessageBox(D_WARN,
		"Reset everything on this screen to its default?\n"
		"A  RESET    B  KEEP"));

	while(1) {
		u32 held = padsButtonsHeld() & SETTINGS_DIGITAL_INPUT_MASK;

		/* The press that chose the row must be let go first. */
		if(!released) {
			released = held == 0u;
		}
		else if(held & BUTTON_A) {
			confirmed = true;
			break;
		}
		else if(held & BUTTON_B) {
			break;
		}
		VIDEO_WaitVSync();
	}
	DrawDispose(box);
	return confirmed;
}

/* True once anything differs from the snapshot taken when Settings opened. */
static bool settingsChanged(const ConfigEntry *config)
{
	return memcmp(&swissSettings, &tempSettings, sizeof(SwissSettings)) != 0 ||
		(config != NULL && memcmp(config, &tempConfig, sizeof(ConfigEntry)) != 0);
}

#define SETTINGS_VIDEO_KEEP_US 10000000u

/* A live video change can leave the TV without a picture, so it only stays
 * if A is pressed within ten seconds. B, or no answer, changes it back. */
static bool settingsKeepVideoMode(const settingRowView_t *row)
{
	u32 retrace = VIDEO_GetRetraceCount();
	u32 waited = 0u;
	int shown = -1;
	bool released = false;
	bool keep = false;
	uiDrawObj_t *box = NULL;

	while(waited < SETTINGS_VIDEO_KEEP_US) {
		int seconds = (int)((SETTINGS_VIDEO_KEEP_US - waited + 999999u) /
			1000000u);
		u32 held = padsButtonsHeld() & SETTINGS_DIGITAL_INPUT_MASK;

		if(seconds != shown) {
			/* DrawMessageBox copies into txtbuffer, so format elsewhere. */
			char message[128];

			/* Three lines: a fourth runs past the 125 px box. The first names
			 * the new value, as the row behind shows it. The last is the
			 * buttons, which DrawMessageBox draws as icons. */
			snprintf(message, sizeof(message), "Keep %s %s?\n"
				"It changes back by itself in %d s.\n"
				"A  KEEP    B  CHANGE BACK", row->label, row->value, seconds);
			box = box == NULL ?
				DrawPublish(DrawMessageBox(D_WARN, message)) :
				DrawRepublish(box, DrawMessageBox(D_WARN, message));
			shown = seconds;
		}
		/* The press that changed the mode must be let go first. */
		if(!released) {
			released = held == 0u;
		}
		else if(held & BUTTON_A) {
			keep = true;
			break;
		}
		else if(held & BUTTON_B) {
			break;
		}
		VIDEO_WaitVSync();
		waited += settingsMenuInputElapsedMicroseconds(&retrace);
	}
	DrawDispose(box);
	return keep;
}

/* Puts back the settings a video row's steps, or a mode not kept, changed. */
static void settingsVideoRestore(void)
{
	swissSettings.sramVideo = settingsVideo.sramVideo;
	swissSettings.uiVMode = settingsVideo.uiVMode;
	swissSettings.aveCompat = settingsVideo.aveCompat;
	swissSettings.forceDTVStatus = settingsVideo.forceDTVStatus;
	swissSettings.rt4kOptim = settingsVideo.rt4kOptim;
	settingsVideo.pending = false;
}

/* A row another setting owns for now: Flatten directory while Library
 * Folders sets it. */
static bool settingsRowLocked(int page, int option)
{
	return page == PAGE_INTERFACE && option == SET_FLATTEN_DIR &&
		swissSettings.libraryFolders;
}

/* Changes one row's value. A video row only steps it; A applies it. */
static void settingsChangeValue(int page, int option, int direction,
	ConfigEntry *config)
{
	if(settingsRowLocked(page, option)) {
		return;
	}
	if(!settingsIsLiveVideoRow(page, option)) {
		settings_toggle(page, option, direction, config);
		return;
	}
	if(!settingsVideo.pending) {
		settingsVideo.pending = true;
		settingsVideo.sramVideo = swissSettings.sramVideo;
		settingsVideo.uiVMode = swissSettings.uiVMode;
		settingsVideo.aveCompat = swissSettings.aveCompat;
		settingsVideo.forceDTVStatus = swissSettings.forceDTVStatus;
		settingsVideo.rt4kOptim = swissSettings.rt4kOptim;
	}
	/* settings_toggle switches to the new mode itself: hold that back. */
	DrawVideoModeDefer(true);
	settings_toggle(page, option, direction, config);
	DrawVideoModeDefer(false);
}

/* A on a video row switches to the value Left and Right stepped to, and it
 * stays only if A is pressed again within ten seconds. Any change asks, not
 * only a new mode: AVE N-DOL and RetroTINK-4K change the signal in place, and
 * DrawVideoMode re-applies an unchanged mode object (updateVideoMode) for the
 * switch and for the change back alike. */
static void settingsApplyVideoMode(int page, int option, ConfigEntry *config)
{
	GXRModeObj *before = getVideoMode();
	settingRowView_t row;

	if(!settingsVideoStepped()) {
		settingsVideo.pending = false;
		return;
	}
	DrawVideoMode(getVideoModeFromSwissSetting(swissSettings.uiVMode));
	settingsDescribeRow(page, option, config, &row);
	if(settingsKeepVideoMode(&row)) {
		settingsVideo.pending = false;
	}
	else {
		settingsVideoRestore();
		DrawVideoMode(before);
	}
}

/* Choice rows with four or more values: A lists them all to pick from.
 * Left and Right still step one value at a time. The field lets the list
 * show the values in order with the current one in place. */
typedef struct {
	u8 page;
	u8 option;
	bool game;	/* the field is in the game's ConfigEntry */
	u16 offset;
	u8 size;
} settingsPickerRow_t;

#define PICK_SETTING(page, option, field) {page, option, false, \
	offsetof(SwissSettings, field), sizeof(((SwissSettings *)0)->field)}
#define PICK_GAME(option, field) {PAGE_GAME, option, true, \
	offsetof(ConfigEntry, field), sizeof(((ConfigEntry *)0)->field)}
static const settingsPickerRow_t settingsPickerRows[] = {
	PICK_SETTING(PAGE_GLOBAL, SET_SYS_LANG, sramLanguage),
	PICK_SETTING(PAGE_GLOBAL, SET_APPS_VIDEOMODE, appsVMode),
	PICK_SETTING(PAGE_GLOBAL, SET_ENABLE_USBGECKO, enableUSBGecko),
	PICK_SETTING(PAGE_GLOBAL, SET_SIMMEMSIZE, simulatedMemSize),
	PICK_SETTING(PAGE_INTERFACE, SET_UI_COLOR, uiColor),
	PICK_SETTING(PAGE_INTERFACE, SET_UI_BACKDROP_COLOR, uiBackdropColor),
	PICK_SETTING(PAGE_INTERFACE, SET_UI_WAVE_COLOR, uiWaveColor),
	PICK_SETTING(PAGE_INTERFACE, SET_LIBRARY_LAYOUT, libraryLayout),
	PICK_SETTING(PAGE_INTERFACE, SET_LIBRARY_ICON, libraryIcon),
	PICK_SETTING(PAGE_INTERFACE, SET_SOURCE_ICON, sourceIcon),
	PICK_SETTING(PAGE_INTERFACE, SET_SETTINGS_ICON, settingsIcon),
	PICK_SETTING(PAGE_INTERFACE, SET_SYSTEM_ICON, systemIcon),
	PICK_SETTING(PAGE_INTERFACE, SET_UP_FACE, upFace),
	PICK_SETTING(PAGE_INTERFACE, SET_LEFT_FACE, leftFace),
	PICK_SETTING(PAGE_INTERFACE, SET_RIGHT_FACE, rightFace),
	PICK_SETTING(PAGE_INTERFACE, SET_DOWN_FACE, downFace),
	PICK_SETTING(PAGE_GAME_GLOBAL, SET_BS2BOOT, bs2Boot),
	PICK_SETTING(PAGE_GAME_GLOBAL, SET_DISABLE_MCPGAMEID, disableMCPGameID),
	PICK_SETTING(PAGE_GAME_DEFAULTS, SET_DEFAULT_NTSC_VIDEOMODE, gameVModeNtsc),
	PICK_SETTING(PAGE_GAME_DEFAULTS, SET_DEFAULT_PAL_VIDEOMODE, gameVModePal),
	PICK_SETTING(PAGE_GAME_DEFAULTS, SET_DEFAULT_HORIZ_SCALE, forceHScale),
	PICK_SETTING(PAGE_GAME_DEFAULTS, SET_DEFAULT_VERT_FILTER, forceVFilter),
	PICK_SETTING(PAGE_GAME_DEFAULTS, SET_DEFAULT_FIELD_RENDER, forceVJitter),
	PICK_SETTING(PAGE_GAME_DEFAULTS, SET_DEFAULT_POLL_RATE, forcePollRate),
	PICK_SETTING(PAGE_GAME_DEFAULTS, SET_DEFAULT_INVERT_CAMERA, invertCStick),
	PICK_SETTING(PAGE_GAME_DEFAULTS, SET_DEFAULT_SWAP_CAMERA, swapCStick),
	PICK_SETTING(PAGE_GAME_DEFAULTS, SET_DEFAULT_RT4K_PROFILE, rt4kProfile),
	PICK_GAME(SET_GAME_LANG, gameLanguage),
	PICK_GAME(SET_FORCE_VIDEOMODE, gameVMode),
	PICK_GAME(SET_HORIZ_SCALE, forceHScale),
	PICK_GAME(SET_VERT_FILTER, forceVFilter),
	PICK_GAME(SET_FIELD_RENDER, forceVJitter),
	PICK_GAME(SET_POLL_RATE, forcePollRate),
	PICK_GAME(SET_INVERT_CAMERA, invertCStick),
	PICK_GAME(SET_SWAP_CAMERA, swapCStick),
	PICK_GAME(SET_RT4K_PROFILE, rt4kProfile),
};
#undef PICK_SETTING
#undef PICK_GAME

static const settingsPickerRow_t *settingsPickerFor(int page, int option)
{
	size_t i;

	for(i = 0; i < sizeof(settingsPickerRows) / sizeof(settingsPickerRows[0]); i++) {
		if(settingsPickerRows[i].page == page &&
			settingsPickerRows[i].option == option) {
			return &settingsPickerRows[i];
		}
	}
	return NULL;
}

static bool settingsRowHasList(int page, int option)
{
	return settingsPickerFor(page, option) != NULL;
}

static u32 settingsPickerValue(const settingsPickerRow_t *pick,
	const ConfigEntry *config)
{
	const u8 *field = (pick->game ? (const u8 *)config :
		(const u8 *)&swissSettings) + pick->offset;
	u8 byte;
	u16 half;
	u32 word;

	switch(pick->size) {
		case 1: memcpy(&byte, field, 1); return byte;
		case 2: memcpy(&half, field, 2); return half;
		default: memcpy(&word, field, 4); return word;
	}
}

#define SETTINGS_PICKER_MAX 16
typedef struct {
	int count;
	int current;	/* the entry set when the list opened */
	u32 value[SETTINGS_PICKER_MAX];
	char text[SETTINGS_PICKER_MAX][32];
} settingsPicker_t;

/* Lists a row's values by stepping the settings with the same code Right
 * uses, so modes Right skips stay out, then puts the settings back. Values
 * with the same name (the two 150 Hz rates) are listed once. */
static int settingsPickerLoad(const settingsPickerRow_t *pick,
	ConfigEntry *config, settingsPicker_t *list)
{
	static SwissSettings savedSettings;
	static ConfigEntry savedConfig;
	u32 start = settingsPickerValue(pick, config);
	int step, i, j;

	memcpy(&savedSettings, &swissSettings, sizeof(SwissSettings));
	if(config != NULL) {
		memcpy(&savedConfig, config, sizeof(ConfigEntry));
	}
	list->count = 0;
	for(step = 0; step < SETTINGS_PICKER_MAX; step++) {
		settingRowView_t row;
		u32 value = settingsPickerValue(pick, config);
		bool named = false;

		if(step > 0 && value == start) {
			break;
		}
		settingsDescribeRow(pick->page, pick->option, config, &row);
		for(i = 0; i < list->count; i++) {
			named = named || !strcmp(list->text[i], row.value);
		}
		if(!named) {
			list->value[list->count] = value;
			snprintf(list->text[list->count], sizeof(list->text[0]), "%s", row.value);
			list->count++;
		}
		settings_toggle(pick->page, pick->option, 1, config);
		if(settingsPickerValue(pick, config) == value) {
			break;
		}
	}
	memcpy(&swissSettings, &savedSettings, sizeof(SwissSettings));
	if(config != NULL) {
		memcpy(config, &savedConfig, sizeof(ConfigEntry));
	}
	/* The walk began at the current value; list in value order. */
	for(i = 1; i < list->count; i++) {
		for(j = i; j > 0 && list->value[j - 1] > list->value[j]; j--) {
			u32 value = list->value[j];
			char text[sizeof(list->text[0])];

			memcpy(text, list->text[j], sizeof(text));
			list->value[j] = list->value[j - 1];
			memcpy(list->text[j], list->text[j - 1], sizeof(text));
			list->value[j - 1] = value;
			memcpy(list->text[j - 1], text, sizeof(text));
		}
	}
	list->current = 0;
	for(i = 0; i < list->count; i++) {
		if(list->value[i] == start) {
			list->current = i;
		}
	}
	return list->count;
}

/* The value list as the video thread draws it: a card over the page with
 * the row's name, its values, and the one set now marked CURRENT. */
static void settingsDrawPicker(const char *label, const settingsPicker_t *list,
	int focus, uiSetListSnapshot_t *out)
{
	char title[UI_SETLAYOUT_LABEL_BUFFER_SIZE];
	int i;

	memset(out, 0, sizeof(*out));
	UISetLayout_ComputeList(list->count, focus, &out->layout);
	(void)UISetLayout_Label(label, title, sizeof(title));
	(void)prepareSettingText(title, sizeof(title) - 1u,
		UI_SETLAYOUT_ELLIPSIZE_TAIL, out->layout.titleMaxWidth,
		UI_SETLAYOUT_CARD_TITLE_SCALE, UI_SETLAYOUT_ROW_TEXT_FLOOR,
		out->title, sizeof(out->title), &out->titleScale, NULL);
	for(i = 0; i < out->layout.visibleCount; i++) {
		(void)prepareSettingText(list->text[out->layout.first + i],
			sizeof(list->text[0]) - 1u, UI_SETLAYOUT_ELLIPSIZE_TAIL,
			out->layout.rowTextMaxWidth, UI_SETLAYOUT_LABEL_SCALE,
			UI_SETLAYOUT_ROW_TEXT_FLOOR, out->value[i], sizeof(out->value[i]),
			&out->valueScale[i], NULL);
	}
	out->current = list->current - out->layout.first;
	if(out->current >= out->layout.visibleCount) {
		out->current = -1;
	}
	for(i = 0; i < 2; i++) {
		(void)prepareHintText(i == 0 ? "A  Choose" : "B  Cancel",
			(out->layout.hintRightX - out->layout.hintX) / 2,
			UI_SETLAYOUT_HINT_SCALE, out->hint[i], sizeof(out->hint[i]),
			&out->hintScale[i]);
	}
}

/* A on a picker row: every value in a list. A picks the focused one, B
 * keeps the current value. Returns whether the value changed. */
static bool settingsPick(const settingsPickerRow_t *pick, ConfigEntry *config,
	uiMenuInputState_t *menuInput, u32 *lastRetrace)
{
	static settingsPicker_t list;
	static uiSetListSnapshot_t shown;
	settingRowView_t row;
	uiDrawObj_t *box = NULL;
	bool repeating = false;
	int focus, step, chosen = -1;
	u32 target;

	settingsDescribeRow(pick->page, pick->option, config, &row);
	if(!row.enabled || settingsPickerLoad(pick, config, &list) < 2) {
		return false;
	}
	focus = list.current;
	/* The A that opened the list must not also choose from it. */
	settingsInhibitThroughDigitalRelease(menuInput, lastRetrace);
	while(1) {
		bool wasDigital;
		u32 btns;

		settingsDrawPicker(row.label, &list, focus, &shown);
		if(box == NULL && (box = DrawSettingsList(&shown)) != NULL) {
			DrawPublish(box);
		}
		/* A color's list shows each color as the focus reaches it. */
		DrawUpdateSettingsList(box, &shown,
			settingsColorLayer(pick->page, pick->option), (int)list.value[focus]);
		btns = settingsWaitForInput(menuInput, lastRetrace, &wasDigital);
		if(btns & BUTTON_UP) {
			focus = MAX(0, focus - 1);
		}
		if(btns & BUTTON_DOWN) {
			focus = MIN(list.count - 1, focus + 1);
		}
		if(btns & (BUTTON_A | BUTTON_B)) {
			chosen = (btns & BUTTON_A) ? focus : -1;
			settingsInhibitThroughDigitalRelease(menuInput, lastRetrace);
			break;
		}
		if(!wasDigital) {
			repeating = false;
		}
		else if(!settingsHoldToRepeat(menuInput, lastRetrace, btns, &repeating)) {
			settingsInhibitThroughDigitalRelease(menuInput, lastRetrace);
		}
	}
	DrawDispose(box);
	if(chosen < 0 || chosen == list.current) {
		return false;
	}
	/* Step to the chosen value with Right's code, as Right would. */
	target = list.value[chosen];
	for(step = 0; step < SETTINGS_PICKER_MAX &&
		settingsPickerValue(pick, config) != target; step++) {
		settings_toggle(pick->page, pick->option, 1, config);
	}
	return true;
}

/* These legacy setting arms open a child input/message loop. Reinitialize the
 * analog policy after they return so movement inside that child cannot become
 * a surprise Settings action. */
static bool settingsInputMayBlock(int page, int option, u32 buttons)
{
	bool horizontal = (buttons & (BUTTON_LEFT | BUTTON_RIGHT)) != 0u;
	bool activate = (buttons & BUTTON_A) != 0u;

	if(activate && settingsPickerFor(page, option) != NULL) {
		return true;
	}

	/* A video row asks to keep its mode on A only: Left and Right just step
	 * the value, so holding them scrolls through the values. */
	if(page == PAGE_GLOBAL) {
		return ((horizontal || activate) && (option == SET_RT4K_OPTIM ||
			option == SET_SAVE_FOLDER)) ||
			(activate && option == SET_LIBRARY_PATH) ||
			(activate && settingsIsLiveVideoRow(page, option));
	}
	if(page == PAGE_INTERFACE) {
		return (horizontal || activate) && in_range(option, SET_AUTOLOAD, SET_FLATTEN_DIR);
	}
	if(page == PAGE_NETWORK && (horizontal || activate)) {
		return in_range(option, SET_BBA_LOCALIP, SET_BBA_GATEWAY) ||
			in_range(option, SET_NTP_HOSTIP, SET_FTP_PASS) ||
			in_range(option, SET_SMB_HOSTIP, SET_RT4K_PORT);
	}
	if(page == PAGE_GAME_DEFAULTS && (horizontal || activate)) {
		return option == SET_DEFAULT_AUDIO_STREAM ||
			option == SET_DEFAULT_EMULATE_ETHERNET ||
			(activate && option == SET_DEFAULT_DEFAULTS);
	}
	if(page == PAGE_GAME) {
		return activate && option == SET_DEFAULTS;
	}
	return false;
}

/* Legacy entry points name a (page, option) setting: open the view that
 * shows it. A game's settings open on their own. */
int show_settings(int page, int option, ConfigEntry *config) {
	int view;
	int row;

	for(view = VIEW_QUICK; view <= VIEW_GAME; view++) {
		/* A game's rows live only in its own view. */
		if((view == VIEW_GAME) != (page == PAGE_GAME)) {
			continue;
		}
		/* The whole table: show_settings_view hides what it must. */
		for(row = 0; row < settingsViews[view].count; row++) {
			const settingsRowRef_t *ref = &settingsViews[view].rows[row];
			if(ref->page == page && ref->option == option) {
				return show_settings_view(view, row, config);
			}
		}
	}
	return show_settings_view(page == PAGE_GAME ? VIEW_GAME : VIEW_QUICK, 0, config);
}

int show_settings_view(int view, int option, ConfigEntry *config) {
	uiMenuInputState_t menuInput;
	u32 menuInputRetrace;
	bool digitalRepeat = false;

	if(view < 0 || view >= UI_SETLAYOUT_PAGE_COUNT) {
		view = VIEW_QUICK;
	}
	wait_network();
	settingsNetAdapter = net_initialized || bba_exists(LOC_ANY);
	UISetLayout_HideTail(VIEW_NETWORK, settingsNetAdapter ? 0 : SETTINGS_SYNC_CLOCK_ROWS);
	if(option >= UISetLayout_RowCount(view)) {
		option = 0;
	}
	// Copy current settings to a temp copy in case the user cancels out
	if(config != NULL) {
		memcpy(&tempConfig, config, sizeof(ConfigEntry));
	}
	memcpy(&tempSettings, &swissSettings, sizeof(SwissSettings));

	GXRModeObj *oldmode = getVideoMode();
	/* A new session publishes a new page; Save and Discard dispose it. */
	settingsPageEvent = NULL;
	menuInputRetrace = VIDEO_GetRetraceCount();
	UIMenuInput_Init(&menuInput);
	/* The caller has already moved the scene to Settings, which hides the
	 * screen it came from: the page goes up now, not once the press that
	 * opened it is let go, or the bare background shows in between. */
	settings_draw_page(view, option, config);
	/* The press that opened Settings (A on Home, X on Game Detail) must not
	 * also act here: a held X would put a game's first row back to its
	 * default. */
	if(padsButtonsHeld() & SETTINGS_DIGITAL_INPUT_MASK) {
		settingsInhibitThroughDigitalRelease(&menuInput,
			&menuInputRetrace);
	}
	while(1) {
		bool wasDigital;
		bool inputMayBlock;
		int inputView;
		int inputOption;
		int rows = UISetLayout_RowCount(view);
		const settingsRowRef_t *ref = settingsViewRow(view, option);
		bool onSetting = ref != NULL && ref->page != SETTINGS_ROW_LINK;
		uiDrawObj_t* settingsPage = settings_draw_page(view, option, config);
		u32 btns = settingsWaitForInput(&menuInput, &menuInputRetrace,
			&wasDigital);
		/* A video value Left or Right stepped to waits for A on its row.
		 * Any other button but Y, or any button off a video row, puts it
		 * back before it acts, so a mode that was never on screen can't be
		 * saved: startup applies the saved mode without asking. */
		if(settingsVideo.pending && (!onSetting ||
			!settingsIsLiveVideoRow(ref->page, ref->option) ||
			(btns & ~(BUTTON_LEFT | BUTTON_RIGHT | BUTTON_A | BUTTON_Y)) != 0u)) {
			settingsVideoRestore();
		}
		inputView = view;
		inputOption = option;
		inputMayBlock = onSetting &&
			settingsInputMayBlock(ref->page, ref->option, btns);
		if(btns & BUTTON_Y) {
			char *tooltip = onSetting ? get_tooltip(ref->page, ref->option) : NULL;
			if(tooltip) {
				uiDrawObj_t* tooltipBox = DrawPublish(DrawSettingsHelp(tooltip));
				while(padsButtonsHeld() & BUTTON_Y) {
					(void)padsMenuInputPoll(&menuInput,
						settingsMenuInputElapsedMicroseconds(
							&menuInputRetrace),
						SETTINGS_MENU_INPUT_POLICY, true);
					VIDEO_WaitVSync();
				}
				while(!((padsButtonsHeld() & BUTTON_Y) ||
					(padsButtonsHeld() & BUTTON_B))) {
					(void)padsMenuInputPoll(&menuInput,
						settingsMenuInputElapsedMicroseconds(
							&menuInputRetrace),
						SETTINGS_MENU_INPUT_POLICY, true);
					VIDEO_WaitVSync();
				}
				(void)padsMenuInputPoll(&menuInput,
					settingsMenuInputElapsedMicroseconds(
						&menuInputRetrace),
					SETTINGS_MENU_INPUT_POLICY, true);
				DrawDispose(tooltipBox);
				UIMenuInput_Init(&menuInput);
			}
		}
		// Right/Left change a setting, or move between Save and Discard
		if(btns & BUTTON_RIGHT) {
			if(option >= rows) {
				if(option < rows + 1) option++;
			}
			else if(onSetting) {
				settingsChangeValue(ref->page, ref->option, 1, config);
			}
		}
		if(btns & BUTTON_LEFT) {
			if(option > rows) {
				option--;
			}
			else if(option < rows && onSetting) {
				settingsChangeValue(ref->page, ref->option, -1, config);
			}
		}
		// X in a game's settings puts an enabled row back to its Game
		// Defaults value
		if((btns & BUTTON_X) && view == VIEW_GAME && onSetting &&
			config != NULL) {
			settingRowView_t row;
			settingsDescribeRow(ref->page, ref->option, config, &row);
			if(row.enabled) {
				settingsUseDefault(config, ref->option);
			}
		}
		if((btns & BUTTON_DOWN) && option < rows + 1)
			option++;
		if((btns & BUTTON_UP) && option > 0)
			option--;
		// L and R move between the three tabs and wrap; a game's own
		// settings have none
		if(UISetLayout_PageDesc(view)->tab != UI_SETLAYOUT_NO_TAB) {
			int tab = UISetLayout_PageDesc(view)->tab;
			if(btns & BUTTON_R) {
				view = (tab + 1) % UI_SETLAYOUT_TAB_COUNT; option = 0;
			}
			if(btns & BUTTON_L) {
				view = (tab + UI_SETLAYOUT_TAB_COUNT - 1) % UI_SETLAYOUT_TAB_COUNT; option = 0;
			}
		}
		// B backs out of a Setup section. Anywhere else it leaves: Save &
		// Exit when something changed, otherwise just close. Discard & Exit
		// stays on the rail to undo.
		if(btns & BUTTON_B) {
			if(view > VIEW_SETUP && view < VIEW_GAME) {
				option = view - VIEW_DISPLAY;
				view = VIEW_SETUP;
				btns &= ~BUTTON_A;
			}
			else {
				option = settingsChanged(config) ?
					UISetLayout_RowCount(view) : UISetLayout_RowCount(view) + 1;
				btns |= BUTTON_A;
			}
		}
		// Handle all options/buttons here
		if((btns & BUTTON_A)) {
			if(option == UISetLayout_RowCount(view)) {
				uiDrawObj_t *msgBox = DrawPublish(DrawProgressBar(true, 0, "Saving changes\205"));
				// Save settings to SRAM
				swissSettings.sram60Hz = getTVFormat() == VI_EURGB60;
				swissSettings.sramProgressive = getScanMode() == VI_PROGRESSIVE;
				if(in_range(swissSettings.aveCompat, GCDIGITAL_COMPAT, GCVIDEO_COMPAT)) {
					swissSettings.sramHOffset &= ~1;
				}
				VIDEO_SetAdjustingValues(swissSettings.sramHOffset, 0);
				// SRAM keeps naming the Configuration Device that Settings
				// opened with (tempSettings) until the settings are saved on
				// the one chosen here.
				u8 chosenConfigDevice = swissSettings.configDeviceId;
				swissSettings.configDeviceId = tempSettings.configDeviceId;
				updateSRAM(&swissSettings, true);
				swissSettings.configDeviceId = chosenConfigDevice;
				// Update environment
				config_update_environ();
				// Update our .ini (in memory)
				if(config != NULL) {
					config_defaults_from(&tempConfig, &tempSettings);
					config_update_game(config, &tempConfig, true);
				}
				// flush settings to .ini
				if(config_update_global(true)) {
					// Saved on the chosen device: SRAM may name it now.
					updateSRAM(&swissSettings, true);
					rt4k_init();
					msgBox = DrawRepublish(msgBox, DrawMessageBox(D_INFO, "Successfully saved configuration!"));
					sleep(1);
					DrawDispose(msgBox);
				}
				else {
					// Not saved on the chosen device: go back to the one
					// Settings opened with, which SRAM still names.
					swissSettings.configDeviceId = tempSettings.configDeviceId;
					msgBox = DrawRepublish(msgBox, DrawMessageBox(D_INFO, "Failed to save configuration!"));
					sleep(1);
					DrawDispose(msgBox);
				}
				/* The page stays up until the press is let go: the
				 * caller's scene takes over only once this returns. */
				settingsInhibitThroughDigitalRelease(&menuInput,
					&menuInputRetrace);
				DrawDispose(settingsPage);
				return 1;
			}
			if(option == UISetLayout_RowCount(view) + 1) {
				// Exit without saving (revert)
				if(config != NULL) {
					memcpy(config, &tempConfig, sizeof(ConfigEntry));
				}
				memcpy(&swissSettings, &tempSettings, sizeof(SwissSettings));
				menuaudio_apply_settings();
				VIDEO_SetAdjustingValues(swissSettings.sramHOffset, 0);
				__SYS_SetTAUCalibration(swissSettings.sramTemperature);
				DrawVideoMode(oldmode);
				settingsInhibitThroughDigitalRelease(&menuInput,
					&menuInputRetrace);
				DrawDispose(settingsPage);
				return 0;
			}
			// A opens a Setup section, runs a text or reset row, applies
			// a video row's stepped value, and changes any other choice row
			// the way Right does
			if(view == inputView && option == inputOption && ref != NULL) {
				if(ref->page == SETTINGS_ROW_LINK) {
					view = ref->option; option = 0;
				}
				else if(settingsRowLocked(ref->page, ref->option)) {
					/* Another setting owns it for now: nothing to do. */
				}
				else if(settingsRowIsAction(ref->page, ref->option)) {
					if(!settingsRowIsReset(ref->page, ref->option) ||
						settingsConfirmReset()) {
						settings_toggle(ref->page, ref->option, 0, config);
					}
				}
				else if(settingsPickerFor(ref->page, ref->option) != NULL) {
					settingsPick(settingsPickerFor(ref->page, ref->option), config,
						&menuInput, &menuInputRetrace);
				}
				else if(settingsIsLiveVideoRow(ref->page, ref->option)) {
					settingsApplyVideoMode(ref->page, ref->option, config);
				}
				else {
					settingsChangeValue(ref->page, ref->option, 1, config);
				}
			}
		}
		if(view != inputView || inputMayBlock) {
			UIMenuInput_Init(&menuInput);
		}
		/* A child prompt may return with its dismissing A press held even when
		 * analog opened it. Consume that release just like a digital action.
		 * A held D-pad direction repeats instead, on the stick's schedule. */
		if(inputMayBlock || !wasDigital) {
			digitalRepeat = false;
		}
		if(inputMayBlock || (wasDigital && !settingsHoldToRepeat(&menuInput,
			&menuInputRetrace, btns, &digitalRepeat))) {
			settingsInhibitThroughDigitalRelease(&menuInput,
				&menuInputRetrace);
		}
	}
}
