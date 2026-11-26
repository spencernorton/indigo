#include <gccore.h>
#include <ogc/lwp.h>
#include <ogc/message.h>
#include <aesndlib.h>
#include <mad.h>
#include <malloc.h>
#include <string.h>
#include "swiss.h"
#include "menuaudio.h"
#include "ui_menu_sfx.h"
#include "menu_music_mp3.h"

#define VOL_UNITY           256

// The menu music streams. A decoder thread turns the bundled MP3 into blocks
// and the music voice's stream callback plays them, so only a few blocks of
// PCM are ever in memory however long the piece is. AESND copies a voice's
// buffer in DSP_STREAMBUFFER_SIZE chunks and pads a short last chunk with
// silence, so a block is exactly one MP3 frame of 16-bit stereo: 4608 bytes,
// four whole chunks.
#define MUSIC_FRAME_SAMPLES 1152
#define MUSIC_BLOCK_BYTES   (MUSIC_FRAME_SAMPLES * 2 * sizeof(s16))
#define MUSIC_BLOCKS        8     // about 290 ms queued at 32 kHz
#define MUSIC_PRIME_FRAMES  3     // decoded and dropped after a seek: bit reservoir, overlap
#define MUSIC_STACK_SIZE    16384
#define MUSIC_PRIORITY      80    // above the menus, like libaesnd's own MP3 player

_Static_assert(sizeof(menu_music_mp3) >= MENU_MUSIC_MP3_LEN + MAD_BUFFER_GUARD,
               "libmad reads MAD_BUFFER_GUARD bytes past the last frame");
_Static_assert(MENU_MUSIC_LOOP_START < MENU_MUSIC_LOOP_END, "the loop must have a length");

static bool inited = false;
static bool musicPlaying = false;
static bool suspended = false;
static bool resumeMusic = false;
static AESNDPB *musicVoice = NULL;
static AESNDPB *sfxVoice = NULL;
static s16 *blipBuf = NULL;
static s16 *selBuf = NULL;

static s16 musicBlocks[MUSIC_BLOCKS][MUSIC_FRAME_SAMPLES * 2] ATTRIBUTE_ALIGN(32);
static struct mad_stream musicStream;
static struct mad_frame musicFrame;
static struct mad_synth musicSynth;
static u8 musicStack[MUSIC_STACK_SIZE] ATTRIBUTE_ALIGN(8);
static lwp_t musicThread = LWP_THREAD_NULL;
static mqbox_t musicFree = MQ_BOX_NULL;
static mqbox_t musicReady = MQ_BOX_NULL;
static s16 *musicHeld = NULL;           // the block the voice is copying from
static volatile bool musicRun = false;

static s16 mf2s16(mad_fixed_t f) {
	f += (1L << (MAD_F_FRACBITS - 16));
	if(f >= MAD_F_ONE) f = MAD_F_ONE - 1;
	else if(f < -MAD_F_ONE) f = -MAD_F_ONE;
	return (s16)(f >> (MAD_F_FRACBITS + 1 - 16));
}

// Point the decoder at an MP3 frame. The bundled stream is constant bitrate
// with no tags (buildtools/audio/make_header.py checks), so frame n starts at
// n * MENU_MUSIC_FRAME_BYTES.
static void music_seek(u32 frame) {
	u32 offset = frame * MENU_MUSIC_FRAME_BYTES;
	mad_stream_buffer(&musicStream, menu_music_mp3 + offset,
	                  MENU_MUSIC_MP3_LEN - offset + MAD_BUFFER_GUARD);
	mad_frame_mute(&musicFrame);
	mad_synth_mute(&musicSynth);
}

// The intro plays once; then [loop_start, loop_end) repeats. Positions count
// decoded samples from the start of the stream, so a seek lands a few frames
// early and drops them until the decoder's history is whole again.
static void *music_decode(void *arg) {
	(void)arg;
	u32 next = 0;
	u32 fill = 0;
	s16 *block = NULL;

	mad_stream_init(&musicStream);
	mad_frame_init(&musicFrame);
	mad_synth_init(&musicSynth);
	music_seek(0);
	while(musicRun) {
		bool wrap = false;
		if(mad_frame_decode(&musicFrame, &musicStream)) {
			if(MAD_RECOVERABLE(musicStream.error)) continue;
			wrap = true;    // the end of the data, or a broken stream
		}
		else {
			u32 first = (u32)(musicStream.this_frame - menu_music_mp3) / MENU_MUSIC_FRAME_BYTES * MUSIC_FRAME_SAMPLES;
			mad_synth_frame(&musicSynth, &musicFrame);
			const struct mad_pcm *pcm = &musicSynth.pcm;
			int right = pcm->channels > 1;
			for(u32 i = 0; i < pcm->length; i++) {
				u32 at = first + i;
				if(at < next) continue;
				if(at >= MENU_MUSIC_LOOP_END) {
					wrap = true;
					break;
				}
				if(!block) {
					mqmsg_t msg = NULL;
					MQ_Receive(musicFree, &msg, MQ_MSG_BLOCK);
					if(!msg || !musicRun) goto out;
					block = msg;
					fill = 0;
				}
				block[fill * 2] = mf2s16(pcm->samples[0][i]);
				block[fill * 2 + 1] = mf2s16(pcm->samples[right][i]);
				next = at + 1;
				if(++fill == MUSIC_FRAME_SAMPLES) {
					MQ_Send(musicReady, block, MQ_MSG_BLOCK);
					block = NULL;
				}
			}
		}
		if(wrap) {
			u32 frame = MENU_MUSIC_LOOP_START / MUSIC_FRAME_SAMPLES;
			next = MENU_MUSIC_LOOP_START;
			music_seek(frame > MUSIC_PRIME_FRAMES ? frame - MUSIC_PRIME_FRAMES : 0);
		}
	}
out:
	mad_synth_finish(&musicSynth);
	mad_frame_finish(&musicFrame);
	mad_stream_finish(&musicStream);
	return NULL;
}

// AESND asks for the next buffer once it has copied the whole current one, so
// the block it held goes straight back to the decoder. With nothing ready the
// voice plays silence and asks again on the next request.
static void music_voice(AESNDPB *pb, u32 state) {
	if(state != VOICE_STATE_STREAM) return;
	mqmsg_t msg = NULL;
	if(musicHeld) MQ_Send(musicFree, musicHeld, MQ_MSG_NOBLOCK);
	musicHeld = NULL;
	if(MQ_Receive(musicReady, &msg, MQ_MSG_NOBLOCK) && msg) {
		musicHeld = msg;
		AESND_SetVoiceBuffer(pb, musicHeld, MUSIC_BLOCK_BYTES);
	}
}

static bool start_decoder(void) {
	if(musicThread != LWP_THREAD_NULL) return true;
	// One spare slot in each box, so the stop message always fits.
	if(MQ_Init(&musicFree, MUSIC_BLOCKS + 1) < 0) return false;
	if(MQ_Init(&musicReady, MUSIC_BLOCKS + 1) < 0) {
		MQ_Close(musicFree);
		musicFree = MQ_BOX_NULL;
		return false;
	}
	for(int i = 0; i < MUSIC_BLOCKS; i++) MQ_Send(musicFree, musicBlocks[i], MQ_MSG_BLOCK);
	musicHeld = NULL;
	musicRun = true;
	if(LWP_CreateThread(&musicThread, music_decode, NULL, musicStack, MUSIC_STACK_SIZE, MUSIC_PRIORITY) < 0) {
		musicRun = false;
		musicThread = LWP_THREAD_NULL;
		MQ_Close(musicFree);
		MQ_Close(musicReady);
		musicFree = musicReady = MQ_BOX_NULL;
		return false;
	}
	return true;
}

// Call with the music voice stopped: nothing else touches the boxes then.
static void stop_decoder(void) {
	if(musicThread == LWP_THREAD_NULL) return;
	musicRun = false;
	MQ_Send(musicFree, NULL, MQ_MSG_NOBLOCK);    // wakes a decoder waiting for a block
	mqmsg_t msg;
	while(MQ_Receive(musicReady, &msg, MQ_MSG_NOBLOCK));    // frees one waiting to queue
	LWP_JoinThread(musicThread, NULL);
	musicThread = LWP_THREAD_NULL;
	MQ_Close(musicFree);
	MQ_Close(musicReady);
	musicFree = musicReady = MQ_BOX_NULL;
	musicHeld = NULL;
}

static void start_music(void) {
	if(!inited || suspended || musicPlaying || swissSettings.disableMenuMusic) return;
	if(!musicVoice) {
		musicVoice = AESND_AllocateVoice(music_voice);
		if(!musicVoice) return;
		AESND_SetVoiceStream(musicVoice, true);
		AESND_SetVoiceFormat(musicVoice, VOICE_STEREO16);
		AESND_SetVoiceFrequency(musicVoice, MENU_MUSIC_RATE);
		AESND_SetVoiceVolume(musicVoice, (VOL_UNITY * 3) / 5, (VOL_UNITY * 3) / 5);
	}
	if(!start_decoder()) return;
	AESND_SetVoiceStop(musicVoice, false);
	musicPlaying = true;
}

// Pausing keeps the decoder where it is; stopping for good starts the music
// over, intro first, the next time it plays.
static void pause_music(void) {
	if(musicVoice && musicPlaying) AESND_SetVoiceStop(musicVoice, true);
	musicPlaying = false;
}

static void stop_music(void) {
	pause_music();
	stop_decoder();
	// Forget the block the voice held: it belongs to the next decoder now.
	if(musicVoice) AESND_SetVoiceBuffer(musicVoice, NULL, 0);
}

void menuaudio_init(void) {
	if(inited) {
		menuaudio_apply_settings();
		return;
	}

	AESND_Init();
	inited = true;
	const size_t blipBytes = UI_MENU_BLIP_SAMPLES * sizeof(*blipBuf);
	const size_t selectBytes = UI_MENU_SELECT_SAMPLES * sizeof(*selBuf);
	blipBuf = memalign(32, blipBytes);
	selBuf = memalign(32, selectBytes);
	if(blipBuf) {
		UIMenuSFX_Blip(blipBuf);
		DCFlushRange(blipBuf, blipBytes);
	}
	if(selBuf) {
		UIMenuSFX_Select(selBuf);
		DCFlushRange(selBuf, selectBytes);
	}
	sfxVoice = AESND_AllocateVoice(NULL);
	menuaudio_apply_settings();
}

void menuaudio_apply_settings(void) {
	if(!inited) return;
	if(swissSettings.disableMenuMusic) stop_music();
	else start_music();
	if(swissSettings.disableMenuSFX && sfxVoice) AESND_SetVoiceStop(sfxVoice, true);
}

void menuaudio_suspend(void) {
	if(!inited || suspended) return;
	resumeMusic = musicPlaying;
	suspended = true;
	pause_music();
	if(sfxVoice) AESND_SetVoiceStop(sfxVoice, true);
}

void menuaudio_resume(void) {
	if(!inited || !suspended) return;
	bool shouldResumeMusic = resumeMusic;
	resumeMusic = false;
	suspended = false;
	if(shouldResumeMusic && !swissSettings.disableMenuMusic) start_music();
}

bool menuaudio_shutdown(void) {
	if(!inited) return false;
	stop_music();
	if(sfxVoice) AESND_SetVoiceStop(sfxVoice, true);
	AESND_Reset();	/* waits for the DSP to finish first: cube/swiss/aesnd */
	musicVoice = NULL;
	sfxVoice = NULL;
	musicPlaying = false;
	suspended = false;
	resumeMusic = false;
	free(blipBuf);
	free(selBuf);
	blipBuf = NULL;
	selBuf = NULL;
	inited = false;
	return true;
}

static void play_sfx(s16 *buf, u32 bytes) {
	if(!buf || !sfxVoice || suspended || swissSettings.disableMenuSFX) return;
	AESND_SetVoiceVolume(sfxVoice, (VOL_UNITY * 17) / 20, (VOL_UNITY * 17) / 20);
	AESND_PlayVoice(sfxVoice, VOICE_MONO16, buf, bytes, UI_MENU_SFX_RATE, 0, false);
}

void menuaudio_blip(void) {
	play_sfx(blipBuf, UI_MENU_BLIP_SAMPLES * sizeof(*blipBuf));
}

void menuaudio_select(void) {
	play_sfx(selBuf, UI_MENU_SELECT_SAMPLES * sizeof(*selBuf));
}
