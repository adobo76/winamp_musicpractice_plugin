#pragma once
#include <windows.h>
#include <atomic>
#include "winamp_min.h"

#define MUSICPRACTICE_VERSION "1.0.0"

namespace pl {

// Sound settings shared between the UI thread and Winamp's decode thread.
// Values are stored in tenths: tempo/speed in 0.1 %, pitch in 0.1 semitone.
extern std::atomic<int>  g_tempo10;
extern std::atomic<int>  g_pitch10;
extern std::atomic<int>  g_speed10;
extern std::atomic<bool> g_clearRequest;   // drop buffered audio (after a seek)
extern std::atomic<bool> g_enabled;        // ON button: false = bypass, keep settings

extern HINSTANCE g_hInst;
extern HWND      g_hwndWinamp;

// dsp.cpp
void DspInit();
void DspQuit();
int  DspProcess(short* samples, int numsamples, int bps, int nch, int srate);
// Given how many ms of (processed) audio sit in the output plugin's buffer,
// returns how far, in source-track ms, the audio you hear lags the decoder.
bool DspSourceLagMs(int outputBufferedMs, double* lagMs);

// ui.cpp
void UiInit();
void UiQuit();
void UiToggle();

void DebugLog(const char* fmt, ...);

} // namespace pl
