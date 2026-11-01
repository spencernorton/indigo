#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <gccore.h>
#include <ogc/exi.h>
#include <ogc/libversion.h>
#include <ogc/machine/processor.h>
#include "deviceHandler.h"
#include "FrameBufferMagic.h"
#include "IPLFontWrite.h"
#include "swiss.h"
#include "settings.h"
#include "main.h"
#include "ata.h"
#include "exi.h"
#include "bba.h"
#include "flippy.h"
#include "gcloader.h"
#include "wkf.h"
#include "ui_system_info.h"

#define INFO_TEXT_CAPACITY 256u
#define INFO_FITTED_CAPACITY 192u

const float exiSpeeds[] = {
	27.0f/32.0f,
	27.0f/16.0f,
	27.0f/8.0f,
	27.0f/4.0f,
	27.0f/2.0f,
	27.0f/1.0f,
	54.0f
};

static const GXColor systemTitleColor = {239, 235, 255, 255};
static const GXColor systemValueColor = {226, 220, 255, 255};
static const GXColor systemLabelColor = {174, 163, 224, 255};
static const GXColor systemMutedColor = {151, 142, 190, 255};

static const char *infoSafe(const char *text)
{
	return text != NULL && text[0] != '\0' ? text : "UNAVAILABLE";
}

static void infoCopy(char *out, size_t capacity, const char *text)
{
	if(out == NULL || capacity == 0u) {
		return;
	}
	(void)snprintf(out, capacity, "%s", infoSafe(text));
}

static void infoAppend(char *out, size_t capacity, const char *text)
{
	size_t used;

	if(out == NULL || capacity == 0u || text == NULL) {
		return;
	}
	used = strlen(out);
	if(used < capacity - 1u) {
		(void)snprintf(out + used, capacity - used, "%s", text);
	}
}

static void infoGetDeviceInfoString(u32 location, char *out, size_t capacity)
{
	DEVICEHANDLER_INTERFACE *device = getDeviceByLocation(location);

	if(device == &__device_mcp_a &&
		deviceHandler_getDeviceAvailable(&__device_card_a)) {
		device = &__device_card_a;
	}
	else if(device == &__device_mcp_b &&
		deviceHandler_getDeviceAvailable(&__device_card_b)) {
		device = &__device_card_b;
	}
	if(location == bba_exists(LOC_ANY)) {
		s32 exiSpeed;
		if(getExiSpeedByLocation(location, &exiSpeed) && exiSpeed >= 0 &&
			exiSpeed < (s32)(sizeof(exiSpeeds) / sizeof(exiSpeeds[0]))) {
			(void)snprintf(out, capacity, "%.3g MHz %s (%s)",
				exiSpeeds[exiSpeed], infoSafe(getHwNameByLocation(location)),
				infoSafe(bba_address_str()));
		}
		else {
			(void)snprintf(out, capacity, "%s (%s)",
				infoSafe(getHwNameByLocation(location)),
				infoSafe(bba_address_str()));
		}
	}
	else if(device == &__device_aram) {
		formatBytes(out, __io_aram.numberOfSectors * __io_aram.bytesPerSector,
			0, false);
		infoAppend(out, capacity, " ");
		infoAppend(out, capacity, device->hwName);
	}
	else if(device == &__device_card_a || device == &__device_card_b) {
		s32 slot;
		s32 memSize;
		s32 sectorSize;
		if(getExiDeviceByLocation(location, &slot, NULL) &&
			CARD_ProbeEx(slot, &memSize, &sectorSize) == CARD_ERROR_READY &&
			sectorSize > 0) {
			(void)snprintf(out, capacity, "%s %i", infoSafe(device->hwName),
				(memSize << 20 >> 3) / sectorSize - 5);
		}
		else {
			infoCopy(out, capacity, device->hwName);
		}
	}
	else if(device == &__device_dvd) {
		if(swissSettings.hasDVDDrive == 1) {
			u8 *driveVersion = (u8*)DVDDriveInfo;
			(void)snprintf(out, capacity, "%s %02X %02X%02X/%02X (%02X)",
				infoSafe(device->hwName), driveVersion[6], driveVersion[4],
				driveVersion[5], driveVersion[7], driveVersion[8]);
		}
		else {
			infoCopy(out, capacity, device->hwName);
		}
	}
	else if(device == &__device_flippy) {
		flippyversion *version = (flippyversion*)DVDDriveInfo->pad;
		(void)snprintf(out, capacity, "%s (%u.%u.%u%s)",
			infoSafe(device->hwName), version->major, version->minor,
			version->build, version->dirty ? "-dirty" : "");
		if(deviceHandler_getDeviceAvailable(&__device_dvd)) {
			infoAppend(out, capacity, " + ");
			infoAppend(out, capacity, __device_dvd.hwName);
		}
	}
	else if(device == &__device_gcloader) {
		if(gcloaderVersionStr != NULL) {
			(void)snprintf(out, capacity, "%s HW%i (%s)",
				infoSafe(device->hwName), gcloaderHwVersion,
				infoSafe(gcloaderVersionStr));
		}
		else {
			infoCopy(out, capacity, device->hwName);
		}
	}
	else if(device == &__device_sd_a || device == &__device_sd_b ||
		device == &__device_sd_c) {
		s32 slot;
		if(getExiDeviceByLocation(location, &slot, NULL)) {
			s32 speed = sdgecko_getSpeed(slot);
			if(speed >= 0 &&
				speed < (s32)(sizeof(exiSpeeds) / sizeof(exiSpeeds[0]))) {
				(void)snprintf(out, capacity, "%.3g MHz %s", exiSpeeds[speed],
					infoSafe(device->hwName));
			}
			else {
				infoCopy(out, capacity, device->hwName);
			}
		}
		else {
			infoCopy(out, capacity, device->hwName);
		}
	}
	else if(device == &__device_wkf) {
		(void)snprintf(out, capacity, "%s (%s)", infoSafe(device->hwName),
			infoSafe(wkfGetSerial()));
	}
	else {
		infoCopy(out, capacity, getHwNameByLocation(location));
	}

	if(!strcmp(out, "Unknown")) {
		u32 exiId;
		if(getExiIdByLocation(location, &exiId)) {
			(void)snprintf(out, capacity, "Unknown (0x%08X)", exiId);
		}
	}
}

static const char *infoGetControllerSocketString(s32 channel)
{
	u32 type = 0;

	if(!PAD_GetType(channel, &type)) {
		return "NOT CONNECTED";
	}
	if(PAD_IsBarrel(channel)) {
		return "TARUKONGA CONTROLLER";
	}
	return infoSafe(SI_GetTypeString(type));
}

static void infoGetConsoleModel(char *out, size_t capacity)
{
	if((SYS_GetConsoleType() & SYS_CONSOLE_MASK) == SYS_CONSOLE_DEVELOPMENT) {
		if(*DVDDeviceCode == 0x8201) {
			infoCopy(out, capacity, "NPDP-GDEV (GCT-0100)");
		}
		else if(*DVDDeviceCode == 0x8200) {
			infoCopy(out, capacity, "NPDP-GBOX (GCT-0200)");
		}
		else {
			infoCopy(out, capacity, "ARTX ORCA");
		}
	}
	else if(!strncmp(IPLInfo, "(C) ", 4)) {
		if(!strncmp(&IPLInfo[0x55], "TDEV", 4) ||
			!strncmp(&IPLInfo[0x55], "DEV  Revision 0.1", 0x11)) {
			infoCopy(out, capacity, "NINTENDO GAMECUBE DOT-006");
		}
		else if(*DVDDeviceCode == 0x8200) {
			infoCopy(out, capacity, !strncmp(&IPLInfo[0x55], "PAL ", 4) ?
				"NINTENDO GAMECUBE DOT-002P" : "NINTENDO GAMECUBE DOT-002");
		}
		else if(*DVDDeviceCode == 0x8001) {
			infoCopy(out, capacity, !strncmp(&IPLInfo[0x55], "PAL ", 4) ?
				"NINTENDO GAMECUBE DOT-001P" : "NINTENDO GAMECUBE DOT-001");
		}
		else if(*DVDDeviceCode == 0x8000 && DVDDriveInfo->rel_date == 0x20010619) {
			infoCopy(out, capacity, "NINTENDO GATEWAY GAMECUBE MSA-500");
		}
		else if(*DVDDeviceCode == 0x8000 && DVDDriveInfo->pad[1] == 'M') {
			infoCopy(out, capacity, "PANASONIC Q SL-GC10-S");
		}
		else if(!strncmp(&IPLInfo[0x55], "PAL  Revision 1.2", 0x11)) {
			infoCopy(out, capacity, "NINTENDO GAMECUBE DOL-101 (EUR)");
		}
		else if(!strncmp(&IPLInfo[0x55], "NTSC Revision 1.2", 0x11)) {
			(void)snprintf(out, capacity, "NINTENDO GAMECUBE DOL-101 (%s)",
				getFontEncode() ? "JPN" : "USA");
		}
		else if(!strncmp(&IPLInfo[0x55], "MPAL", 4)) {
			infoCopy(out, capacity, "NINTENDO GAMECUBE DOL-002 (BRA)");
		}
		else if(!strncmp(&IPLInfo[0x55], "PAL ", 4)) {
			infoCopy(out, capacity, "NINTENDO GAMECUBE DOL-001 (EUR)");
		}
		else {
			(void)snprintf(out, capacity, "NINTENDO GAMECUBE DOL-001 (%s)",
				getFontEncode() ? "JPN" : "USA");
		}
	}
	else {
		infoCopy(out, capacity, "NINTENDO WII");
	}
}

static void infoGetIplVersion(char *out, size_t capacity)
{
	if(!strncmp(IPLInfo, "(C) ", 4)) {
		const char *fallback = (SYS_GetConsoleType() & SYS_CONSOLE_MASK) ==
			SYS_CONSOLE_RETAIL ? "NTSC Revision 1.0" : "DEV  Revision 1.0";
		const char *revision = IPLInfo[0x55] ? &IPLInfo[0x55] : fallback;
		if((SYS_GetConsoleType() & SYS_CONSOLE_MASK) == SYS_CONSOLE_RETAIL) {
			(void)snprintf(out, capacity, "%.*s", 0x11, revision);
		}
		else {
			(void)snprintf(out, capacity, "%.*s (%s MODE)", 0x11, revision,
				swissSettings.sramBoot ? "PRODUCTION" : "DEVELOPMENT");
		}
	}
	else {
		infoCopy(out, capacity, "DUMMY IPL");
	}
}

static void infoGetCpu(char *out, size_t capacity)
{
	u32 coreMHz = SYS_GetCoreFrequency() / 1000000;
	u32 pvr = mfpvr();

	if((pvr & 0xFFFFF000) == 0x00083000) {
		if((pvr & 0xFEF) == 0x203 || (pvr & 0xFFF) == 0x214) {
			(void)snprintf(out, capacity, "%u MHz IBM GEKKO DD%X.%XE", coreMHz,
				(pvr >> 8) & 0xF, pvr & 0xF);
		}
		else {
			(void)snprintf(out, capacity, "%u MHz IBM GEKKO DD%X.%X", coreMHz,
				(pvr >> 8) & 0xF, pvr & 0xF);
		}
	}
	else if((pvr & 0xFFFFF000) == 0x00087000) {
		if((pvr & 0xFFF) == 0x110) {
			(void)snprintf(out, capacity, "%u MHz IBM BROADWAY DD%X.%X%X",
				coreMHz, (pvr >> 8) & 0xF, pvr & 0xF, (pvr >> 4) & 0xF);
		}
		else {
			(void)snprintf(out, capacity, "%u MHz IBM BROADWAY DD%X.%X",
				coreMHz, (pvr >> 8) & 0xF, pvr & 0xF);
		}
	}
	else {
		(void)snprintf(out, capacity, "UNKNOWN (0x%08X)", pvr);
	}
}

static void infoGetSystemOnChip(char *out, size_t capacity)
{
	u32 busMHz = SYS_GetBusFrequency() / 1000000;
	u32 chipId = ((vu32*)0xCC003000)[11];

	if((chipId & 0xFFFFFFF) == 0x46500B1) {
		if(is_gamecube()) {
			(void)snprintf(out, capacity, "%u MHz ARTX FLIPPER REV.%c", busMHz,
				'A' + (chipId >> 28));
		}
		else {
			(void)snprintf(out, capacity, "%u MHz ATI HOLLYWOOD", busMHz);
		}
	}
	else {
		(void)snprintf(out, capacity, "UNKNOWN (0x%08X)", chipId);
	}
}

static void infoGetVideoMode(char *out, size_t capacity)
{
	(void)snprintf(out, capacity, "%s  %.5G Hz",
		infoSafe(getVideoModeString(getVideoMode())), VIDEO_GetRetraceRate());
}

static uiSystemRegion_t infoGetRegion(void)
{
	if(strncmp(IPLInfo, "(C) ", 4)) {
		return UI_SYSTEM_REGION_UNKNOWN;
	}
	if(!strncmp(&IPLInfo[0x55], "PAL ", 4)) {
		return UI_SYSTEM_REGION_PAL;
	}
	if(!strncmp(&IPLInfo[0x55], "MPAL", 4)) {
		return UI_SYSTEM_REGION_MPAL;
	}
	return getFontEncode() ? UI_SYSTEM_REGION_NTSC_J :
		UI_SYSTEM_REGION_NTSC_U;
}

static void infoAddFitted(uiDrawObj_t *container, int x, int y,
	const char *text, int width, float maximum, int align, GXColor color)
{
	char fitted[INFO_FITTED_CAPACITY];
	float scale = UISystem_CopyFitted(fitted, sizeof(fitted), infoSafe(text),
		width, maximum, GetTextSizeInPixels, NULL);
	DrawAddChild(container, DrawStyledLabel(x, y, fitted, scale, align, color));
}

static void infoAddPair(uiDrawObj_t *container, int x, int y, int width,
	const char *label, const char *value, float maximum)
{
	DrawAddChild(container, DrawStyledLabel(x, y, label, 0.60f, ALIGN_LEFT,
		systemLabelColor));
	infoAddFitted(container, x, y + 16, value, width, maximum, ALIGN_LEFT,
		systemValueColor);
}

static void infoAddCard(uiDrawObj_t *container, const uiSystemRect_t *rect,
	GXColor color)
{
	DrawAddChild(container, DrawEmptyColouredBox(rect->x, rect->y,
		rect->x + rect->w, rect->y + rect->h, color));
}

static GXColor infoPanelColor(void)
{
	return swissSettings.disablePanelTransparency ?
		(GXColor) {13, 9, 34, 238} : (GXColor) {13, 9, 34, 198};
}

static GXColor infoCardColor(void)
{
	return swissSettings.disablePanelTransparency ?
		(GXColor) {24, 17, 58, 244} : (GXColor) {24, 17, 58, 218};
}

static void infoDrawOverview(uiDrawObj_t *container,
	const uiSystemLayout_t *layout)
{
	char clockText[24];
	char dateText[64];
	char temperatureText[24];
	char calibrationText[24];
	char videoText[INFO_TEXT_CAPACITY];
	char buildText[INFO_TEXT_CAPACITY];
	time_t now = time(NULL);
	struct tm localTimeStorage;
	struct tm *localTime = now == (time_t)-1 ? NULL :
		localtime_r(&now, &localTimeStorage);
	int coreTemperature = CoreTemperature();
	DEVICEHANDLER_INTERFACE *current = devices[DEVICE_CUR];
	bool sourceAvailable = current != NULL &&
		deviceHandler_getDeviceAvailable(current);

	(void)UISystem_FormatClock(clockText, sizeof(clockText),
		localTime != NULL ? localTime->tm_hour : 0,
		localTime != NULL ? localTime->tm_min : 0, localTime != NULL);
	(void)UISystem_FormatDate(dateText, sizeof(dateText),
		localTime != NULL ? localTime->tm_wday : 0,
		localTime != NULL ? localTime->tm_mon : 0,
		localTime != NULL ? localTime->tm_mday : 0,
		localTime != NULL ? localTime->tm_year + 1900 : 0,
		localTime != NULL);
	(void)UISystem_FormatTemperature(temperatureText, sizeof(temperatureText),
		coreTemperature, coreTemperature >= 0);
	(void)UISystem_FormatCalibration(calibrationText, sizeof(calibrationText),
		swissSettings.sramTemperature);
	infoGetVideoMode(videoText, sizeof(videoText));
	(void)snprintf(buildText, sizeof(buildText), "%s  %s", GIT_REVISION,
		GIT_COMMIT);

	infoAddCard(container, &layout->leftCard, infoCardColor());
	infoAddCard(container, &layout->rightCard, infoCardColor());
	DrawAddChild(container, DrawStyledLabel(70, 154, "LOCAL TIME", 0.66f,
		ALIGN_LEFT, systemLabelColor));
	infoAddFitted(container, 181, 188, clockText, 220, 1.45f, ALIGN_CENTER,
		systemTitleColor);
	infoAddFitted(container, 181, 218, dateText, UI_SYSTEM_DATE_WIDTH,
		UI_SYSTEM_DATE_SCALE, ALIGN_CENTER,
		systemMutedColor);
	DrawAddChild(container, DrawStyledLabel(70, 254,
		"CPU THERMAL / MINUTE SAMPLE", 0.60f,
		ALIGN_LEFT, systemLabelColor));
	infoAddFitted(container, 70, 280, temperatureText, 220, 1.00f, ALIGN_LEFT,
		systemTitleColor);
	DrawAddChild(container, DrawStyledLabel(70, 314, "CALIBRATION", 0.60f,
		ALIGN_LEFT, systemLabelColor));
	infoAddFitted(container, 70, 334, calibrationText, 220, 0.72f, ALIGN_LEFT,
		systemValueColor);
	infoAddFitted(container, 70, 358, "ADJUST IN SETUP / CONSOLE", 220,
		0.60f, ALIGN_LEFT, systemMutedColor);

	DrawAddChild(container, DrawStyledLabel(348, 154, "ACTIVE SESSION", 0.66f,
		ALIGN_LEFT, systemLabelColor));
	infoAddPair(container, 348, 174, 220, "SOURCE",
		current != NULL ? DeviceDisplayName(current) : "NONE", 0.72f);
	infoAddPair(container, 348, 214, 220, "SOURCE STATUS",
		UISystem_SourceHealth(current != NULL, sourceAvailable), 0.72f);
	infoAddPair(container, 348, 254, 220, "VIDEO", videoText, 0.72f);
	infoAddPair(container, 348, 294, 220, "REGION",
		UISystem_RegionName(infoGetRegion()), 0.72f);
	infoAddPair(container, 348, 334, 220, "BUILD", buildText, 0.72f);
}

static void infoDrawConsole(uiDrawObj_t *container,
	const uiSystemLayout_t *layout)
{
	char model[INFO_TEXT_CAPACITY];
	char ipl[INFO_TEXT_CAPACITY];
	char cpu[INFO_TEXT_CAPACITY];
	char chip[INFO_TEXT_CAPACITY];
	char ecid[INFO_TEXT_CAPACITY];

	infoGetConsoleModel(model, sizeof(model));
	infoGetIplVersion(ipl, sizeof(ipl));
	infoGetCpu(cpu, sizeof(cpu));
	infoGetSystemOnChip(chip, sizeof(chip));
	(void)snprintf(ecid, sizeof(ecid), "%08X:%08X:%08X:%08X", mfspr(ECID0),
		mfspr(ECID1), mfspr(ECID2), mfspr(ECID3));
	infoAddCard(container, &layout->wideCard, infoCardColor());
	infoAddPair(container, 70, 158, 500, "MODEL", model, 0.76f);
	infoAddPair(container, 70, 202, 500, "IPL VERSION", ipl, 0.76f);
	infoAddPair(container, 70, 246, 500, "CPU", cpu, 0.76f);
	infoAddPair(container, 70, 290, 500, "SYSTEM-ON-CHIP", chip, 0.76f);
	infoAddPair(container, 70, 334, 500, "CPU ECID", ecid, 0.72f);
}

static void infoDrawConnections(uiDrawObj_t *container,
	const uiSystemLayout_t *layout)
{
	char slotA[INFO_TEXT_CAPACITY];
	char slotB[INFO_TEXT_CAPACITY];
	char serial1[INFO_TEXT_CAPACITY];
	char serial2[INFO_TEXT_CAPACITY];
	char dvd[INFO_TEXT_CAPACITY];
	char hsp[INFO_TEXT_CAPACITY];
	DEVICEHANDLER_INTERFACE *current = devices[DEVICE_CUR];
	DEVICEHANDLER_INTERFACE *configuration = devices[DEVICE_CONFIG];

	infoGetDeviceInfoString(LOC_MEMCARD_SLOT_A, slotA, sizeof(slotA));
	infoGetDeviceInfoString(LOC_MEMCARD_SLOT_B, slotB, sizeof(slotB));
	infoGetDeviceInfoString(LOC_SERIAL_PORT_1, serial1, sizeof(serial1));
	infoGetDeviceInfoString(LOC_SERIAL_PORT_2, serial2, sizeof(serial2));
	infoGetDeviceInfoString(LOC_DVD_CONNECTOR, dvd, sizeof(dvd));
	infoGetDeviceInfoString(LOC_HSP, hsp, sizeof(hsp));

	infoAddCard(container, &layout->leftCard, infoCardColor());
	infoAddCard(container, &layout->rightCard, infoCardColor());
	DrawAddChild(container, DrawStyledLabel(70, 154, "EXTERNAL PORTS", 0.66f,
		ALIGN_LEFT, systemLabelColor));
	infoAddPair(container, 70, 170, 222, "SLOT-A", slotA, 0.70f);
	infoAddPair(container, 70, 205, 222, "SLOT-B", slotB, 0.70f);
	infoAddPair(container, 70, 240, 222, "SERIAL PORT 1", serial1, 0.70f);
	infoAddPair(container, 70, 275, 222, "SERIAL PORT 2", serial2, 0.70f);
	infoAddPair(container, 70, 310, 222, "DVD INTERFACE", dvd, 0.70f);
	infoAddPair(container, 70, 345, 222, "HIGH SPEED PORT", hsp, 0.70f);

	DrawAddChild(container, DrawStyledLabel(348, 154, "LIVE SNAPSHOT", 0.66f,
		ALIGN_LEFT, systemLabelColor));
	infoAddPair(container, 348, 178, 220, "SLOT-A HOTPLUG",
		getExiTypeByLocation(LOC_MEMCARD_SLOT_A), 0.70f);
	infoAddPair(container, 348, 224, 220, "SLOT-B HOTPLUG",
		getExiTypeByLocation(LOC_MEMCARD_SLOT_B), 0.70f);
	infoAddPair(container, 348, 270, 220, "CURRENT SOURCE",
		current != NULL ? DeviceDisplayName(current) : "NONE", 0.70f);
	infoAddPair(container, 348, 316, 220, "CONFIGURATION",
		configuration != NULL ? DeviceDisplayName(configuration) : "NONE", 0.70f);
	infoAddFitted(container, 348, 358, "REOPEN PAGE TO REFRESH", 220, 0.60f,
		ALIGN_LEFT, systemMutedColor);
}

static void infoDrawInputOutput(uiDrawObj_t *container,
	const uiSystemLayout_t *layout)
{
	char videoText[INFO_TEXT_CAPACITY];

	infoGetVideoMode(videoText, sizeof(videoText));
	infoAddCard(container, &layout->leftCard, infoCardColor());
	infoAddCard(container, &layout->rightCard, infoCardColor());
	DrawAddChild(container, DrawStyledLabel(70, 154, "CONTROLLERS", 0.66f,
		ALIGN_LEFT, systemLabelColor));
	infoAddPair(container, 70, 178, 220, "SOCKET 1",
		infoGetControllerSocketString(PAD_CHAN0), 0.70f);
	infoAddPair(container, 70, 224, 220, "SOCKET 2",
		infoGetControllerSocketString(PAD_CHAN1), 0.70f);
	infoAddPair(container, 70, 270, 220, "SOCKET 3",
		infoGetControllerSocketString(PAD_CHAN2), 0.70f);
	infoAddPair(container, 70, 316, 220, "SOCKET 4",
		infoGetControllerSocketString(PAD_CHAN3), 0.70f);

	DrawAddChild(container, DrawStyledLabel(348, 154, "VIDEO & REGION", 0.66f,
		ALIGN_LEFT, systemLabelColor));
	infoAddPair(container, 348, 170, 220, "VIDEO MODE", videoText, 0.70f);
	infoAddPair(container, 348, 207, 220, "PROGRESSIVE",
		getDTVStatus() ? (getRawDTVStatus() ? "ENABLED" : "FORCED") :
		"DISABLED", 0.70f);
	infoAddPair(container, 348, 244, 220, "REGION",
		UISystem_RegionName(infoGetRegion()), 0.70f);
	infoAddPair(container, 348, 281, 220, "AUDIO",
		swissSettings.sramStereo ? "STEREO" : "MONO", 0.70f);
	infoAddPair(container, 348, 318, 220, "LANGUAGE",
		sramLanguageStr[swissSettings.sramLanguage], 0.70f);
	infoAddFitted(container, 348, 358, "REOPEN PAGE TO REFRESH", 220, 0.60f,
		ALIGN_LEFT, systemMutedColor);
}

static void infoDrawAbout(uiDrawObj_t *container,
	const uiSystemLayout_t *layout)
{
	char buildText[INFO_TEXT_CAPACITY];
	char compilerText[INFO_TEXT_CAPACITY];

	(void)snprintf(buildText, sizeof(buildText), "COMMIT %s  /  REVISION %s",
		GIT_COMMIT, GIT_REVISION);
	(void)snprintf(compilerText, sizeof(compilerText), "BUILT WITH %s",
		_V_STRING);
	infoAddCard(container, &layout->wideCard, infoCardColor());
	DrawAddChild(container, DrawStyledLabel(320, 162, "SWISS 0.6", 1.16f,
		ALIGN_CENTER, systemTitleColor));
	DrawAddChild(container, DrawStyledLabel(320, 190,
		"BY EMU_KIDID & EXTREMS, 2026", 0.70f, ALIGN_CENTER,
		systemValueColor));
	DrawAddChild(container, DrawStyledLabel(320, 220,
		"INDIGO - UNOFFICIAL SWISS FORK", 0.66f, ALIGN_CENTER, systemLabelColor));
	infoAddFitted(container, 320, 246, buildText, UI_SYSTEM_ABOUT_TEXT_WIDTH,
		0.72f, ALIGN_CENTER, systemValueColor);
	infoAddFitted(container, 320, 270, compilerText, UI_SYSTEM_ABOUT_TEXT_WIDTH,
		0.66f, ALIGN_CENTER, systemMutedColor);
	DrawAddChild(container, DrawStyledLabel(320, 310, "SOURCE / UPDATES",
		0.66f, ALIGN_CENTER, systemLabelColor));
	infoAddFitted(container, 320, 331, "GITHUB.COM/SPENCERNORTON/INDIGO",
		UI_SYSTEM_ABOUT_TEXT_WIDTH, 0.70f, ALIGN_CENTER, systemValueColor);
	DrawAddChild(container, DrawStyledLabel(320, 354,
		UI_SYSTEM_ABOUT_HELP_LABEL, 0.66f, ALIGN_CENTER, systemLabelColor));
	infoAddFitted(container, 320, 374, UI_SYSTEM_ABOUT_HELP_TEXT,
		UI_SYSTEM_ABOUT_TEXT_WIDTH, 0.70f, ALIGN_CENTER, systemValueColor);
}

static void infoDrawCredits(uiDrawObj_t *container,
	const uiSystemLayout_t *layout)
{
	infoAddCard(container, &layout->wideCard, infoCardColor());
	DrawAddChild(container, DrawStyledLabel(320, 150, "CURRENT PATREON SUPPORTERS",
		0.66f, ALIGN_CENTER, systemLabelColor));
	infoAddFitted(container, 320, 168,
		"BORG NUMBER ONE (STEVEN WEISER), ROMAN ANTONACCI, 8BITMODS,", 500,
		0.60f, ALIGN_CENTER, systemValueColor);
	infoAddFitted(container, 320, 185,
		"CASTLEMANIA RYAN, DAN KUNZ, FERNANDO AVELINO, HAKANAISEISHIN, HAYMOSE,",
		500, 0.60f, ALIGN_CENTER, systemValueColor);
	infoAddFitted(container, 320, 202,
		"ALEX MITCHELL, BADSECTOR, JEFFREY PIERCE, JON MOON, KEVIN,", 500,
		0.60f, ALIGN_CENTER, systemValueColor);
	infoAddFitted(container, 320, 219,
		"KORY, MARLON, SILVERSTEEL, WILLIAM FOWLER", 500, 0.60f,
		ALIGN_CENTER, systemValueColor);
	DrawAddChild(container, DrawStyledLabel(320, 240,
		"HISTORICAL PATREON SUPPORTERS", 0.66f, ALIGN_CENTER,
		systemLabelColor));
	infoAddFitted(container, 320, 258,
		"MENEERBEER, SUBELEMENT, KIROVAIR, CRISTOFER CRUZ,", 500, 0.60f,
		ALIGN_CENTER, systemValueColor);
	infoAddFitted(container, 320, 275,
		"RAMBLINGOKIE, LINDH0LM154, FINNYGUY, CTPG", 500, 0.60f,
		ALIGN_CENTER, systemValueColor);
	/* The Library's layouts from elsewhere; see NOTICE. Gameplay Spotlight
	 * (github.com/mvizensk/gameplay-spotlight): its author let Indigo take
	 * its design for the Spotlight layout. The jewel case games view of
	 * portlandhodl's Swiss (github.com/portlandhodl/swiss-gc), after Ann
	 * Nguyen's CD Case (bubbbly.com/cd-case), is the Jewel Case layout. */
	DrawAddChild(container, DrawStyledLabel(320, 296,
		"LIBRARY DESIGNS", 0.66f, ALIGN_CENTER, systemLabelColor));
	infoAddFitted(container, 320, 314,
		"GAMEPLAY SPOTLIGHT BY MVIZENSK", 500, 0.60f,
		ALIGN_CENTER, systemValueColor);
	infoAddFitted(container, 320, 331,
		"JEWEL CASE BY PORTLANDHODL, AFTER CD CASE BY ANN NGUYEN", 500, 0.60f,
		ALIGN_CENTER, systemValueColor);
	infoAddFitted(container, 320, 349,
		"EXTRA GREETZ: FIX94, MEGALOMANIAC, SEPP256, NOVENARY", 500, 0.60f,
		ALIGN_CENTER, systemMutedColor);
	DrawAddChild(container, DrawStyledLabel(320, 369,
		"AND A BIG THANKS TO YOU, FOR USING SWISS!", 0.70f, ALIGN_CENTER,
		systemTitleColor));
}

uiDrawObj_t * info_draw_page(int page_num)
{
	uiSystemLayout_t layout;
	const uiSystemPageDesc_t *desc;
	uiDrawObj_t *container = DrawContainer();
	char pageProgress[24];
	char pageStatus[64];
	char pageFitted[INFO_FITTED_CAPACITY];
	float pageScale;
	GXColor panelColor;
	GXColor railColor;

	UISystem_ComputeLayout(page_num, &layout);
	desc = UISystem_PageDesc(layout.page);
	panelColor = infoPanelColor();
	railColor = swissSettings.disablePanelTransparency ?
		(GXColor) {31, 21, 72, 248} : (GXColor) {31, 21, 72, 232};
	infoAddCard(container, &layout.panel, panelColor);
	infoAddCard(container, &layout.rail, railColor);
	DrawAddChild(container, DrawStyledLabel(48, 78, "SYSTEM INFORMATION", 0.60f,
		ALIGN_LEFT, systemLabelColor));
	DrawAddChild(container, DrawStyledLabel(layout.titleX, layout.titleY,
		desc->title, 0.90f, ALIGN_LEFT, systemTitleColor));
	infoAddFitted(container, layout.subtitleX, layout.subtitleY, desc->subtitle,
		420, 0.60f, ALIGN_LEFT, systemMutedColor);
	(void)snprintf(pageProgress, sizeof(pageProgress), "%d OF %d",
		layout.page + 1, UI_SYSTEM_PAGE_COUNT);
	DrawAddChild(container, DrawStyledLabel(layout.progressX, layout.progressY,
		pageProgress, 0.60f, ALIGN_RIGHT, systemLabelColor));

	switch(layout.page) {
		case UI_SYSTEM_PAGE_OVERVIEW:
			infoDrawOverview(container, &layout);
			break;
		case UI_SYSTEM_PAGE_CONSOLE:
			infoDrawConsole(container, &layout);
			break;
		case UI_SYSTEM_PAGE_CONNECTIONS:
			infoDrawConnections(container, &layout);
			break;
		case UI_SYSTEM_PAGE_IO:
			infoDrawInputOutput(container, &layout);
			break;
		case UI_SYSTEM_PAGE_ABOUT:
			infoDrawAbout(container, &layout);
			break;
		case UI_SYSTEM_PAGE_CREDITS:
		default:
			infoDrawCredits(container, &layout);
			break;
	}
	UISystem_FormatPageStatus(pageStatus, sizeof(pageStatus), layout.page);
	/* The footer's button names draw as button icons. */
	pageScale = UISystem_CopyFitted(pageFitted, sizeof(pageFitted), pageStatus,
		520, 0.66f, GetHintSizeInPixels, NULL);
	DrawAddChild(container, DrawHintLabel(320, 420, pageFitted, pageScale,
		ALIGN_CENTER, systemTitleColor));
	return container;
}

static time_t infoCurrentMinute(void)
{
	time_t now = time(NULL);
	return now == (time_t)-1 ? (time_t)-1 : now / 60;
}

void show_info()
{
	int page = UI_SYSTEM_PAGE_OVERVIEW;
	uiDrawObj_t *pagePanel = NULL;
	while(padsButtonsHeld() & BUTTON_A) {
		VIDEO_WaitVSync();
	}
	while(1) {
		time_t publishedMinute;
		bool refreshOverview = false;
		unsigned int pollFrames = 0u;
		u32 btns;

		pagePanel = DrawRepublish(pagePanel, info_draw_page(page));
		publishedMinute = infoCurrentMinute();
		while(!refreshOverview && !((padsButtonsHeld() & BUTTON_RIGHT) ||
			(padsButtonsHeld() & BUTTON_LEFT) ||
			(padsButtonsHeld() & BUTTON_B) ||
			(padsButtonsHeld() & BUTTON_R) ||
			(padsButtonsHeld() & BUTTON_L))) {
			VIDEO_WaitVSync();
			if(page == UI_SYSTEM_PAGE_OVERVIEW && ++pollFrames >= 50u) {
				time_t currentMinute = infoCurrentMinute();
				pollFrames = 0u;
				refreshOverview = currentMinute != (time_t)-1 &&
					currentMinute != publishedMinute;
			}
		}
		if(refreshOverview) {
			continue;
		}
		btns = padsButtonsHeld();
		if(((btns & BUTTON_RIGHT) || (btns & BUTTON_R)) &&
			page < UI_SYSTEM_PAGE_COUNT - 1) {
			page++;
		}
		if(((btns & BUTTON_LEFT) || (btns & BUTTON_L)) && page > 0) {
			page--;
		}
		if(btns & BUTTON_B) {
			break;
		}
		while((padsButtonsHeld() & BUTTON_RIGHT) ||
			(padsButtonsHeld() & BUTTON_LEFT) ||
			(padsButtonsHeld() & BUTTON_B) ||
			(padsButtonsHeld() & BUTTON_R) ||
			(padsButtonsHeld() & BUTTON_L)) {
			VIDEO_WaitVSync();
		}
	}
	DrawDispose(pagePanel);
	while (padsButtonsHeld() & BUTTON_B) {
		VIDEO_WaitVSync();
	}
}
