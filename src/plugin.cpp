// Winamp DSP plug-in entry points.
#include "musicpractice.h"
#include <cstdio>
#include <cstdarg>

namespace pl {
HINSTANCE g_hInst = nullptr;
HWND      g_hwndWinamp = nullptr;

void DebugLog(const char* fmt, ...) {
    static int enabled = -1;
    if (enabled < 0) enabled = GetEnvironmentVariableA("MUSICPRACTICE_DEBUG", nullptr, 0) > 0;
    if (!enabled) return;
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    char path[MAX_PATH];
    GetTempPathA(MAX_PATH, path);
    strcat_s(path, "musicpractice_debug.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") == 0 && f) {
        fprintf(f, "%10lu %s\n", GetTickCount(), buf);
        fclose(f);
    }
}
} // namespace pl

using namespace pl;

static void Config(winampDSPModule*) { UiToggle(); }

static int Init(winampDSPModule* m) {
    g_hInst = m->hDllInstance;
    g_hwndWinamp = m->hwndParent;
    DspInit();
    UiInit();
    return 0;
}

static int ModifySamples(winampDSPModule*, short int* samples, int numsamples, int bps, int nch, int srate) {
    return DspProcess(samples, numsamples, bps, nch, srate);
}

static void Quit(winampDSPModule*) {
    UiQuit();
    DspQuit();
}

static winampDSPModule g_module = {
    (char*)"Music Practice " MUSICPRACTICE_VERSION " - tempo, pitch & A-B loop",
    nullptr, nullptr, Config, Init, ModifySamples, Quit, nullptr
};

static winampDSPModule* GetModule(int i) { return i == 0 ? &g_module : nullptr; }

static winampDSPHeader g_header = {
    DSP_HDRVER, (char*)"Music Practice " MUSICPRACTICE_VERSION, GetModule, nullptr
};

extern "C" __declspec(dllexport) winampDSPHeader* winampDSPGetHeader2() { return &g_header; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_hInst = h; DisableThreadLibraryCalls(h); }
    return TRUE;
}
