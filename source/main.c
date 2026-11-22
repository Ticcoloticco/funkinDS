// Funkin DS - entry point, state machine, save data
#include "engine.h"
#include "character.h"
#include <filesystem.h>
#include <fat.h>

u32 kDown, kHeld, kUp;
touchPosition touch;
bool touchDown;
float g_fade = 0;
float g_subFade = 0;
int g_song = 0;
int g_diff = 1;
bool g_storyMode = false;
int g_week = 0;
int g_weekPos = 0;
int g_weekScore = 0;
int g_optionsReturn = ST_MAINMENU;
bool g_titleSeen = false;
Results g_results;
u32 prof_max[PF_COUNT], prof_show[PF_COUNT];
static int profFrames;
const char *g_video;
int g_videoNext;
bool g_cutsceneDone;

const Cutscene *cutscene_for(const char *song, bool ending) {
	for (int i = 0; i < cutscene_count; i++)
		if (!strcmp(cutscenes[i].song, song) && cutscenes[i].ending == ending) return &cutscenes[i];
	return NULL;
}
int perf_drops, perf_frames;

Save save;
static int curState = -1, nextState = -1;
static bool fatOk;

static const StateDef states[ST_COUNT] = {
	[ST_TITLE] = { title_enter, title_update, title_draw, title_leave },
	[ST_MAINMENU] = { mainmenu_enter, mainmenu_update, mainmenu_draw, mainmenu_leave },
	[ST_STORY] = { story_enter, story_update, story_draw, story_leave },
	[ST_FREEPLAY] = { freeplay_enter, freeplay_update, freeplay_draw, freeplay_leave },
	[ST_OPTIONS] = { options_enter, options_update, options_draw, options_leave },
	[ST_PLAY] = { play_enter, play_update, play_draw, play_leave },
	[ST_GAMEOVER] = { gameover_enter, gameover_update, gameover_draw, gameover_leave },
	[ST_CHARSELECT] = { charselect_enter, charselect_update, charselect_draw, charselect_leave },
	[ST_RESULTS] = { results_enter, results_update, results_draw, results_leave },
	[ST_VIDEO] = { video_enter, video_update_state, video_draw, video_leave },
};

const char *diff_name(int d) {
	static const char *n[5] = { "easy", "normal", "hard", "erect", "nightmare" };
	return n[d < 0 ? 0 : d > 4 ? 4 : d];
}

void asset_path(const char *path, char *out, size_t outSize) {
    const char *rel = path ? path : "";

    if (!strncmp(rel, "nitro:", 6))
        rel += 6;
    else if (!strncmp(rel, "fat:", 4)) {
        snprintf(out, outSize, "%s", rel);
        return;
    }

    while (*rel == '/' || *rel == '\\')
        rel++;

    snprintf(out, outSize, "fat:/FunkinDS/nitrofiles/%s", rel);
}

void *file_load(const char *path, u32 *size) {
    char full[256];
    asset_path(path, full, sizeof(full));

    FILE *f = fopen(full, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *buf = malloc(n + 4);
    if (buf) fread(buf, 1, n, f);

    fclose(f);
    if (size) *size = n;
    return buf;
}
	char full[96];
	snprintf(full, sizeof(full), "nitro:%s", path);
	FILE *f = fopen(full, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *buf = malloc(n + 4);
	if (buf) fread(buf, 1, n, f);
	fclose(f);
	if (size) *size = n;
	return buf;
}

// ---------------------------------------------------------------------------
// save data (FlxSave "WwcdSave" + FunkinSave highscores + options)
// ---------------------------------------------------------------------------
#define SAVE_PATH "fat:/data/funkinds.sav"
#define SAVE_MAGIC 0x324B4E46   // "FNK2"

static void save_defaults(void) {
	memset(&save, 0, sizeof(save));
	save.ghostTapping = true;
	save.camZoom = true;
	save.flashing = true;
	save.lastDiff = 1;
}

void save_load(void) {
	save_defaults();
	if (!fatOk) return;
	FILE *f = fopen(SAVE_PATH, "rb");
	if (!f) return;
	u32 magic = 0, size = 0;
	if (fread(&magic, 4, 1, f) == 1 && magic == SAVE_MAGIC && fread(&size, 4, 1, f) == 1 && size == sizeof(Save)) {
		Save s;
		if (fread(&s, sizeof(s), 1, f) == 1) save = s;
	}
	fclose(f);
}

void save_write(void) {
	if (!fatOk) return;
	mkdir("fat:/data", 0777);
	FILE *f = fopen(SAVE_PATH, "wb");
	if (!f) return;
	u32 magic = SAVE_MAGIC, size = sizeof(Save);
	fwrite(&magic, 4, 1, f);
	fwrite(&size, 4, 1, f);
	fwrite(&save, sizeof(save), 1, f);
	fclose(f);
}

// CoolUtil.playMenuSong(fadeIn)
void menu_music(bool fadeIn) {
	if (audio_music_playing()) return;
	audio_play_music("nitro:/snd/menu.adp", true, fadeIn ? 0 : 1);
	if (fadeIn) audio_fade_music(0.7f, 4);
}

void format_score(char *out, int score) {
	char b[16];
	bool neg = score < 0;
	snprintf(b, sizeof(b), "%d", neg ? -score : score);
	int len = strlen(b), o = 0;
	if (neg) out[o++] = '-';
	for (int i = 0; i < len; i++) {
		out[o++] = b[i];
		int rev = len - 1 - i;
		if (rev > 0 && rev % 3 == 0) out[o++] = ',';
	}
	out[o] = 0;
}

void state_switch(int st) { nextState = st; }

// screen transitions: every state change fades out (and fades the song
// out when leaving gameplay), then the new state fades in
static int trans;            // 0 none, 1 fading out, 2 fading in
float g_flash;               // white flash on both screens (FlxG.camera.flash)
static float flashDur = 1;

void screen_flash(float dur) {
	if (!save.flashing) return;
	g_flash = 1;
	flashDur = dur;
}

static void do_switch(void) {
	if (curState >= 0) states[curState].leave();
	tween_clear();
	video_close();
	gfx_reset_vram();
	font_unload_all();
	curState = nextState;
	nextState = -1;
	states[curState].enter();
}

static void fatal(const char *msg) {
	consoleDemoInit();
	printf("\n\n  Funkin DS\n\n  %s\n", msg);
	while (pmMainLoop()) swiWaitForVBlank();
	exit(0);
}

int main(int argc, char **argv) {
	defaultExceptionHandler();
	if (!nitroFSInit(NULL)) fatal("NitroFS init failed.\n  Run the .nds with a loader\n  that supports argv/NitroFS.");
	fatOk = fatInitDefault();
	srand(12345);

	gfx_init();
	video_init();
	sub_init();
	audio_init();
	save_load();
#ifdef TEST_BOTPLAY
	save.botplay = true;
#endif

#ifdef START_SONG
	g_song = START_SONG;
	g_diff = 2;
	nextState = ST_PLAY;
#else
	nextState = ST_TITLE;
#endif
	u32 lastTicks = time_ticks();

	while (pmMainLoop()) {
		if (nextState >= 0 && trans != 1) {
			if (curState < 0) {
				do_switch();
				g_fade = 1;
				trans = 2;
				lastTicks = time_ticks();
			} else trans = 1;
		}
		scanKeys();
		kDown = keysDown();
		kHeld = keysHeld();
		kUp = keysUp();
		touchDown = (kHeld & KEY_TOUCH) != 0;
		if (touchDown) touchRead(&touch);

		u32 now = time_ticks();
		float dt = (now - lastTicks) / (float)TICK_FREQ;
		lastTicks = now;
		if (curState == ST_PLAY) { perf_frames++; if (dt > 1.5f / 59.83f) perf_drops += (int)(dt * 59.83f + 0.5f) - 1; }
		if (dt > 0.1f) dt = 0.1f;
		if (dt < 0.001f) dt = 1.0f / 60;

		if (trans == 1) {
			// fading out: the state keeps drawing but takes no input
			g_fade += dt / 0.25f;
			if (curState == ST_PLAY || curState == ST_VIDEO || curState == ST_RESULTS || curState == ST_GAMEOVER ||
				nextState == ST_PLAY || nextState == ST_VIDEO)
				audio_set_volume(clampf(1 - g_fade, 0, 1));
			if (g_fade >= 1) {
				g_fade = 1;
				do_switch();
				g_fade = 1;
				g_flash = 0;
				trans = 2;
				lastTicks = time_ticks();
				continue;
			}
		} else {
			u32 t0 = time_ticks();
			states[curState].update(dt);
			prof_add(PF_UPDATE, t0);
			if (trans == 2 && nextState < 0) {
				g_fade -= dt / 0.3f;
				if (g_fade <= 0) { g_fade = 0; trans = 0; }
			}
		}
		tween_update(dt);
		if (g_flash > 0) g_flash -= dt / flashDur;

		u32 t1 = time_ticks();
		gfx_frame_begin();
		chr_frame_begin();
		states[curState].draw();
		gfx_frame_end();
		prof_add(PF_DRAW, t1);
		prof_add(PF_FRAME, now);

		swiWaitForVBlank();
		t1 = time_ticks();
		gfx_vblank();
		video_vblank();
		prof_add(PF_VBLANK, t1);
		t1 = time_ticks();
		sub_present();
		prof_add(PF_SUB, t1);
		if (++profFrames >= 60) { memcpy(prof_show, prof_max, sizeof(prof_max)); memset(prof_max, 0, sizeof(prof_max)); profFrames = 0; }
		// master brightness: smoothstep fades to black, white flashes
		float fa = clampf(g_fade, 0, 1), fs = clampf(g_fade > g_subFade ? g_fade : g_subFade, 0, 1);
		fa = fa * fa * (3 - 2 * fa);
		fs = fs * fs * (3 - 2 * fs);
		int b = (int)(fa * 16 + 0.5f), bs = (int)(fs * 16 + 0.5f);
		int w = (int)(clampf(g_flash, 0, 1) * 16 + 0.5f);
		REG_MASTER_BRIGHT = b ? ((2 << 14) | b) : w ? ((1 << 14) | w) : 0;
		REG_MASTER_BRIGHT_SUB = bs ? ((2 << 14) | bs) : w ? ((1 << 14) | w) : 0;
		t1 = time_ticks();
		audio_update();
		prof_add(PF_AUDIO, t1);
	}
	return 0;
}
