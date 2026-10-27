// WorldMusic DSR - FMOD 4.44.61 C API, resolved at runtime from the game's own fmodex64.dll / fmod_event64.dll.
// Every name below is in the export tables (MEASURED: musicdll/out/pe_info.txt). No FMOD headers needed.
#pragma once
#include <windows.h>
#include <string.h>

typedef int FRES;   // FMOD_RESULT, 0 = OK
#define FMOD_OK 0

// FMOD_MODE bits (FMOD Ex 4.44)
#define RM_LOOP_OFF      0x00000001
#define RM_2D            0x00000008
#define RM_SOFTWARE      0x00000040
#define RM_CREATESTREAM  0x00000080
#define RM_UNICODE       0x01000000
#define RM_TIMEUNIT_MS   0x00000001
#define RM_CHANNEL_FREE  (-1)
#define RM_EVENT_DEFAULT 0
#define RM_EVENT_STATE_PLAYING 0x00000008

// FMOD_CREATESOUNDEXINFO 4.44 x64, reconstructed: 216 bytes, ignoresetfilesystem int @192. Only that field is set:
// it makes FMOD use its own file IO instead of the game's setFileSystem callbacks (the exe imports setFileSystem).
// The harness proves the offset (failing user callbacks: plain open refused, flagged open succeeds).
struct RmExInfo { unsigned char b[216]; };
inline void rm_exinfo_bypass(RmExInfo* e) { memset(e, 0, sizeof *e); *(int*)(e->b + 0) = 216; *(int*)(e->b + 192) = 1; }

#define RM_FNS(X) \
  X(fe, FMOD_EventSystem_Create,          FRES(*)(void**)) \
  X(fe, FMOD_EventSystem_Init,            FRES(*)(void*, int, unsigned, void*, unsigned)) \
  X(fe, FMOD_EventSystem_SetMediaPath,    FRES(*)(void*, const char*)) \
  X(fe, FMOD_EventSystem_Load,            FRES(*)(void*, const char*, void*, void**)) \
  X(fe, FMOD_EventSystem_GetEvent,        FRES(*)(void*, const char*, unsigned, void**)) \
  X(fe, FMOD_EventSystem_GetCategory,     FRES(*)(void*, const char*, void**)) \
  X(fe, FMOD_EventSystem_GetCategoryByIndex, FRES(*)(void*, int, void**)) \
  X(fe, FMOD_EventSystem_GetNumCategories,FRES(*)(void*, int*)) \
  X(fe, FMOD_EventSystem_GetSystemObject, FRES(*)(void*, void**)) \
  X(fe, FMOD_EventSystem_Update,          FRES(*)(void*)) \
  X(fe, FMOD_EventSystem_Release,         FRES(*)(void*)) \
  X(fe, FMOD_EventCategory_GetInfo,       FRES(*)(void*, int*, char**)) \
  X(fe, FMOD_EventCategory_GetNumCategories, FRES(*)(void*, int*)) \
  X(fe, FMOD_EventCategory_GetCategoryByIndex, FRES(*)(void*, int, void**)) \
  X(fe, FMOD_EventCategory_GetChannelGroup, FRES(*)(void*, void**)) \
  X(fe, FMOD_EventCategory_GetVolume,     FRES(*)(void*, float*)) \
  X(fe, FMOD_EventCategory_SetVolume,     FRES(*)(void*, float)) \
  X(fe, FMOD_Event_Start,                 FRES(*)(void*)) \
  X(fe, FMOD_Event_Stop,                  FRES(*)(void*, int)) \
  X(fe, FMOD_Event_GetState,              FRES(*)(void*, unsigned*)) \
  X(fx, FMOD_System_SetOutput,            FRES(*)(void*, int)) \
  X(fx, FMOD_System_CreateStream,         FRES(*)(void*, const void*, unsigned, RmExInfo*, void**)) \
  X(fx, FMOD_System_PlaySound,            FRES(*)(void*, int, void*, int, void**)) \
  X(fx, FMOD_System_CreateChannelGroup,   FRES(*)(void*, const char*, void**)) \
  X(fx, FMOD_ChannelGroup_AddGroup,       FRES(*)(void*, void*)) \
  X(fx, FMOD_ChannelGroup_GetNumChannels, FRES(*)(void*, int*)) \
  X(fx, FMOD_ChannelGroup_GetChannel,     FRES(*)(void*, int, void**)) \
  X(fx, FMOD_ChannelGroup_GetNumGroups,   FRES(*)(void*, int*)) \
  X(fx, FMOD_ChannelGroup_GetGroup,       FRES(*)(void*, int, void**)) \
  X(fx, FMOD_ChannelGroup_GetVolume,      FRES(*)(void*, float*)) \
  X(fx, FMOD_ChannelGroup_GetName,        FRES(*)(void*, char*, int)) \
  X(fx, FMOD_Channel_IsPlaying,           FRES(*)(void*, int*)) \
  X(fx, FMOD_Channel_GetAudibility,       FRES(*)(void*, float*)) \
  X(fx, FMOD_Channel_SetChannelGroup,     FRES(*)(void*, void*)) \
  X(fx, FMOD_Channel_SetVolume,           FRES(*)(void*, float)) \
  X(fx, FMOD_Channel_SetPaused,           FRES(*)(void*, int)) \
  X(fx, FMOD_Channel_Stop,                FRES(*)(void*)) \
  X(fx, FMOD_Channel_GetPosition,         FRES(*)(void*, unsigned*, unsigned)) \
  X(fx, FMOD_Sound_GetLength,             FRES(*)(void*, unsigned*, unsigned)) \
  X(fx, FMOD_Sound_GetFormat,             FRES(*)(void*, int*, int*, int*, int*)) \
  X(fx, FMOD_Sound_GetSubSound,           FRES(*)(void*, int, void**)) \
  X(fx, FMOD_Sound_GetNumSubSounds,       FRES(*)(void*, int*)) \
  X(fx, FMOD_Sound_Release,               FRES(*)(void*))

struct RmApi {
#define RM_DECL(mod, name, type) using T_##name = type; T_##name name;
  RM_FNS(RM_DECL)
#undef RM_DECL
};

// Returns NULL on success, else the name of the first missing export.
inline const char* rm_load_api(RmApi* a, HMODULE fe, HMODULE fx) {
#define RM_GET(mod, name, type) a->name = (RmApi::T_##name)GetProcAddress(mod, #name); if (!a->name) return #name;
  RM_FNS(RM_GET)
#undef RM_GET
  return NULL;
}
