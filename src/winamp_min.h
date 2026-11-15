// Minimal Winamp 5.x plug-in API definitions used by Music Practice.
// Re-declared from the public Winamp SDK documentation (DSP.H, IN2.H, OUT.H,
// wa_ipc.h) so the project builds without the SDK installed.
#pragma once
#include <windows.h>
#include <stdint.h>

// ---------------------------------------------------------------- DSP.H
#define DSP_HDRVER 0x20

typedef struct winampDSPModule {
    char*     description;
    HWND      hwndParent;     // filled in by Winamp: main window
    HINSTANCE hDllInstance;   // filled in by Winamp
    void (*Config)(struct winampDSPModule* this_mod);
    int  (*Init)(struct winampDSPModule* this_mod);   // 0 on success
    // Returns the number of samples written back into `samples`.
    // Must be between numsamples/2 and numsamples*2; the buffer holds 2x.
    int  (*ModifySamples)(struct winampDSPModule* this_mod, short int* samples,
                          int numsamples, int bps, int nch, int srate);
    void (*Quit)(struct winampDSPModule* this_mod);
    void* userData;
} winampDSPModule;

typedef struct {
    int   version;
    char* description;
    winampDSPModule* (*getModule)(int);
    int (*sf)(int key);       // only read when version >= 0x21
} winampDSPHeader;

// ---------------------------------------------------------------- OUT.H
typedef struct {
    int       version;
    char*     description;
    intptr_t  id;
    HWND      hMainWindow;
    HINSTANCE hDllInstance;
    void (*Config)(HWND hwndParent);
    void (*About)(HWND hwndParent);
    void (*Init)();
    void (*Quit)();
    int  (*Open)(int samplerate, int numchannels, int bitspersamp, int bufferlenms, int prebufferms);
    void (*Close)();
    int  (*Write)(char* buf, int len);
    int  (*CanWrite)();
    int  (*IsPlaying)();
    int  (*Pause)(int pause);
    void (*SetVolume)(int volume);
    void (*SetPan)(int pan);
    void (*Flush)(int t);
    int  (*GetOutputTime)();   // ms actually played
    int  (*GetWrittenTime)();  // ms written into the output buffer
} Out_Module;

// ---------------------------------------------------------------- IN2.H
typedef struct {
    int       version;
    char*     description;
    HWND      hMainWindow;
    HINSTANCE hDllInstance;
    char*     FileExtensions;
    int       is_seekable;
    int       UsesOutputPlug;
    void (*Config)(HWND hwndParent);
    void (*About)(HWND hwndParent);
    void (*Init)();
    void (*Quit)();
    void (*GetFileInfo)(const char* file, char* title, int* length_in_ms);
    int  (*InfoBox)(const char* file, HWND hwndParent);
    int  (*IsOurFile)(const char* fn);
    int  (*Play)(const char* fn);
    void (*Pause)();
    void (*UnPause)();
    int  (*IsPaused)();
    void (*Stop)();
    int  (*GetLength)();
    int  (*GetOutputTime)();
    void (*SetOutputTime)(int time_in_ms);
    void (*SetVolume)(int volume);
    void (*SetPan)(int pan);
    void (*SAVSAInit)(int maxlatency_in_ms, int srate);
    void (*SAVSADeInit)();
    void (*SAAddPCMData)(void* PCMData, int nch, int bps, int timestamp);
    int  (*SAGetMode)();
    int  (*SAAdd)(void* data, int timestamp, int csa);
    void (*VSAAddPCMData)(void* PCMData, int nch, int bps, int timestamp);
    int  (*VSAGetMode)(int* specNch, int* specNumBands);
    int  (*VSAAdd)(void* data, int timestamp);
    void (*VSASetInfo)(int srate, int nch);
    int  (*dsp_isactive)();
    int  (*dsp_dosamples)(short int* samples, int numsamples, int bps, int nch, int srate);
    void (*EQSet)(int on, char data[10], int preamp);
    void (*SetInfo)(int bitrate, int srate, int stereo, int synched);
    Out_Module* outMod;
} In_Module;

// ---------------------------------------------------------------- wa_ipc.h
#define WM_WA_IPC WM_USER

#define IPC_ISPLAYING          104   // 1 playing, 3 paused, 0 stopped
#define IPC_GETOUTPUTTIME      105   // wParam 0: position ms, 1: length s
#define IPC_JUMPTOTIME         106   // wParam ms
#define IPC_GETLISTPOS         125
#define IPC_GETPLAYLISTFILE    211   // wParam index -> char*
#define IPC_GETPLAYLISTFILEW   214   // wParam index -> wchar_t*
#define IPC_GET_HMENU          281   // wParam 0 -> main popup menu
#define IPC_GETINIDIRECTORY    335   // -> char*
#define IPC_GET_EMBEDIF        505
#define IPC_CANPLAY            566   // wParam char* filename -> In_Module*

#define WINAMP_OPTIONS_EQ      40036
#define WINAMP_BUTTON2         40045 // play
#define WINAMP_BUTTON3         40046 // pause

typedef struct {
    HWND     me;
    int      flags;
    RECT     r;
    void*    user_ptr;
    intptr_t extra_data[64];
} embedWindowState;

#define EMBED_FLAGS_NORESIZE        0x1
#define EMBED_FLAGS_NOTRANSPARENCY  0x2
#define EMBED_FLAGS_NOWINDOWMENU    0x4
#define EMBED_FLAGS_GUID            0x8

#define SET_EMBED_GUID(ws, g) { (ws)->flags |= EMBED_FLAGS_GUID; *((GUID*)&(ws)->extra_data[4]) = (g); }
