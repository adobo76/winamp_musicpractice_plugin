// Music Practice panel: a Winamp "embedded window" (same frame the EQ / Media
// Library use, so it takes the skin and snaps/docks with Winamp's windows)
// containing a custom-drawn control panel.
#include "musicpractice.h"
#include <commctrl.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>
#include <climits>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")

namespace pl {
namespace {

// ------------------------------------------------------------------ config
const wchar_t* kPanelClass = L"MusicPracticePanel";
const UINT     kMenuId     = 47711;
const UINT_PTR kSubclassId = 0x504C4F50;
const int      kDesignW = 256, kDesignH = 120;         // panel content at 1x
const int      kInsetL = 11, kInsetT = 20, kInsetR = 8, kInsetB = 14; // classic frame
int            scale = 1;                              // 2 in Winamp double-size mode
int  FrameW() { return 275 * scale; }
int  FrameH() { return kInsetT + kInsetB + kDesignH * scale; }
// {8D7C4F21-5A3B-4E2D-9C61-2B7E5A0F1D93}
const GUID kEmbedGuid = { 0x8d7c4f21, 0x5a3b, 0x4e2d, { 0x9c, 0x61, 0x2b, 0x7e, 0x5a, 0x0f, 0x1d, 0x93 } };

enum { TIMER_POLL = 1, TIMER_REPEAT = 2 };

// ------------------------------------------------------------------ colours
const COLORREF cBg       = RGB(28, 28, 40);
const COLORREF cLabel    = RGB(190, 196, 214);
const COLORREF cDim      = RGB(110, 116, 136);
const COLORREF cLcdBg    = RGB(0, 0, 0);
const COLORREF cLcdText  = RGB(0, 232, 0);
const COLORREF cLcdDim   = RGB(0, 96, 0);
const COLORREF cBtnFace  = RGB(166, 168, 186);
const COLORREF cBtnHi    = RGB(226, 228, 240);
const COLORREF cBtnLo    = RGB(72, 72, 90);
const COLORREF cBtnText  = RGB(24, 24, 36);
const COLORREF cGroove   = RGB(8, 8, 14);
const COLORREF cFill     = RGB(0, 180, 40);
const COLORREF cFillHot  = RGB(220, 200, 0);
const COLORREF cSep      = RGB(60, 62, 82);

// ------------------------------------------------------------------ state
embedWindowState ews;
HWND  hwndFrame = nullptr, hwndPanel = nullptr;
HFONT fontUi = nullptr, fontBtn = nullptr, fontLcd = nullptr;

int OffsetX() {
    RECT rc = { 0, 0, FrameW() - kInsetL - kInsetR, 0 };
    if (hwndPanel) GetClientRect(hwndPanel, &rc);
    return std::max(0, (int)(rc.right / scale - kDesignW) / 2);
}
std::wstring iniPath;
bool  menuAdded = false, subclassed = false;

bool  autoReset = false;
int   loopA = -1, loopB = -1;      // ms in the track, -1 = unset
bool  loopOn = false;
int   posMs = -1;                  // what you are hearing now
DWORD cooldownUntil = 0;
std::wstring lastFile;
int   lastListPos = -2;

int   pressedId = 0;               // button held down
bool  pressedInside = false;
int   dragId = 0;                  // slider being dragged

// ------------------------------------------------------------------ widgets
enum Kind { K_SLIDER, K_BUTTON, K_TOGGLE };
enum Id {
    W_NONE,
    W_TEMPO, W_PITCH, W_SPEED,
    W_TEMPO_DN, W_PITCH_DN, W_SPEED_DN,
    W_TEMPO_UP, W_PITCH_UP, W_SPEED_UP,
    W_RESET, W_AUTORESET, W_ENABLE,
    W_A_SET, W_A_BB, W_A_B, W_A_F, W_A_FF, W_START,
    W_B_SET, W_B_BB, W_B_B, W_B_F, W_B_FF,
    W_M_BB, W_M_B, W_M_F, W_M_FF,
    W_LOOP,
};

struct Widget { int id; Kind kind; RECT r; const wchar_t* text; bool repeat; };
std::vector<Widget> widgets;

struct SliderDef { int id; std::atomic<int>* v; int mn, mx, step; const wchar_t* label; };
SliderDef sliders[3] = {
    { W_TEMPO, &g_tempo10, -500, 1000, 10, L"TEMPO" },
    { W_PITCH, &g_pitch10, -120,  120, 10, L"PITCH" },
    { W_SPEED, &g_speed10, -500, 1000, 10, L"SPEED" },
};

// LCD readouts (not interactive)
const int kRowH = 13;
RECT rcSliderVal[3], rcRate, rcA, rcB, rcLen;

RECT R(int x, int y, int w, int h) { RECT r = { x, y, x + w, y + h }; return r; }

void Add(int id, Kind k, RECT r, const wchar_t* t, bool rep = false) { widgets.push_back({ id, k, r, t, rep }); }

void BuildLayout() {
    widgets.clear();
    for (int i = 0; i < 3; i++) {
        int y = 3 + i * 16;
        Add(W_TEMPO_DN + i, K_BUTTON, R(42, y, 11, kRowH), L"-", true);
        Add(W_TEMPO + i,    K_SLIDER, R(56, y, 113, kRowH), nullptr);
        Add(W_TEMPO_UP + i, K_BUTTON, R(172, y, 11, kRowH), L"+", true);
        rcSliderVal[i] = R(186, y, 66, kRowH);
    }
    Add(W_ENABLE,    K_TOGGLE, R(3, 51, 30, kRowH), L"ON");
    Add(W_RESET,     K_BUTTON, R(35, 51, 38, kRowH), L"RESET");
    Add(W_AUTORESET, K_TOGGLE, R(75, 51, 62, kRowH), L"AUTO-RESET");
    rcRate = R(172, 51, 80, kRowH);

    const int rowA = 71, rowB = 87, rowM = 103;
    const int ids[2][5] = { { W_A_SET, W_A_BB, W_A_B, W_A_F, W_A_FF },
                            { W_B_SET, W_B_BB, W_B_B, W_B_F, W_B_FF } };
    for (int k = 0; k < 2; k++) {
        int y = k ? rowB : rowA;
        Add(ids[k][0], K_BUTTON, R(68, y, 24, kRowH), L"SET");
        Add(ids[k][1], K_BUTTON, R(94, y, 15, kRowH), L"\u00AB", true);
        Add(ids[k][2], K_BUTTON, R(111, y, 13, kRowH), L"<", true);
        Add(ids[k][3], K_BUTTON, R(126, y, 13, kRowH), L">", true);
        Add(ids[k][4], K_BUTTON, R(141, y, 15, kRowH), L"\u00BB", true);
    }
    rcA = R(14, rowA, 52, kRowH);
    rcB = R(14, rowB, 52, kRowH);
    Add(W_START, K_BUTTON, R(158, rowA, 44, 29), L"GO TO\nSTART");
    Add(W_LOOP,  K_TOGGLE, R(204, rowA, 48, 29), L"LOOP");

    Add(W_M_BB, K_BUTTON, R(94, rowM, 15, kRowH), L"\u00AB", true);
    Add(W_M_B,  K_BUTTON, R(111, rowM, 13, kRowH), L"<", true);
    Add(W_M_F,  K_BUTTON, R(126, rowM, 13, kRowH), L">", true);
    Add(W_M_FF, K_BUTTON, R(141, rowM, 15, kRowH), L"\u00BB", true);
    rcLen = R(158, rowM, 94, kRowH);

}

// ------------------------------------------------------------------ helpers
LRESULT Wa(WPARAM w, LPARAM ipc) { return SendMessageW(g_hwndWinamp, WM_WA_IPC, w, ipc); }
int  PlayState() { return (int)Wa(0, IPC_ISPLAYING); }   // 1 play, 3 pause, 0 stop

void FormatTime(wchar_t* buf, size_t n, int ms) {
    if (ms < 0) { swprintf(buf, n, L"-:--.--"); return; }
    swprintf(buf, n, L"%d:%02d.%02d", ms / 60000, (ms / 1000) % 60, (ms / 10) % 100);
}

bool IsInModule(const void* p) {
    HMODULE h = nullptr;
    if (!p || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCWSTR)p, &h)) return false;
    return h != nullptr;
}

bool GetModuleBaseNameA_(HMODULE m, char (&out)[MAX_PATH]) {
    char path[MAX_PATH];
    if (!GetModuleFileNameA(m, path, MAX_PATH)) return false;
    const char* slash = strrchr(path, '\\');
    strcpy_s(out, slash ? slash + 1 : path);
    return true;
}

// Does the In_Module's extension list ("MP3;MP2\0desc\0OGG\0desc\0\0") cover ext?
bool HandlesExt(const In_Module* in, const char* ext) {
    const char* p = in->FileExtensions;
    if (!p || !IsInModule(p)) return false;
    for (int guard = 0; *p && guard < 200; guard++) {
        std::string list = p;                       // "MP3;MP2;MP1"
        p += list.size() + 1;
        size_t start = 0;
        while (start <= list.size()) {
            size_t end = list.find(';', start);
            if (end == std::string::npos) end = list.size();
            if (_stricmp(list.substr(start, end - start).c_str(), ext) == 0) return true;
            start = end + 1;
        }
        if (!*p) break;
        p += strlen(p) + 1;                         // skip description
    }
    return false;
}

// Input plug-in that plays `fn`: scan Winamp's loaded in_*.dll modules.
In_Module* FindInputPlugin(const char* fn) {
    static std::string cachedFn;
    static In_Module* cached = nullptr;
    if (cached && cachedFn == fn) return cached;
    const char* dot = strrchr(fn, '.');
    const char* ext = dot ? dot + 1 : "";
    In_Module* fallback = nullptr;
    HMODULE mods[512]; DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) return nullptr;
    for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < 512; i++) {
        char name[MAX_PATH];
        if (!GetModuleBaseNameA_(mods[i], name)) continue;
        if (_strnicmp(name, "in_", 3) != 0) continue;
        typedef In_Module* (*GetIn)();
        GetIn get = (GetIn)GetProcAddress(mods[i], "winampGetInModule2");
        if (!get) continue;
        In_Module* in = get();
        if (!in || !in->outMod) continue;
        if (HandlesExt(in, ext)) { cached = in; cachedFn = fn; return in; }
        if (!fallback) fallback = in;
    }
    return fallback;   // any plug-in will do: they all share Winamp's output plug-in
}

// The output plug-in Winamp is currently feeding, reached through the
// input plug-in that is playing the current file.
Out_Module* CurrentOutput() {
    int pos = (int)Wa(0, IPC_GETLISTPOS);
    const char* fn = (const char*)Wa(pos, IPC_GETPLAYLISTFILE);
    static DWORD lastLog = 0;
    bool log = GetTickCount() - lastLog > 2000;
    if (log) lastLog = GetTickCount();
    if (!fn || !*fn) { if (log) DebugLog("out: no filename"); return nullptr; }
    In_Module* in = FindInputPlugin(fn);
    if (log) DebugLog("out: fn=%s in=%p outMod=%p", fn, in, in ? in->outMod : nullptr);
    if (!in || !IsInModule(in->outMod)) return nullptr;
    Out_Module* om = in->outMod;
    if (!IsInModule((void*)om->GetOutputTime) || !IsInModule((void*)om->GetWrittenTime)) return nullptr;
    return om;
}

// Track position you are actually hearing, in ms. Winamp's own position
// assumes 1:1 playback; correct it for the audio we stretched/compressed.
int HeardPositionMs() {
    int reported = (int)Wa(0, IPC_GETOUTPUTTIME);
    if (reported < 0 || PlayState() == 0) return -1;
    Out_Module* om = CurrentOutput();
    if (!om) return reported;
    int buffered = om->GetWrittenTime() - om->GetOutputTime();
    if (buffered < 0 || buffered > 30000) return reported;
    double lag;
    if (!DspSourceLagMs(buffered, &lag)) return reported;
    int heard = (int)std::lround(reported + buffered - lag);
    static DWORD lastLog = 0;
    if (GetTickCount() - lastLog > 1000) {
        lastLog = GetTickCount();
        DebugLog("pos: reported=%d buffered=%d lag=%.0f heard=%d tempo=%d", reported, buffered, lag, heard, g_tempo10.load());
    }
    return std::max(0, heard);
}

void JumpTo(int ms) {
    if (PlayState() == 0) return;
    ms = std::max(0, ms);
    g_clearRequest = true;
    Wa(ms, IPC_JUMPTOTIME);
    cooldownUntil = GetTickCount() + 150;
    posMs = ms;
}

void Redraw() { if (hwndPanel) InvalidateRect(hwndPanel, nullptr, FALSE); }

void SetSlider(int idx, int v) {
    SliderDef& s = sliders[idx];
    v = std::min(std::max(v, s.mn), s.mx);
    s.v->store(v);
    Redraw();
}

int LoopLen() { return (loopA >= 0 && loopB > loopA) ? loopB - loopA : 0; }

void NudgeA(int d) { if (loopA < 0) loopA = 0; loopA = std::max(0, loopA + d); if (loopB >= 0 && loopB <= loopA) loopB = -1; }
void NudgeB(int d) { if (loopB < 0) loopB = std::max(0, posMs); loopB = std::max(0, loopB + d); if (loopA >= 0 && loopB <= loopA) loopB = loopA + 40; }

void DoAction(int id) {
    switch (id) {
    case W_TEMPO_DN: case W_PITCH_DN: case W_SPEED_DN: { int i = id - W_TEMPO_DN; SetSlider(i, sliders[i].v->load() - sliders[i].step); break; }
    case W_TEMPO_UP: case W_PITCH_UP: case W_SPEED_UP: { int i = id - W_TEMPO_UP; SetSlider(i, sliders[i].v->load() + sliders[i].step); break; }
    case W_RESET: for (int i = 0; i < 3; i++) SetSlider(i, 0); break;
    case W_AUTORESET: autoReset = !autoReset; break;
    case W_ENABLE: g_enabled = !g_enabled.load(); break;

    case W_A_SET: if (posMs >= 0) { loopA = posMs; if (loopB >= 0 && loopB <= loopA) loopB = -1; } break;
    case W_A_BB: NudgeA(-1000); break;
    case W_A_B:  NudgeA(-40); break;
    case W_A_F:  NudgeA(40); break;
    case W_A_FF: NudgeA(1000); break;
    case W_START: if (loopA >= 0) JumpTo(loopA); break;

    case W_B_SET: if (posMs >= 0 && posMs > loopA) loopB = posMs; break;
    case W_B_BB: NudgeB(-1000); break;
    case W_B_B:  NudgeB(-40); break;
    case W_B_F:  NudgeB(40); break;
    case W_B_FF: NudgeB(1000); break;

    case W_M_BB: case W_M_FF: case W_M_B: case W_M_F: {
        int len = LoopLen();
        if (!len) break;
        int d = (id == W_M_BB) ? -len : (id == W_M_FF) ? len : (id == W_M_B) ? -40 : 40;
        if (loopA + d < 0) d = -loopA;
        loopA += d; loopB += d;
        if (loopOn && (id == W_M_BB || id == W_M_FF)) JumpTo(loopA);
        break;
    }

    case W_LOOP:
        if (!loopOn && LoopLen() == 0) { MessageBeep(MB_ICONWARNING); break; }
        loopOn = !loopOn;
        break;
    }
    Redraw();
}

// ------------------------------------------------------------------ painting
void Fill(HDC dc, const RECT& r, COLORREF c) {
    SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);
}

void Bevel(HDC dc, const RECT& r, COLORREF tl, COLORREF br) {
    Fill(dc, R(r.left, r.top, r.right - r.left, 1), tl);
    Fill(dc, R(r.left, r.top, 1, r.bottom - r.top), tl);
    Fill(dc, R(r.left, r.bottom - 1, r.right - r.left, 1), br);
    Fill(dc, R(r.right - 1, r.top, 1, r.bottom - r.top), br);
}

void Text(HDC dc, RECT r, const wchar_t* s, COLORREF c, HFONT f, UINT align = DT_CENTER) {
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    if (!wcschr(s, L'\n')) {
        DrawTextW(dc, s, -1, &r, align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }
    // Multi-line: centre the block vertically.
    RECT m = r;
    DrawTextW(dc, s, -1, &m, align | DT_NOPREFIX | DT_CALCRECT);
    r.top += ((r.bottom - r.top) - (m.bottom - m.top)) / 2;
    DrawTextW(dc, s, -1, &r, align | DT_NOPREFIX);
}

void DrawLcd(HDC dc, const RECT& r, const wchar_t* s, bool lit = true, UINT align = DT_RIGHT) {
    Fill(dc, r, cLcdBg);
    Bevel(dc, r, cBtnLo, RGB(90, 92, 110));
    RECT t = r; InflateRect(&t, -3, 0);
    Text(dc, t, s, lit ? cLcdText : cLcdDim, fontLcd, align);
}

void DrawButton(HDC dc, const Widget& w, bool down, bool led) {
    RECT r = w.r;
    Fill(dc, r, down ? RGB(140, 142, 160) : cBtnFace);
    Bevel(dc, r, down ? cBtnLo : cBtnHi, down ? cBtnHi : cBtnLo);
    RECT t = r;
    if (down) OffsetRect(&t, 1, 1);
    if (w.kind == K_TOGGLE) {
        RECT l = R(r.left + 3, (r.top + r.bottom - 5) / 2, 5, 5);
        Fill(dc, l, led ? RGB(0, 255, 0) : RGB(0, 70, 0));
        t.left += 8;
    }
    Text(dc, t, w.text, cBtnText, fontBtn);
}

void DrawSlider(HDC dc, const Widget& w, const SliderDef& s) {
    RECT r = w.r;
    RECT g = R(r.left, r.top + 4, r.right - r.left, 5);
    Fill(dc, g, cGroove);
    Bevel(dc, g, cBtnLo, RGB(90, 92, 110));
    const int span = (r.right - r.left) - 7;
    auto xOf = [&](int v) { return r.left + 3 + (int)std::lround((double)(v - s.mn) * span / (s.mx - s.mn)); };
    int v = s.v->load();
    int x0 = xOf(0), xv = xOf(v);
    if (xv != x0) {
        RECT f = R(std::min(x0, xv), g.top + 1, std::abs(xv - x0), 3);
        Fill(dc, f, dragId == w.id ? cFillHot : cFill);
    }
    Fill(dc, R(x0, r.top + 1, 1, 2), cDim);                  // zero tick
    Fill(dc, R(x0, r.bottom - 3, 1, 2), cDim);
    RECT th = R(xv - 3, r.top, 7, r.bottom - r.top);
    Fill(dc, th, cBtnFace);
    Bevel(dc, th, cBtnHi, cBtnLo);
}

void Paint(HDC dc, const RECT& client) {
    Fill(dc, client, cBg);
    wchar_t buf[64];

    for (int i = 0; i < 3; i++) {
        int y = 3 + i * 16;
        Text(dc, R(3, y, 36, kRowH), sliders[i].label, cLabel, fontUi, DT_LEFT);
        int v = sliders[i].v->load();
        if (i == 1) swprintf(buf, 64, L"%+.1f st", v / 10.0);
        else        swprintf(buf, 64, L"%+.1f%%", v / 10.0);
        DrawLcd(dc, rcSliderVal[i], buf, v != 0 && g_enabled.load());
    }
    if (!g_enabled.load()) {
        DrawLcd(dc, rcRate, L"BYPASSED", false, DT_CENTER);
    } else {
        double rate = (1 + g_tempo10.load() / 1000.0) * (1 + g_speed10.load() / 1000.0);
        rate = std::min(2.0, std::max(0.5, rate));
        swprintf(buf, 64, L"RATE x%.2f", rate);
        DrawLcd(dc, rcRate, buf, std::fabs(rate - 1.0) > 1e-6);
    }

    Fill(dc, R(3, 67, 249, 1), cSep);

    Text(dc, R(3, rcA.top, 10, kRowH), L"A", cLabel, fontUi, DT_LEFT);
    Text(dc, R(3, rcB.top, 10, kRowH), L"B", cLabel, fontUi, DT_LEFT);
    FormatTime(buf, 64, loopA); DrawLcd(dc, rcA, buf, loopA >= 0);
    FormatTime(buf, 64, loopB); DrawLcd(dc, rcB, buf, loopB >= 0);
    Text(dc, R(3, 103, 88, kRowH), L"MOVE A+B", cLabel, fontUi, DT_LEFT);
    {
        wchar_t t[32]; FormatTime(t, 32, LoopLen() ? LoopLen() : -1);
        swprintf(buf, 64, L"LEN %s", t);
        DrawLcd(dc, rcLen, buf, LoopLen() > 0);
    }

    for (const Widget& w : widgets) {
        if (w.kind == K_SLIDER) {
            DrawSlider(dc, w, sliders[w.id - W_TEMPO]);
        } else {
            bool led = (w.id == W_LOOP && loopOn) || (w.id == W_AUTORESET && autoReset) ||
                       (w.id == W_ENABLE && g_enabled.load());
            DrawButton(dc, w, pressedId == w.id && pressedInside, led);
        }
    }
}

// ------------------------------------------------------------------ input
const Widget* HitTest(POINT p) {
    for (const Widget& w : widgets) {
        RECT r = w.r;
        if (w.kind == K_SLIDER) InflateRect(&r, 3, 2);
        if (PtInRect(&r, p)) return &w;
    }
    return nullptr;
}

void SliderFromX(int id, int x) {
    for (const Widget& w : widgets) if (w.id == id) {
        SliderDef& s = sliders[id - W_TEMPO];
        const int span = (w.r.right - w.r.left) - 7;
        double f = (double)(x - (w.r.left + 3)) / span;
        int v = s.mn + (int)std::lround(f * (s.mx - s.mn));
        v = (int)std::lround((double)v / s.step) * s.step;          // snap to step
        if (std::abs(v) <= s.step / 2) v = 0;
        SetSlider(id - W_TEMPO, v);
        return;
    }
}

// Client pixels -> 1x design coordinates.
POINT Map(POINT p) { p.x = p.x / scale - OffsetX(); p.y = p.y / scale; return p; }
POINT Pt(LPARAM l) { POINT p = { (short)LOWORD(l), (short)HIWORD(l) }; return Map(p); }

// ------------------------------------------------------------------ polling
// Follow Winamp's double-size mode (main window 550 px wide instead of 275).
void UpdateScale(bool force = false) {
    RECT mr;
    if (!GetWindowRect(g_hwndWinamp, &mr)) return;
    int s = (mr.right - mr.left) >= 500 ? 2 : 1;
    if (s == scale && !force) return;
    scale = s;
    if (hwndFrame) SetWindowPos(hwndFrame, nullptr, 0, 0, FrameW(), FrameH(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (hwndPanel) SetWindowPos(hwndPanel, nullptr, kInsetL, kInsetT, FrameW() - kInsetL - kInsetR, FrameH() - kInsetT - kInsetB,
                                SWP_NOZORDER | SWP_NOACTIVATE);
    DebugLog("scale=%d frame=%dx%d", scale, FrameW(), FrameH());
    Redraw();
}

void Poll() {
    UpdateScale();
    const int state = PlayState();

    // Track change: forget loop points (and optionally the sound settings).
    int lp = (int)Wa(0, IPC_GETLISTPOS);
    const wchar_t* fw = (const wchar_t*)Wa(lp, IPC_GETPLAYLISTFILEW);
    std::wstring file = fw ? fw : L"";
    if (lastListPos != -2 && (lp != lastListPos || file != lastFile)) {
        loopOn = false; loopA = loopB = -1;
        if (autoReset) for (int i = 0; i < 3; i++) sliders[i].v->store(0);
        Redraw();
    }
    lastListPos = lp; lastFile = file;

    int prev = posMs;
    if (GetTickCount() >= cooldownUntil) posMs = HeardPositionMs();
    if (state == 0) posMs = -1;

    if (loopOn && state == 1 && GetTickCount() >= cooldownUntil &&
        LoopLen() > 0 && posMs >= loopB) {
        DebugLog("loop: pos=%d B=%d -> A=%d", posMs, loopB, loopA);
        JumpTo(loopA);
    }
    if (posMs / 10 != prev / 10) Redraw();
}

LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        SetTimer(h, TIMER_POLL, 15, nullptr);
        return 0;
    case WM_TIMER:
        if (w == TIMER_POLL) Poll();
        else if (w == TIMER_REPEAT) {
            if (pressedId && pressedInside) DoAction(pressedId);
            SetTimer(h, TIMER_REPEAT, 60, nullptr);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        // Draw at 1x, then pixel-double like Winamp's double-size mode.
        RECT rc; GetClientRect(h, &rc);
        const int lw = (rc.right + scale - 1) / scale, lh = (rc.bottom + scale - 1) / scale;
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, lw, lh);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        HGDIOBJ oldFont = SelectObject(mem, fontUi);
        RECT all = { 0, 0, lw, lh };
        Fill(mem, all, cBg);
        SetViewportOrgEx(mem, OffsetX(), 0, nullptr);
        RECT design = { 0, 0, kDesignW, kDesignH };
        Paint(mem, design);
        SetViewportOrgEx(mem, 0, 0, nullptr);
        if (scale == 1) BitBlt(dc, 0, 0, lw, lh, mem, 0, 0, SRCCOPY);
        else StretchBlt(dc, 0, 0, lw * scale, lh * scale, mem, 0, 0, lw, lh, SRCCOPY);
        SelectObject(mem, oldFont);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        const Widget* wd = HitTest(Pt(l));
        if (!wd) return 0;
        if (wd->kind == K_SLIDER) {
            if (m == WM_LBUTTONDBLCLK) { SetSlider(wd->id - W_TEMPO, 0); return 0; }
            dragId = wd->id;
            SetCapture(h);
            SliderFromX(dragId, Pt(l).x);
        } else {
            pressedId = wd->id;
            pressedInside = true;
            SetCapture(h);
            if (wd->repeat) { DoAction(wd->id); SetTimer(h, TIMER_REPEAT, 400, nullptr); }
            Redraw();
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (dragId) SliderFromX(dragId, Pt(l).x);
        else if (pressedId) {
            const Widget* wd = HitTest(Pt(l));
            bool inside = wd && wd->id == pressedId;
            if (inside != pressedInside) { pressedInside = inside; Redraw(); }
        }
        return 0;
    case WM_LBUTTONUP:
        if (pressedId) {
            int id = pressedId;
            bool fire = pressedInside;
            bool rep = false;
            for (const Widget& wd : widgets) if (wd.id == id) rep = wd.repeat;
            pressedId = 0;
            KillTimer(h, TIMER_REPEAT);
            if (fire && !rep) DoAction(id);
        }
        dragId = 0;
        if (GetCapture() == h) ReleaseCapture();
        Redraw();
        return 0;
    case WM_CAPTURECHANGED:
        if ((HWND)l != h) { pressedId = 0; dragId = 0; KillTimer(h, TIMER_REPEAT); Redraw(); }
        return 0;
    case WM_RBUTTONUP: {
        const Widget* wd = HitTest(Pt(l));
        if (wd && wd->kind == K_SLIDER) SetSlider(wd->id - W_TEMPO, 0);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p = { (short)LOWORD(l), (short)HIWORD(l) };
        ScreenToClient(h, &p);
        const Widget* wd = HitTest(Map(p));
        if (wd && wd->kind == K_SLIDER) {
            int i = wd->id - W_TEMPO;
            int step = (GET_KEYSTATE_WPARAM(w) & MK_SHIFT) ? 1 : sliders[i].step;
            int notches = GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA;
            SetSlider(i, sliders[i].v->load() + notches * step);
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        KillTimer(h, TIMER_POLL);
        KillTimer(h, TIMER_REPEAT);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ------------------------------------------------------------------ settings
int IniInt(const wchar_t* key, int def) { return (int)GetPrivateProfileIntW(L"Music Practice", key, def, iniPath.c_str()); }
void IniPut(const wchar_t* key, int v) {
    wchar_t b[32]; swprintf(b, 32, L"%d", v);
    WritePrivateProfileStringW(L"Music Practice", key, b, iniPath.c_str());
}

void LocateIni() {
    const char* dir = (const char*)Wa(0, IPC_GETINIDIRECTORY);
    wchar_t wdir[MAX_PATH] = L"";
    if (dir && *dir) MultiByteToWideChar(CP_ACP, 0, dir, -1, wdir, MAX_PATH);
    else GetEnvironmentVariableW(L"APPDATA", wdir, MAX_PATH), wcscat_s(wdir, L"\\Winamp");
    iniPath = std::wstring(wdir) + L"\\Plugins";
    CreateDirectoryW(iniPath.c_str(), nullptr);
    iniPath += L"\\musicpractice.ini";
}

bool RectOnSomeMonitor(const RECT& r) {
    RECT title = { r.left + 20, r.top, r.right - 20, r.top + 14 };   // need the title bar reachable
    HMONITOR m = MonitorFromRect(&title, MONITOR_DEFAULTTONULL);
    if (!m) return false;
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(m, &mi);
    RECT x;
    return IntersectRect(&x, &title, &mi.rcWork) && (x.right - x.left) >= 40;
}

RECT DefaultRect() {
    // Below whichever of Winamp's main window / EQ sits lowest.
    RECT main = { 0, 0, FrameW(), 116 };
    GetWindowRect(g_hwndWinamp, &main);
    int bottom = main.bottom;
    HWND eq = nullptr;
    while ((eq = FindWindowExW(nullptr, eq, L"Winamp EQ", nullptr)) != nullptr)
        if (GetWindow(eq, GW_OWNER) == g_hwndWinamp) break;
    RECT er;
    if (eq && IsWindowVisible(eq) && GetWindowRect(eq, &er) && std::abs(er.left - main.left) < 30) bottom = std::max(bottom, (int)er.bottom);
    RECT r = { main.left, bottom, main.left + FrameW(), bottom + FrameH() };
    return r;
}

// ------------------------------------------------------------------ Winamp menu
LRESULT CALLBACK WinampSubclass(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if ((m == WM_COMMAND || m == WM_SYSCOMMAND) && LOWORD(w) == kMenuId) { UiToggle(); return 0; }
    if (m == WM_INITMENUPOPUP) {
        LRESULT r = DefSubclassProc(h, m, w, l);
        CheckMenuItem((HMENU)w, kMenuId, MF_BYCOMMAND | (hwndFrame && IsWindowVisible(hwndFrame) ? MF_CHECKED : MF_UNCHECKED));
        return r;
    }
    return DefSubclassProc(h, m, w, l);
}

void AddMenu() {
    HMENU menu = (HMENU)Wa(0, IPC_GET_HMENU);
    if (!menu || !IsMenu(menu)) return;
    int count = GetMenuItemCount(menu), pos = -1;
    for (int i = 0; i < count; i++) if (GetMenuItemID(menu, i) == WINAMP_OPTIONS_EQ) { pos = i + 1; break; }
    menuAdded = InsertMenuW(menu, pos < 0 ? count : pos, MF_BYPOSITION | MF_STRING, kMenuId, L"Music Practice") != 0;
}

void RemoveMenu() {
    if (!menuAdded) return;
    HMENU menu = (HMENU)Wa(0, IPC_GET_HMENU);
    if (menu && IsMenu(menu)) DeleteMenu(menu, kMenuId, MF_BYCOMMAND);
    menuAdded = false;
}

} // namespace

// ------------------------------------------------------------------ public
void UiInit() {
    if (hwndFrame) return;
    LocateIni();
    g_tempo10 = std::min(1000, std::max(-500, IniInt(L"tempo", 0)));
    g_pitch10 = std::min(120,  std::max(-120, IniInt(L"pitch", 0)));
    g_speed10 = std::min(1000, std::max(-500, IniInt(L"speed", 0)));
    autoReset = IniInt(L"autoreset", 0) != 0;
    g_enabled = IniInt(L"enabled", 1) != 0;

    WNDCLASSW wc = {};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kPanelClass;
    RegisterClassW(&wc);

    fontUi     = CreateFontW(-9,  0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, L"Tahoma");
    fontLcd    = CreateFontW(-10, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, FIXED_PITCH, L"Lucida Console");
    fontBtn    = CreateFontW(-8,  0, 0, 0, FW_BOLD,   0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, L"Tahoma");
    BuildLayout();

    RECT r;
    r.left = IniInt(L"x", INT_MIN);
    r.top = IniInt(L"y", INT_MIN);
    r.right = r.left + FrameW();
    r.bottom = r.top + FrameH();
    if (r.left == INT_MIN || r.top == INT_MIN || !RectOnSomeMonitor(r)) r = DefaultRect();

    ZeroMemory(&ews, sizeof(ews));
    ews.flags = EMBED_FLAGS_NORESIZE;
    SET_EMBED_GUID(&ews, kEmbedGuid);
    ews.r = r;
    hwndFrame = (HWND)Wa((WPARAM)&ews, IPC_GET_EMBEDIF);
    DebugLog("embed frame=%p rect=%d,%d", hwndFrame, r.left, r.top);
    if (!hwndFrame) return;
    SetWindowTextW(hwndFrame, L"Music Practice");

    RECT fc; GetClientRect(hwndFrame, &fc);
    hwndPanel = CreateWindowExW(0, kPanelClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                kInsetL, kInsetT, FrameW() - kInsetL - kInsetR, FrameH() - kInsetT - kInsetB,
                                hwndFrame, nullptr, g_hInst, nullptr);
    DebugLog("panel=%p frame client=%dx%d", hwndPanel, fc.right, fc.bottom);

    // Winamp calls our Quit() while it is still inside a message that passed
    // through WinampSubclass (e.g. WM_CLOSE) and then unloads the DLL, so that
    // call would return into unmapped code. Pin the DLL for the life of the
    // process so the stack can unwind safely.
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (LPCWSTR)&WinampSubclass, &self);
    }
    subclassed = SetWindowSubclass(g_hwndWinamp, WinampSubclass, kSubclassId, 0) != 0;
    DebugLog("init: thread=%lu winampThread=%lu subclassed=%d", GetCurrentThreadId(),
             GetWindowThreadProcessId(g_hwndWinamp, nullptr), subclassed);
    AddMenu();

    if (IniInt(L"visible", 1)) ShowWindow(hwndFrame, SW_SHOWNA);
}

void UiQuit() {
    if (!iniPath.empty()) {
        IniPut(L"tempo", g_tempo10); IniPut(L"pitch", g_pitch10); IniPut(L"speed", g_speed10);
        IniPut(L"autoreset", autoReset ? 1 : 0);
        IniPut(L"enabled", g_enabled.load() ? 1 : 0);
        if (hwndFrame) {
            RECT r; GetWindowRect(hwndFrame, &r);
            IniPut(L"x", r.left); IniPut(L"y", r.top);
            IniPut(L"visible", IsWindowVisible(hwndFrame) ? 1 : 0);
        }
    }
    RemoveMenu();
    if (subclassed) {
        BOOL ok = RemoveWindowSubclass(g_hwndWinamp, WinampSubclass, kSubclassId);
        DebugLog("quit: thread=%lu winampThread=%lu isWindow=%d removed=%d", GetCurrentThreadId(),
                 GetWindowThreadProcessId(g_hwndWinamp, nullptr), IsWindow(g_hwndWinamp), ok);
        subclassed = false;
    }
    if (hwndFrame) { DestroyWindow(hwndFrame); hwndFrame = hwndPanel = nullptr; }
    UnregisterClassW(kPanelClass, g_hInst);
    for (HFONT* f : { &fontUi, &fontBtn, &fontLcd }) if (*f) { DeleteObject(*f); *f = nullptr; }
}

void UiToggle() {
    if (!hwndFrame) return;
    ShowWindow(hwndFrame, IsWindowVisible(hwndFrame) ? SW_HIDE : SW_SHOWNA);
}

} // namespace pl
