// WorldMusic DSR core: plays stock DSR music tracks (picked in WorldMusic.ini) + the player's own files when the
// game's 'music' category is silent. Driven from the game's own EventSystem::update (DLL) or from the offline harness.
// Single-threaded by design.
#pragma once
#include "fmodapi.h"

struct RmConfig {
  int silence_ms = 5000;      // game music silent this long -> start our track
  int fade_in_ms = 3000;
  int fade_out_ms = 2000;
  int gap_ms = 3000;          // pause between our own tracks
  int custom_volume_pct = 70; // custom files: 100 = full channel volume (stock tracks carry their own volume per track)
  int log = 1;
  int shuffle = 0;            // 0 = playlist order (enabled stock tracks, then custom files A-Z), 1 = random, never the same twice in a row
  char category[64] = "music";
};

struct RmTrack {              // a stock FSB4 sub-sound (fsb == true) or an external audio file
  wchar_t path[MAX_PATH]; char name[64]; char wav[32]; bool fsb; int index;
  unsigned offset, length; float base_vol; int vol_pct;
};
#define RM_MAX_TRACKS 256

struct RmStatus { int game_channels; int ours_playing; int track; unsigned pos_ms; float fade; };

int rm_core_init(RmApi* api, void* es, const wchar_t* sound_dir, const RmConfig& cfg, void (*logf)(const char*));
void rm_core_tick(void* es);          // call once per EventSystem::update
RmStatus rm_core_status();
// call before rm_core_init, in playlist order: a stock track = sample <wav> inside <sound dir>\<fsb> (found by name),
// played at its vanilla event volume; a file = .ogg .mp3 .wav .flac (all MEASURED playable)
bool rm_add_stock_track(const char* key, const wchar_t* fsb, const char* wav, float event_vol, int vol_pct);
bool rm_add_file_track(const wchar_t* path);
int rm_count_game_channels();         // exposed for the harness
void* rm_music_group();
void* rm_music_category();
void* rm_channel();
