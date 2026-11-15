// MusicPractice-Setup.exe: installs / uninstalls dsp_musicpractice.dll into Winamp.
//
//   MusicPractice-Setup.exe              interactive install / uninstall
//   MusicPractice-Setup.exe /dir="C:\Path\To\Winamp"   use a specific Winamp folder
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <string>
#include <vector>


static const wchar_t* kTitle = L"Music Practice Setup";
static const wchar_t* kDll = L"dsp_musicpractice.dll";

static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static std::wstring RegString(HKEY root, const wchar_t* key, const wchar_t* value, REGSAM extra = 0) {
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_READ | extra, &h) != ERROR_SUCCESS) return L"";
    wchar_t buf[MAX_PATH] = L""; DWORD sz = sizeof(buf), type = 0;
    LONG r = RegQueryValueExW(h, value, nullptr, &type, (BYTE*)buf, &sz);
    RegCloseKey(h);
    return (r == ERROR_SUCCESS && type == REG_SZ) ? buf : L"";
}

static std::wstring Trim(std::wstring s) {
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'"' || s.back() == L' ')) s.pop_back();
    while (!s.empty() && (s.front() == L'"' || s.front() == L' ')) s.erase(0, 1);
    return s;
}

static std::wstring FindWinamp(const std::wstring& forced) {
    std::vector<std::wstring> c;
    if (!forced.empty()) c.push_back(forced);
    c.push_back(RegString(HKEY_CURRENT_USER, L"Software\\Winamp", nullptr));
    c.push_back(RegString(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Winamp", L"InstallLocation", KEY_WOW64_32KEY));
    wchar_t pf[MAX_PATH];
    if (SHGetSpecialFolderPathW(nullptr, pf, CSIDL_PROGRAM_FILESX86, FALSE)) c.push_back(std::wstring(pf) + L"\\Winamp");
    if (SHGetSpecialFolderPathW(nullptr, pf, CSIDL_PROGRAM_FILES, FALSE)) c.push_back(std::wstring(pf) + L"\\Winamp");
    for (auto& d : c) {
        std::wstring t = Trim(d);
        if (!t.empty() && Exists(t + L"\\winamp.exe")) return t;
    }
    return L"";
}

// Where Winamp keeps winamp.ini: paths.ini "inidir" ("{26}\Winamp" = CSIDL 26)
static std::wstring IniDir(const std::wstring& waDir) {
    wchar_t raw[MAX_PATH] = L"";
    GetPrivateProfileStringW(L"Winamp", L"inidir", L"", raw, MAX_PATH, (waDir + L"\\paths.ini").c_str());
    std::wstring s = raw;
    if (s.empty()) {
        wchar_t ad[MAX_PATH];
        SHGetSpecialFolderPathW(nullptr, ad, CSIDL_APPDATA, FALSE);
        std::wstring def = std::wstring(ad) + L"\\Winamp";
        return Exists(def + L"\\winamp.ini") ? def : waDir;
    }
    if (s[0] == L'{') {
        size_t close = s.find(L'}');
        int csidl = _wtoi(s.substr(1, close - 1).c_str());
        wchar_t base[MAX_PATH] = L"";
        SHGetFolderPathW(nullptr, csidl, nullptr, 0, base);
        s = std::wstring(base) + s.substr(close + 1);
    }
    return Trim(s);
}

static bool WinampRunning(const std::wstring& waDir) {
    std::wstring exe = waDir + L"\\winamp.exe";
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"winamp.exe") != 0) continue;
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!p) { found = true; break; }
        wchar_t path[MAX_PATH]; DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(p, 0, path, &n) && _wcsicmp(path, exe.c_str()) == 0) found = true;
        CloseHandle(p);
    }
    CloseHandle(snap);
    return found;
}

static int Ask(const wchar_t* main, const std::wstring& content, const TASKDIALOG_BUTTON* btns, int n,
               const wchar_t* verify = nullptr, BOOL* verified = nullptr, PCWSTR icon = TD_INFORMATION_ICON) {
    TASKDIALOGCONFIG tc = { sizeof(tc) };
    tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | (verified && *verified ? TDF_VERIFICATION_FLAG_CHECKED : 0);
    tc.pszWindowTitle = kTitle;
    tc.pszMainIcon = icon;
    tc.pszMainInstruction = main;
    tc.pszContent = content.c_str();
    tc.pButtons = btns;
    tc.cButtons = n;
    tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    tc.pszVerificationText = verify;
    int pressed = IDCANCEL;
    TaskDialogIndirect(&tc, &pressed, nullptr, verified);
    return pressed;
}

static void Info(const wchar_t* main, const std::wstring& content, PCWSTR icon = TD_INFORMATION_ICON) {
    TaskDialog(nullptr, nullptr, kTitle, main, content.c_str(), TDCBF_OK_BUTTON, icon, nullptr);
}

static bool WriteEmbeddedDll(const std::wstring& path) {
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
    const void* data = g ? LockResource(g) : nullptr;
    DWORD size = r ? SizeofResource(nullptr, r) : 0;
    if (!data || !size) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    BOOL ok = WriteFile(f, data, size, &w, nullptr) && w == size;
    CloseHandle(f);
    return ok != FALSE;
}

static std::wstring IniGet(const std::wstring& ini, const wchar_t* key) {
    wchar_t b[MAX_PATH] = L"";
    GetPrivateProfileStringW(L"Winamp", key, L"", b, MAX_PATH, ini.c_str());
    return b;
}

static std::wstring LastError() {
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, GetLastError(), 0, (LPWSTR)&msg, 0, nullptr);
    std::wstring s = msg ? msg : L"unknown error";
    LocalFree(msg);
    return s;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    std::wstring forced;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++)
        if (_wcsnicmp(argv[i], L"/dir=", 5) == 0) forced = argv[i] + 5;
    LocalFree(argv);

    std::wstring wa = FindWinamp(forced);
    if (wa.empty()) {
        Info(L"Winamp wasn't found", L"Music Practice needs Winamp 5.x installed. If it's in an unusual folder, run:\n\nMusicPractice-Setup.exe /dir=\"C:\\Path\\To\\Winamp\"", TD_ERROR_ICON);
        return 1;
    }
    const std::wstring plugins = wa + L"\\Plugins";
    const std::wstring target = plugins + L"\\" + kDll;
    const std::wstring ini = IniDir(wa) + L"\\winamp.ini";
    const std::wstring disabledDir = plugins + L"\\Disabled";
    const std::wstring loopMaster = plugins + L"\\gen_Loop.dll";
    const std::wstring loopMasterOff = disabledDir + L"\\gen_Loop.dll";

    while (WinampRunning(wa)) {
        TASKDIALOG_BUTTON b[] = { { 100, L"I've closed Winamp, continue" } };
        if (Ask(L"Please close Winamp first", L"Winamp is running. Close it (right-click > Exit) so its plug-ins can be updated.", b, 1,
                nullptr, nullptr, TD_WARNING_ICON) != 100) return 0;
    }

    const bool installed = Exists(target);
    int action = 1;   // 1 install, 2 uninstall
    if (installed) {
        TASKDIALOG_BUTTON b[] = { { 1, L"Reinstall / update Music Practice" }, { 2, L"Uninstall Music Practice" } };
        action = Ask(L"Music Practice is already installed", L"Winamp folder: " + wa, b, 2);
        if (action != 1 && action != 2) return 0;
    }

    if (action == 2) {
        if (!DeleteFileW(target.c_str())) { Info(L"Couldn't remove Music Practice", target + L"\n\n" + LastError(), TD_ERROR_ICON); return 1; }
        std::wstring note = L"Music Practice was removed.";
        if (_wcsicmp(IniGet(ini, L"dspplugin_name").c_str(), kDll) == 0) {
            bool pm = Exists(plugins + L"\\dsp_pacemaker.dll");
            WritePrivateProfileStringW(L"Winamp", L"dspplugin_name", pm ? L"dsp_pacemaker.dll" : L"", ini.c_str());
            WritePrivateProfileStringW(L"Winamp", L"dspplugin_num", L"0", ini.c_str());
            if (pm) note += L"\nPaceMaker is the active DSP plug-in again.";
        }
        if (Exists(loopMasterOff) && !Exists(loopMaster) && MoveFileW(loopMasterOff.c_str(), loopMaster.c_str()))
            note += L"\nLoop Master was turned back on.";
        Info(L"Uninstalled", note);
        return 0;
    }

    // ---- install
    BOOL makeActive = TRUE;
    {
        TASKDIALOG_BUTTON b[] = { { 1, L"Install" } };
        std::wstring content =
            L"Music Practice adds tempo, pitch and speed control plus an A-B loop in a panel that docks to Winamp like the equalizer.\n\n"
            L"Winamp folder: " + wa;
        if (Ask(L"Install Music Practice for Winamp?", content, b, 1,
                L"Make Music Practice the active DSP plug-in (replaces PaceMaker)", &makeActive) != 1) return 0;
    }

    if (!WriteEmbeddedDll(target)) { Info(L"Install failed", L"Couldn't write " + target + L"\n\n" + LastError(), TD_ERROR_ICON); return 1; }

    std::wstring note = L"Installed to " + target + L".";
    if (makeActive) {
        if (Exists(ini)) {
            WritePrivateProfileStringW(L"Winamp", L"dspplugin_name", kDll, ini.c_str());
            WritePrivateProfileStringW(L"Winamp", L"dspplugin_num", L"0", ini.c_str());
            note += L"\nMusic Practice is now the active DSP plug-in.";
        } else {
            note += L"\nTo turn it on: Winamp > Preferences (Ctrl+P) > Plug-ins > DSP/Effect > Music Practice.";
        }
    }

    if (Exists(loopMaster)) {
        TASKDIALOG_BUTTON b[] = { { 1, L"Turn off Loop Master" }, { 2, L"Keep Loop Master" } };
        if (Ask(L"Turn off Loop Master?", L"Music Practice includes Loop Master's A-B looping. Loop Master's file will be moved to Plugins\\Disabled so it can be restored later.",
                b, 2) == 1) {
            CreateDirectoryW(disabledDir.c_str(), nullptr);
            if (MoveFileExW(loopMaster.c_str(), loopMasterOff.c_str(), MOVEFILE_REPLACE_EXISTING)) note += L"\nLoop Master was turned off.";
            else note += L"\nCouldn't move Loop Master: " + LastError();
        }
    }

    note += L"\n\nStart Winamp. Show or hide the panel from Winamp's main menu (click the Winamp logo, or right-click the title bar) > Music Practice.";
    Info(L"Music Practice is installed", note);
    return 0;
}
