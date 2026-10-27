// WorldMusic DSR - DLL for DS Mod Loader (Nexus 1140, game root dinput8.dll, loads mods\dll\*.dll ~1 s after start).
// Hooks the exe's import of FMOD::EventSystem::update (IAT entry, works whenever we load) and runs the core right after
// the game's own update, on the game's sound thread. Config WorldMusic.ini + log WorldMusic.log next to this DLL,
// the player's own music in WorldMusic_Custom_MP3\ next to this DLL. Ships no audio: stock tracks stream from the
// player's own sound\*.fsb banks.
#include "core.h"
#include <stdio.h>

static const char* UPDATE_NAME = "?update@EventSystem@FMOD@@QEAA?AW4FMOD_RESULT@@XZ";
typedef FRES (*UpdateFn)(void*);
static UpdateFn orig_update = nullptr;
static RmApi api;
static RmConfig cfg;
static wchar_t sound_dir[MAX_PATH];
static FILE* logf_ = nullptr;
static int state = 0;              // 0 waiting for the music category, 1 running, 2 disabled
static ULONGLONG next_try = 0;
static int tries = 0;

static void wlog(const char* s) {
  if (!logf_) return;
  SYSTEMTIME t; GetLocalTime(&t);
  fprintf(logf_, "%02d:%02d:%02d.%03d %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, s); fflush(logf_);
}
static void wlogf(const char* fmt, ...) { char b[512]; va_list v; va_start(v, fmt); vsnprintf(b, sizeof b, fmt, v); va_end(v); wlog(b); }

static int step(void* es) {        // returns new state; no C++ objects here: called under SEH
  if (state == 0) {
    ULONGLONG now = GetTickCount64();
    if (now < next_try) return 0;
    next_try = now + 1000;
    int r = rm_core_init(&api, es, sound_dir, cfg, wlog);
    if (r > 0) { wlogf("core running (after %d tries)", tries + 1); return 1; }
    if (r < 0) return 2;
    if (++tries == 1 || tries % 60 == 0) wlogf("music category not ready yet (try %d)", tries);
    return tries > 600 ? 2 : 0;    // give up after ~10 min without a 'music' category
  }
  rm_core_tick(es);
  return 1;
}
static FRES hook_update(void* es) {
  FRES r = orig_update(es);
  if (state != 2) {
    __try { state = step(es); }
    __except (EXCEPTION_EXECUTE_HANDLER) { state = 2; wlogf("EXCEPTION 0x%08lx in WorldMusic -> disabled, game unaffected", GetExceptionCode()); }
  }
  return r;
}

static bool patch_iat(void* target) {
  BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
  auto dos = (IMAGE_DOS_HEADER*)base; auto nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
  auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  for (auto d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++) {
    if (_stricmp((char*)(base + d->Name), "fmod_event64.dll")) continue;
    auto iat = (ULONGLONG*)(base + d->FirstThunk);
    auto names = (IMAGE_THUNK_DATA64*)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
    for (int i = 0; iat[i]; i++) {
      bool by_name = d->OriginalFirstThunk && !(names[i].u1.Ordinal & IMAGE_ORDINAL_FLAG64) &&
                     !strcmp((char*)((IMAGE_IMPORT_BY_NAME*)(base + names[i].u1.AddressOfData))->Name, UPDATE_NAME);
      if (iat[i] != (ULONGLONG)target && !by_name) continue;
      DWORD old; ULONGLONG* slot = &iat[i];
      if (!VirtualProtect(slot, 8, PAGE_READWRITE, &old)) { wlogf("VirtualProtect failed %lu", GetLastError()); return false; }
      orig_update = (UpdateFn)*slot;          // chain whatever is there (stock or another mod's hook)
      if (((ULONGLONG)slot & 7) == 0) InterlockedExchange64((LONG64*)slot, (LONG64)&hook_update);
      else *slot = (ULONGLONG)&hook_update;
      VirtualProtect(slot, 8, old, &old);
      wlogf("IAT hook on EventSystem::update: slot %p (%s) orig %p (%s)", slot, by_name ? "by name" : "by address",
            orig_update, orig_update == target ? "stock" : "chained");
      return true;
    }
  }
  wlog("EventSystem::update import not found -> WorldMusic off");
  return false;
}

// Stock DSR music, one row per sample (MEASURED: event -> sounddef -> wave from the stock .fev, sample present in the
// .fsb, all MPEG stereo). vol = the vanilla event volume (FirelinkShrine keeps 0.569, the level approved in game for 1.0).
// Per-track ini volume <Key>Volume: 70 = in-game loudness; shipped default 60, FirelinkShrine + CharacterCreation 70.
// Names: map ids + MSB boss-room labels; the rows marked 'likely' in WorldMusic.ini are inferred from the room layout.
static const struct { const char* key; const wchar_t* fsb; const char* wav; float vol; int on; } CATALOG[] = {
  { "FirelinkShrine",        L"frpg_sm10.fsb",  "m100200000.wav",    0.569f, 1 },
  { "CharacterCreation",     L"frpg_smain.fsb", "config.wav",        0.697f, 1 },
  { "Opening",               L"frpg_smain.fsb", "proto-opening.wav", 1.000f, 0 },
  { "AshLake",               L"frpg_sm13.fsb",  "m130200000.wav",    0.655f, 0 },
  { "KilnOfTheFirstFlame",   L"frpg_sm18.fsb",  "m180000009.wav",    0.975f, 0 },
  { "AsylumDemon",           L"frpg_sm18.fsb",  "m180100001.wav",    0.470f, 0 },
  { "StrayDemon",            L"frpg_sm18.fsb",  "m180100002.wav",    0.488f, 0 },
  { "TaurusDemon",           L"frpg_sm10.fsb",  "m100000001.wav",    0.673f, 0 },
  { "BellGargoyles",         L"frpg_sm10.fsb",  "m100000000.wav",    0.751f, 0 },
  { "CapraDemon",            L"frpg_sm10.fsb",  "m100100002.wav",    0.665f, 0 },
  { "GapingDragon",          L"frpg_sm10.fsb",  "m100100000.wav",    0.720f, 0 },
  { "MoonlightButterfly",    L"frpg_sm12.fsb",  "m120000001.wav",    0.384f, 0 },
  { "GreatGreyWolfSif",      L"frpg_sm12.fsb",  "m120000000.wav",    0.423f, 0 },
  { "ChaosWitchQuelaag",     L"frpg_sm14.fsb",  "m140000000.wav",    0.591f, 0 },
  { "QuelaagsSister",        L"frpg_sm14.fsb",  "m140000001.wav",    0.488f, 0 },
  { "IronGolem",             L"frpg_sm15.fsb",  "m150000000.wav",    0.552f, 0 },
  { "OrnsteinAndSmough",     L"frpg_sm15.fsb",  "m150100000.wav",    0.505f, 0 },
  { "DarkSunGwyndolin",      L"frpg_sm15.fsb",  "m150100001.wav",    0.388f, 0 },
  { "Gwynevere",             L"frpg_sm15.fsb",  "m150100002.wav",    0.276f, 0 },
  { "CrossbreedPriscilla",   L"frpg_sm11.fsb",  "m110000000.wav",    0.500f, 0 },
  { "Pinwheel",              L"frpg_sm13.fsb",  "m130100000.wav",    0.703f, 0 },
  { "GravelordNito",         L"frpg_sm13.fsb",  "m130000000.wav",    0.720f, 0 },
  { "SeathTheScaleless",     L"frpg_sm17.fsb",  "m170000000.wav",    0.652f, 0 },
  { "FourKings",             L"frpg_sm16.fsb",  "m160000000.wav",    0.505f, 0 },
  { "DemonRuinsMidBoss1",    L"frpg_sm14.fsb",  "m140100000.wav",    0.617f, 0 },
  { "DemonRuinsMidBoss2",    L"frpg_sm14.fsb",  "m140100002.wav",    0.591f, 0 },
  { "DemonRuinsMidBoss3",    L"frpg_sm14.fsb",  "m140100003.wav",    0.608f, 0 },
  { "BedOfChaos",            L"frpg_sm14.fsb",  "m140100001.wav",    0.652f, 0 },
  { "GwynLordOfCinder",      L"frpg_sm18.fsb",  "m180000000.wav",    0.781f, 0 },
  { "SanctuaryGuardian",     L"fdlc_sm12.fsb",  "m120100001.wav",    0.603f, 0 },
  { "KnightArtorias",        L"fdlc_sm12.fsb",  "m120100002.wav",    0.624f, 0 },
  { "ManusFatherOfTheAbyss", L"fdlc_sm12.fsb",  "m120100003.wav",    0.603f, 0 },
  { "BlackDragonKalameet",   L"fdlc_sm12.fsb",  "m120100004.wav",    0.603f, 0 },
  { "BattleOfStoicismArena", L"fdlc_sm12.fsb",  "m120199999.wav",    0.612f, 0 },
};

// Every .ogg / .mp3 / .wav / .flac in WorldMusic_Custom_MP3\ joins the playlist after the stock tracks, A-Z.
static void scan_music(const wchar_t* dir) {
  static wchar_t names[RM_MAX_TRACKS][MAX_PATH]; int n = 0;
  wchar_t pat[MAX_PATH]; swprintf(pat, MAX_PATH, L"%s\\*", dir);
  WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      const wchar_t* e = wcsrchr(fd.cFileName, L'.');
      if (!e || (_wcsicmp(e, L".ogg") && _wcsicmp(e, L".mp3") && _wcsicmp(e, L".wav") && _wcsicmp(e, L".flac"))) continue;
      if (n < RM_MAX_TRACKS) wcsncpy(names[n++], fd.cFileName, MAX_PATH - 1);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  for (int i = 1; i < n; i++)            // insertion sort, case-insensitive A-Z
    for (int j = i; j > 0 && _wcsicmp(names[j - 1], names[j]) > 0; j--) {
      wchar_t t[MAX_PATH]; wcscpy(t, names[j]); wcscpy(names[j], names[j - 1]); wcscpy(names[j - 1], t);
    }
  for (int i = 0; i < n; i++) { wchar_t p[MAX_PATH]; swprintf(p, MAX_PATH, L"%s\\%s", dir, names[i]); rm_add_file_track(p); }
  wlogf("custom music files in %ls: %d", dir, n);
}

static int ini_int(const wchar_t* ini, const wchar_t* sec, const wchar_t* k, int d) { return (int)GetPrivateProfileIntW(sec, k, d, ini); }

static void start(HMODULE self) {
  wchar_t dir[MAX_PATH], ini[MAX_PATH], lp[MAX_PATH];
  GetModuleFileNameW(self, dir, MAX_PATH); wchar_t* s = wcsrchr(dir, L'\\'); if (s) *s = 0;
  swprintf(ini, MAX_PATH, L"%s\\WorldMusic.ini", dir);
  const wchar_t* W = L"WorldMusic";
  cfg.log = ini_int(ini, W, L"Log", 1);
  if (cfg.log) { swprintf(lp, MAX_PATH, L"%s\\WorldMusic.log", dir); logf_ = _wfopen(lp, L"w"); }
  cfg.silence_ms = ini_int(ini, W, L"SilenceSeconds", 5) * 1000;
  cfg.fade_in_ms = ini_int(ini, W, L"FadeInMs", 3000);
  cfg.fade_out_ms = ini_int(ini, W, L"FadeOutMs", 2000);
  cfg.gap_ms = ini_int(ini, W, L"GapSeconds", 3) * 1000;
  cfg.custom_volume_pct = ini_int(ini, W, L"CustomMusicVolume", 70);
  cfg.shuffle = ini_int(ini, W, L"Shuffle", 0);
  if (ini_int(ini, W, L"Enabled", 1) == 0) { wlog("Enabled=0 -> off"); return; }
  for (auto& c : CATALOG) {              // missing key -> the shipped default
    wchar_t k[64], kv[80]; MultiByteToWideChar(CP_ACP, 0, c.key, -1, k, 64); swprintf(kv, 80, L"%sVolume", k);
    if (ini_int(ini, L"Tracks", k, c.on)) rm_add_stock_track(c.key, c.fsb, c.wav, c.vol, ini_int(ini, L"Tracks", kv, c.on ? 70 : 60));
  }
  if (ini_int(ini, W, L"CustomMusic", 1)) {
    wchar_t cd[MAX_PATH]; swprintf(cd, MAX_PATH, L"%s\\WorldMusic_Custom_MP3", dir); scan_music(cd);
  }
  wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); s = wcsrchr(exe, L'\\'); if (s) *s = 0;
  swprintf(sound_dir, MAX_PATH, L"%s\\sound\\", exe);
  { wchar_t sd[MAX_PATH]; GetPrivateProfileStringW(W, L"SoundDir", L"", sd, MAX_PATH, ini);   // test override only
    if (sd[0]) wcscpy(sound_dir, sd); }
  wlogf("WorldMusic DSR 1.0 loaded; game sound dir %ls", sound_dir);
  HMODULE fe = GetModuleHandleW(L"fmod_event64.dll"), fx = GetModuleHandleW(L"fmodex64.dll");
  if (!fe || !fx) { wlog("FMOD DLLs not loaded in this process -> off"); return; }
  const char* miss = rm_load_api(&api, fe, fx);
  if (miss) { wlogf("missing FMOD export %s -> off", miss); return; }
  patch_iat((void*)GetProcAddress(fe, UPDATE_NAME));
}

BOOL WINAPI DllMain(HMODULE h, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); start(h); }
  return TRUE;
}
