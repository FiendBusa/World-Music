// WorldMusic DSR core - see core.h. Stock track facts (MEASURED from the stock .fev/.fsb, WorldMusic catalog in dllmain.cpp):
//   every DSR music sample = FSB4 sub-sound, sample mode 0x10002240 (MPEG), 2 ch, 44100 or 48000 Hz (3 DLC tracks are 48 kHz).
//   Samples are found by their name inside the bank (e.g. frpg_sm10.fsb 'm100200000.wav' = Firelink Shrine).
#include "core.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static RmApi* A_ = nullptr;
static RmConfig C_;
static void (*L_)(const char*) = nullptr;
static void* sys_ = nullptr;     // FMOD::System
static void* cat_ = nullptr;     // music EventCategory
static void* mg_ = nullptr;      // its ChannelGroup
static void* ours_ = nullptr;    // our ChannelGroup (child of mg_)
static void* chan_ = nullptr;
static void* snd_ = nullptr;
static RmTrack T_[RM_MAX_TRACKS];
static int nT_ = 0, nfiles_ = 0, next_ = 0, cur_ = -1;
static RmTrack F_[RM_MAX_TRACKS];       // queued before init, in playlist order (stock + files)
static enum { IDLE, PLAYING, FADING_OUT } st_ = IDLE;
static ULONGLONG quiet_since_ = 0, phase_t0_ = 0, last_log_ = 0;
static float fade_ = 0.f, fade_from_ = 0.f;
static int last_game_ = -1;
static bool ok_ = false;

static void logf(const char* fmt, ...) {
  if (!L_ || !C_.log) return;
  char b[512]; va_list v; va_start(v, fmt); vsnprintf(b, sizeof b, fmt, v); va_end(v); L_(b);
}

// FSB4 walk: header 0x30 (magic, n, shdrsize, datasize, ver, mode, 24 bytes), then n headers (u16 size first),
// data contiguous from 0x30 + shdrsize; per sample: name @+2 (30 B), compressed size @+36, mode @+48, freq @+52, ch @+62.
static bool fsb_locate(RmTrack* t) {
  HANDLE h = CreateFileW(t->path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) { logf("%s: bank open failed err %lu", t->name, GetLastError()); return false; }
  unsigned char hd[0x30]; DWORD rd = 0; bool ok = false, found = false;
  if (ReadFile(h, hd, 0x30, &rd, nullptr) && rd == 0x30 && !memcmp(hd, "FSB4", 4)) {
    unsigned n, hs; memcpy(&n, hd + 4, 4); memcpy(&hs, hd + 8, 4);
    unsigned char* hb = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, hs);
    if (hb && ReadFile(h, hb, hs, &rd, nullptr) && rd == hs) {
      unsigned o = 0, data = 0x30 + hs;
      for (unsigned i = 0; i < n && o + 64 <= hs; i++) {
        unsigned short sz; memcpy(&sz, hb + o, 2);
        unsigned cb, mode; int freq; unsigned short ch;
        memcpy(&cb, hb + o + 36, 4); memcpy(&mode, hb + o + 48, 4); memcpy(&freq, hb + o + 52, 4); memcpy(&ch, hb + o + 62, 2);
        char name[31] = {0}; memcpy(name, hb + o + 2, 30);
        if (!_stricmp(name, t->wav)) {
          found = true;
          ok = mode == 0x10002240 && (freq == 44100 || freq == 48000) && ch == 2;
          logf("%s: sample #%u '%s' mode %08x %d Hz %u ch -> %s", t->name, i, name, mode, freq, ch, ok ? "OK" : "UNEXPECTED FORMAT");
          t->index = (int)i; t->offset = data; t->length = cb; break;
        }
        data += cb; o += sz;
      }
      if (!found) logf("%s: sample '%s' not in the bank", t->name, t->wav);
    }
    if (hb) HeapFree(GetProcessHeap(), 0, hb);
  } else logf("%s: bank is not FSB4", t->name);
  CloseHandle(h); return ok;
}

bool rm_add_stock_track(const char* key, const wchar_t* fsb, const char* wav, float event_vol, int vol_pct) {
  if (nfiles_ >= RM_MAX_TRACKS) return false;
  RmTrack& t = F_[nfiles_++]; memset(&t, 0, sizeof t);
  wcsncpy(t.path, fsb, MAX_PATH - 1); t.fsb = true; t.index = -1; t.base_vol = event_vol; t.vol_pct = vol_pct;
  snprintf(t.name, sizeof t.name, "%s", key); snprintf(t.wav, sizeof t.wav, "%s", wav);
  return true;
}

bool rm_add_file_track(const wchar_t* path) {
  if (nfiles_ >= RM_MAX_TRACKS) return false;
  RmTrack& t = F_[nfiles_++]; memset(&t, 0, sizeof t);
  wcsncpy(t.path, path, MAX_PATH - 1); t.index = -1; t.base_vol = -1.f;   // file: volume = CustomMusicVolume
  const wchar_t* b = wcsrchr(path, L'\\'); b = b ? b + 1 : path;
  WideCharToMultiByte(CP_UTF8, 0, b, -1, t.name, sizeof t.name - 1, nullptr, nullptr);
  return true;
}

// returns 1 running, 0 not ready yet (no 'music' category -> call again later), -1 permanent failure (stock FSB check)
int rm_core_init(RmApi* api, void* es, const wchar_t* sound_dir, const RmConfig& cfg, void (*lf)(const char*)) {
  A_ = api; C_ = cfg; L_ = lf;
  static int tracks_ok = 0;
  if (!tracks_ok) {
    nT_ = 0;
    for (int i = 0; i < nfiles_; i++) {
      RmTrack t = F_[i];
      if (t.fsb) {
        wchar_t p[MAX_PATH]; swprintf(p, MAX_PATH, L"%s%s", sound_dir, t.path); wcscpy(t.path, p);
        if (!fsb_locate(&t)) { logf("stock track %s skipped (bank check failed)", t.name); continue; }
      }
      T_[nT_++] = t;
    }
    for (int i = 0; i < nT_; i++) logf("playlist %d: %s", i + 1, T_[i].name);
    if (!nT_) { logf("playlist empty -> WorldMusic off"); return -1; }
    tracks_ok = 1;
  }
  FRES r = A_->FMOD_EventSystem_GetSystemObject(es, &sys_);
  if (r || !sys_) return 0;
  r = A_->FMOD_EventSystem_GetCategory(es, C_.category, &cat_);
  if (r || !cat_) return 0;
  r = A_->FMOD_EventCategory_GetChannelGroup(cat_, &mg_);
  if (r || !mg_) return 0;
  if (!ours_) {
    r = A_->FMOD_System_CreateChannelGroup(sys_, "worldmusic", &ours_);
    if (r || !ours_) { logf("createChannelGroup r=%d -> off", r); return -1; }
  }
  r = A_->FMOD_ChannelGroup_AddGroup(mg_, ours_);
  if (r) { logf("addGroup r=%d -> off", r); return -1; }
  quiet_since_ = GetTickCount64();
  logf("init OK: system %p category '%s' %p group %p ours %p; silence %d ms fade %d/%d gap %d custom volume %d%%",
       sys_, C_.category, cat_, mg_, ours_, C_.silence_ms, C_.fade_in_ms, C_.fade_out_ms, C_.gap_ms, C_.custom_volume_pct);
  ok_ = true; return 1;
}

// The game loads / unloads .fev projects at runtime: re-resolve the category + its group once a second and
// re-parent our group if FMOD handed out a different one (keeps the BGM slider and the detection on the live group).
static void recheck(void* es, ULONGLONG now) {
  static ULONGLONG next = 0;
  if (now < next) return;
  next = now + 1000;
  void* c = nullptr; void* g = nullptr;
  if (A_->FMOD_EventSystem_GetCategory(es, C_.category, &c) || !c) return;
  if (A_->FMOD_EventCategory_GetChannelGroup(c, &g) || !g) return;
  if (c != cat_ || g != mg_) {
    logf("music category/group changed %p/%p -> %p/%p, re-parenting", cat_, mg_, c, g);
    cat_ = c; mg_ = g; A_->FMOD_ChannelGroup_AddGroup(mg_, ours_);
  }
}

static int count_group(void* g, int depth) {
  if (!g || g == ours_ || depth > 8) return 0;
  int n = 0, c = 0, ng = 0;
  if (!A_->FMOD_ChannelGroup_GetNumChannels(g, &n))
    for (int i = 0; i < n; i++) {
      void* ch = nullptr; int pl = 0;
      if (!A_->FMOD_ChannelGroup_GetChannel(g, i, &ch) && ch && !A_->FMOD_Channel_IsPlaying(ch, &pl) && pl) c++;
    }
  if (!A_->FMOD_ChannelGroup_GetNumGroups(g, &ng))
    for (int i = 0; i < ng; i++) { void* sg = nullptr; if (!A_->FMOD_ChannelGroup_GetGroup(g, i, &sg)) c += count_group(sg, depth + 1); }
  return c;
}
int rm_count_game_channels() { return ok_ ? count_group(mg_, 0) : -1; }
void* rm_music_group() { return mg_; }
void* rm_music_category() { return cat_; }

static void stop_ours() {
  if (chan_) A_->FMOD_Channel_Stop(chan_);
  if (snd_) A_->FMOD_Sound_Release(snd_);
  chan_ = snd_ = nullptr; cur_ = -1;
}

// Stock track: its own volume, 70 = exactly as loud as in the game (vanilla event volume), linear. Custom file:
// CustomMusicVolume, 100 = full channel volume. Both sit under the game's music slider; capped at 1.0.
static float track_vol(const RmTrack& t) {
  float v = t.base_vol >= 0.f ? t.base_vol * t.vol_pct / 70.f : C_.custom_volume_pct / 100.f;
  return v > 1.f ? 1.f : v < 0.f ? 0.f : v;
}

static bool start_track(int i) {
  // Stock: open the whole FSB as a stream and play sub-sound <index> (the game's FMOD has the FSB codec; a raw MPEG
  // range is refused with FMOD_ERR_FORMAT 25, MEASURED harness1.txt). File: open it directly (out/fmtprobe.txt).
  RmTrack& t = T_[i];
  RmExInfo ex; rm_exinfo_bypass(&ex);
  LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
  FRES r = A_->FMOD_System_CreateStream(sys_, t.path, RM_2D | RM_SOFTWARE | RM_CREATESTREAM | RM_LOOP_OFF | RM_UNICODE, &ex, &snd_);
  QueryPerformanceCounter(&t1);
  logf("open %s took %.1f ms r=%d", t.name, (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart, r);
  if (r || !snd_) { snd_ = nullptr; return false; }
  int ns = 0; void* play = snd_;
  if (t.fsb) {
    A_->FMOD_Sound_GetNumSubSounds(snd_, &ns);
    r = A_->FMOD_Sound_GetSubSound(snd_, t.index, &play);
    if (r || !play) { logf("getSubSound %s #%d of %d r=%d", t.name, t.index, ns, r); A_->FMOD_Sound_Release(snd_); snd_ = nullptr; return false; }
  }
  unsigned len = 0; A_->FMOD_Sound_GetLength(play, &len, RM_TIMEUNIT_MS);
  r = A_->FMOD_System_PlaySound(sys_, RM_CHANNEL_FREE, play, 1, &chan_);
  if (r || !chan_) { logf("playSound r=%d", r); A_->FMOD_Sound_Release(snd_); snd_ = chan_ = nullptr; return false; }
  A_->FMOD_Channel_SetChannelGroup(chan_, ours_);
  A_->FMOD_Channel_SetVolume(chan_, 0.f);
  A_->FMOD_Channel_SetPaused(chan_, 0);
  cur_ = i; logf("START %d/%d %s (length ~%u ms, volume %.3f)", i + 1, nT_, t.name, len, track_vol(t));
  return true;
}

static int pick_next(int last) {
  if (nT_ <= 1) return 0;
  if (!C_.shuffle) return (last + 1) % nT_;
  int k = (int)(GetTickCount64() % (ULONGLONG)(nT_ - 1));   // any track except the one just played
  return k >= last ? k + 1 : k;
}

void rm_core_tick(void* es) {
  if (!ok_) return;
  ULONGLONG now = GetTickCount64();
  recheck(es, now);
  int game = count_group(mg_, 0);
  if ((game > 0) != (last_game_ > 0)) logf("game music channels %d -> %d", last_game_, game);
  last_game_ = game;
  if (game > 0) quiet_since_ = now;
  switch (st_) {
  case IDLE:
    if (!game && now - quiet_since_ >= (ULONGLONG)C_.silence_ms) {
      int i = next_;
      if (start_track(i)) { next_ = pick_next(i); st_ = PLAYING; phase_t0_ = now; fade_ = 0.f; }
      else { next_ = pick_next(i); quiet_since_ = now; }   // skip a broken file, retry after another silence window
    }
    break;
  case PLAYING: {
    int pl = 0;
    if (!chan_ || A_->FMOD_Channel_IsPlaying(chan_, &pl) || !pl) {   // track ended
      logf("END %s", T_[cur_ < 0 ? 0 : cur_].name); stop_ours(); st_ = IDLE;
      quiet_since_ = now - C_.silence_ms + C_.gap_ms; break;
    }
    if (game) { st_ = FADING_OUT; phase_t0_ = now; fade_from_ = fade_; logf("game music started -> fade out"); break; }
    float k = C_.fade_in_ms > 0 ? (float)(now - phase_t0_) / C_.fade_in_ms : 1.f;
    fade_ = k > 1.f ? 1.f : k;
    break; }
  case FADING_OUT: {
    float k = C_.fade_out_ms > 0 ? (float)(now - phase_t0_) / C_.fade_out_ms : 1.f;
    fade_ = fade_from_ * (k > 1.f ? 0.f : 1.f - k);
    if (k >= 1.f) { stop_ours(); st_ = IDLE; quiet_since_ = now; logf("faded out"); }
    break; }
  }
  if (chan_) A_->FMOD_Channel_SetVolume(chan_, track_vol(T_[cur_ < 0 ? 0 : cur_]) * fade_);
  if (C_.log >= 2 && now - last_log_ >= 1000) {
    last_log_ = now; unsigned pos = 0; if (chan_) A_->FMOD_Channel_GetPosition(chan_, &pos, RM_TIMEUNIT_MS);
    logf("tick game=%d state=%d track=%d pos=%u fade=%.2f", game, (int)st_, cur_, pos, fade_);
  }
}

RmStatus rm_core_status() {
  RmStatus s = { last_game_, 0, cur_, 0, fade_ };
  int pl = 0; if (chan_ && !A_->FMOD_Channel_IsPlaying(chan_, &pl)) s.ours_playing = pl;
  if (chan_) A_->FMOD_Channel_GetPosition(chan_, &s.pos_ms, RM_TIMEUNIT_MS);
  return s;
}
void* rm_channel() { return chan_; }
