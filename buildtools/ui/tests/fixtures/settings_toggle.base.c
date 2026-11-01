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
			case SET_SYS_TIMEZONE:
				select_timezone(swissSettings.timeZone);
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
			case SET_FILEBROWSER_TYPE:
				swissSettings.fileBrowserType += direction;
				swissSettings.fileBrowserType = (swissSettings.fileBrowserType + BROWSER_MAX) % BROWSER_MAX;
			break;
			case SET_APPSBROWSER_TYPE:
				swissSettings.appsBrowserType += direction;
				swissSettings.appsBrowserType = (swissSettings.appsBrowserType + BROWSER_MAX) % BROWSER_MAX;
			break;
			case SET_GAMEBROWSER_TYPE:
				swissSettings.gameBrowserType += direction;
				swissSettings.gameBrowserType = (swissSettings.gameBrowserType + BROWSER_MAX) % BROWSER_MAX;
			break;
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
				swissSettings.disableUIAnimations ^= 1;
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
			case SET_SNTP_HOSTIP:
				DrawGetTextEntry(ENTRYMODE_IP, "SNTP Host IP", &swissSettings.sntpHostIp, sizeof(swissSettings.sntpHostIp) - 1);
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
					config_defaults(gameConfig);
			break;
		}
	}
}
