#include "Trayy.h"
#include <vector>
#include <string>
#include <fstream>
#include <shellapi.h>
#include <thread>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <psapi.h>
#include <algorithm>
#include <codecvt>
#include <locale>
#include <sstream>
#include <tlhelp32.h>

// Global variables
UINT WM_TASKBAR_CREATED;
HWINEVENTHOOK hEventHook;
HWINEVENTHOOK hLocationHook;
HINSTANCE hInstance;
HMODULE hLib;
HWND hwndBase;
HWND hwndMain;
HWND hwndItems[MAXTRAYITEMS];
HWND hwndForMenu;
std::unordered_set<std::wstring> appNames;
std::unordered_set<std::wstring> specialAppNames;
bool HOOKBOTH = true;
bool NOTASKBAR = false;
bool updateAvailable = false;
std::unordered_map<HWND, HWND> g_OverlayWindows;

std::unordered_set<std::wstring> ExcludedNames = { L"Cancel", L"File Upload", L"ApplicationFrameHost.exe", L"MSCTFIME UI", L"Default IME", L"EVR Fullscreen Window" };
std::unordered_set<std::wstring> ExcludedProcesses = { L"Chrom Legacy Window", L"Explorer.EXE", L"SearchHost.exe", L"svchost.exe", L"taskhostw.exe", L"OneDrive.exe", L"TextInputHost.exe", L"SystemSettings.exe", L"RuntimeBroker.exe", L"SearchUI.exe", L"ShellExperienceHost.exe", L"msedgewebview2.exe", L"pwahelper.exe", L"conhost.exe", L"VCTIP.EXE", L"GameBarFTServer.exe" };
std::unordered_set<std::wstring> UseWindowName = { L"chrome.exe", L"firefox.exe", L"opera.exe", L"msedge.exe", L"iexplore.exe", L"brave.exe", L"vivaldi.exe", L"chromium.exe" };

// Shared memory variables
HANDLE hSharedMemory = NULL;
SpecialAppsSharedData* pSharedData = NULL;

// Shared memory functions
BOOL InitializeSharedMemory() {
    // Create shared memory
    hSharedMemory = CreateFileMapping(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        SHARED_MEM_SIZE,
        SHARED_MEM_NAME);

    if (hSharedMemory == NULL) {
        return FALSE;
    }

    // Map view of file
    pSharedData = (SpecialAppsSharedData*)MapViewOfFile(
        hSharedMemory,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        SHARED_MEM_SIZE);

    if (pSharedData == NULL) {
        CloseHandle(hSharedMemory);
        hSharedMemory = NULL;
        return FALSE;
    }

    // Initialize
    pSharedData->count = 0;

    return TRUE;
}

void CleanupSharedMemory() {
    if (pSharedData) {
        UnmapViewOfFile(pSharedData);
        pSharedData = NULL;
    }

    if (hSharedMemory) {
        CloseHandle(hSharedMemory);
        hSharedMemory = NULL;
    }
}

BOOL UpdateSpecialAppsList(const std::unordered_set<std::wstring>& specialApps) {
    if (!pSharedData)
        return FALSE;

    // Clear existing data
    ZeroMemory(pSharedData, SHARED_MEM_SIZE);

    // Update count and app names
    int idx = 0;
    for (const auto& app : specialApps) {
        if (idx >= MAX_SPECIAL_APPS) break;
        wcscpy_s(pSharedData->specialApps[idx], MAX_PATH, app.c_str());
        idx++;
    }
    pSharedData->count = idx;

    return TRUE;
}

int FindInTray(HWND hwnd) {
    for (int i = 0; i < MAXTRAYITEMS; i++) {
        if (hwndItems[i] == hwnd) {
            return i;
        }
    }
    return -1;
}

HICON GetWindowIcon(HWND hwnd) {
    HICON icon;
    if (icon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_SMALL, 0)) {
        return icon;
    }
    if (icon = (HICON)SendMessage(hwnd, WM_GETICON, ICON_BIG, 0)) {
        return icon;
    }
    if (icon = (HICON)GetClassLongPtr(hwnd, GCLP_HICONSM)) {
        return icon;
    }
    if (icon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON)) {
        return icon;
    }
    return LoadIcon(NULL, IDI_WINLOGO);
}

bool AddToTray(int i) {
    NOTIFYICONDATA nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = NOTIFYICONDATA_V2_SIZE;
    nid.hWnd = hwndMain;
    nid.uID = (UINT)i;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYCMD;
    nid.hIcon = GetWindowIcon(hwndItems[i]);
    GetWindowText(hwndItems[i], nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]));
    nid.uVersion = NOTIFYICON_VERSION;
    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
        return false;
    }
    if (!Shell_NotifyIcon(NIM_SETVERSION, &nid)) {
        Shell_NotifyIcon(NIM_DELETE, &nid);
        return false;
    }
    return true;
}

static bool AddWindowToTray(HWND hwnd) {
    int i = FindInTray(NULL);
    if (i == -1) {
        return false;
    }
    hwndItems[i] = hwnd;
    return AddToTray(i);
}

void MinimizeWindowToTray(HWND hwnd) {
    // Don't minimize MDI child windows
    if ((UINT)GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_MDICHILD) {
        return;
    }

    // If hwnd is a child window, find parent window
    if ((UINT)GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CHILD) {
        hwnd = GetAncestor(hwnd, GA_ROOT);
    }

    ShowWindow(hwnd, SW_HIDE);

    // Add icon to tray if it's not already there
    if (FindInTray(hwnd) == -1) {
        if (!AddWindowToTray(hwnd)) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            return;
        }
    }
}

std::wstring getProcessName(HWND hwnd) {
    DWORD processId;
    GetWindowThreadProcessId(hwnd, &processId);
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (hProcess == NULL)
        return L"";

    wchar_t processName[MAX_PATH] = L"";
    HMODULE hMod;
    DWORD cbNeeded;

    if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded))
    {
        GetModuleBaseName(hProcess, hMod, processName, sizeof(processName) / sizeof(TCHAR));
    }
    CloseHandle(hProcess);
    return std::wstring(processName);
}

std::wstring GetCleanAppName(const std::wstring& appName) {
    size_t lastSpace = appName.find_last_of(L' ');
    if (lastSpace != std::wstring::npos) {
        std::wstring lastWord = appName.substr(lastSpace + 1);
        if (lastWord.length() > 2 && (lastWord[0] == L'w' || lastWord[0] == L'W')) {
            size_t hPos = std::wstring::npos;
            for (size_t j = 1; j < lastWord.length(); ++j) {
                if (lastWord[j] == L'h' || lastWord[j] == L'H') {
                    hPos = j;
                    break;
                }
            }
            if (hPos != std::wstring::npos && hPos > 1 && hPos < lastWord.length() - 1) {
                try {
                    (void)std::stoi(lastWord.substr(1, hPos - 1));
                    (void)std::stoi(lastWord.substr(hPos + 1));
                    return appName.substr(0, lastSpace);
                }
                catch (...) {
                    OutputDebugString(L"Not a dimension string in GetCleanAppName\n");
                }
            }
        }
    }
    return appName;
}

bool MatchesAppName(HWND hwnd) {
    std::wstring originalProcessName = getProcessName(hwnd);
    std::wstring processName = originalProcessName;
    wchar_t windowName[256];
    GetWindowText(hwnd, windowName, 256);
    std::wstring windowNameStr(windowName);

    bool switchedProcessNametoWindowName = false;
    if (UseWindowName.find(originalProcessName) != UseWindowName.end()) {
        processName = windowNameStr;
        switchedProcessNametoWindowName = true;
    }

    for (auto appName : appNames) {
        if (appName.empty()) {
            continue;
        }

        appName = GetCleanAppName(appName);

        std::wistringstream iss(appName);
        std::wstring word;
        std::vector<std::wstring> words;

        while (iss >> word) {
            words.push_back(word);
        }

        if (words.empty()) {
            continue;
        }

        bool firstWordHasExtension = words[0].find(L'.') != std::wstring::npos;
        if (firstWordHasExtension) {
            if (switchedProcessNametoWindowName) {
                continue;
            }

            std::wstring processPart = words[0];
            std::wstring titlePart = words.size() > 1 ? appName.substr(appName.find(L' ') + 1) : L"";

            // Special case for Thunderbird
            if (processPart == L"thunderbird.exe") {
                if (titlePart.empty()) {
                    titlePart = L" - Mozilla Thunderbird";
                }
            }

            if (originalProcessName == processPart) {
                if (windowNameStr.find(titlePart) != std::wstring::npos) {
                    return true;
                }
            }
        }
        else {
            if (switchedProcessNametoWindowName) {
                if (processName.find(appName) != std::wstring::npos) {
                    return true;
                }
            }
        }
    }

    if (originalProcessName == NAME L".exe" && windowNameStr == NAME) {
        return true;
    }

    return false;
}

bool RemoveWindowFromTray(HWND hwnd) {
    int i = FindInTray(hwnd);
    if (i == -1) {
        return false;
    }
    NOTIFYICONDATA nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = NOTIFYICONDATA_V2_SIZE;
    nid.hWnd = hwndMain;
    nid.uID = (UINT)i;
    if (!Shell_NotifyIcon(NIM_DELETE, &nid)) {
        return false;
    }
    hwndItems[i] = NULL;
    return true;
}

void RefreshWindowInTray(HWND hwnd) {
    int i = FindInTray(hwnd);
    if (i == -1) {
        return;
    }

    if (!IsWindow(hwnd)) {
        RemoveWindowFromTray(hwnd);
        return;
    }

    if (!MatchesAppName(hwnd) && IsWindowVisible(hwnd)) {
        RemoveWindowFromTray(hwnd);
        return;
    }

    NOTIFYICONDATA nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = NOTIFYICONDATA_V2_SIZE;
    nid.hWnd = hwndMain;
    nid.uID = (UINT)i;
    nid.uFlags = NIF_TIP | NIF_ICON;
    GetWindowText(hwnd, nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]));
    nid.hIcon = GetWindowIcon(hwnd);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void ReinstateTaskbarState() {
    for (int i = 0; i < MAXTRAYITEMS; i++) {
        if (hwndItems[i] != NULL && hwndItems[i] != hwndMain) {
            if (!IsWindowVisible(hwndItems[i])) {
                continue;
            }
            if (NOTASKBAR) {
                SetWindowLongPtr(hwndItems[i], GWLP_HWNDPARENT, (LONG_PTR)hwndBase);
            }
            else {
                SetWindowLongPtr(hwndItems[i], GWLP_HWNDPARENT, 0);
            }
            // force taskbar refresh
            SetWindowPos(hwndItems[i], HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        }
    }
}

void RefreshTray() {
    for (int i = 0; i < MAXTRAYITEMS; i++) {
        if (hwndItems[i] != NULL && hwndItems[i] != hwndMain) {
            RefreshWindowInTray(hwndItems[i]);
        }
    }
}

void RestoreWindowFromTray(HWND hwnd) {
    wchar_t windowName[256];
    GetWindowText(hwnd, windowName, 256);
    if (hwnd == hwndMain) {
        // Mirror logic from CreateMainWindow()
        RECT rect;
        HWND taskbar = FindWindow(L"Shell_traywnd", NULL);
        if (!taskbar) {
            // Fallback to primary monitor screen if taskbar isn't found
            rect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        }
        else {
            GetWindowRect(taskbar, &rect);
        }

        // Get current window dimensions
        RECT windowRect;
        GetWindowRect(hwnd, &windowRect);
        int windowWidth = windowRect.right - windowRect.left;
        int windowHeight = windowRect.bottom - windowRect.top;

        // Calculate position exactly as CreateMainWindow does
        int x = rect.right - windowWidth - DESKTOP_PADDING;
        int y;
        if (rect.top == 0) {
            // Taskbar at top
            y = rect.bottom + DESKTOP_PADDING;
        }
        else {
            // Taskbar at bottom
            y = rect.top - windowHeight - DESKTOP_PADDING;
        }

        // Use ShowWindow to ensure WM_SHOWWINDOW is sent and timer is started
        ShowWindow(hwnd, SW_SHOW);
        // Explicitly position and show the window
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, 0, 0,
            SWP_NOSIZE | SWP_SHOWWINDOW);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(hwnd);
        return;
    }

    if (NOTASKBAR) {
        SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, (LONG_PTR)hwndBase);
    }
    else {
        SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0);
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);

    if (IsWindow(hwnd)) {
        RefreshWindowInTray(hwnd);
    }

    // If app is no longer in app list
    if (MatchesAppName(hwnd) == false) {
        SetWindowLongPtr(hwnd, GWLP_HWNDPARENT, 0);
    }
}

void RestoreWindowFromTray(std::wstring appName) {
    for (int i = 0; i < MAXTRAYITEMS; i++) {
        if (hwndItems[i] != NULL && hwndItems[i] != hwndMain) {
            std::wstring processName = getProcessName(hwndItems[i]);
            wchar_t windowName[256];
            GetWindowText(hwndItems[i], windowName, 256);
            if (UseWindowName.find(processName) != UseWindowName.end()) {
                processName = windowName;
            }
            std::wstring nameWithoutExt = processName;
            size_t extPos = nameWithoutExt.rfind(L'.');
            if (extPos != std::wstring::npos) {
                processName = nameWithoutExt.substr(0, extPos);
            }

            std::wstring appNameClean = GetCleanAppName(appName);

            if (processName == appNameClean) {
                RestoreWindowFromTray(hwndItems[i]);
                return;
            }
        }
    }
}

void CloseWindowFromTray(HWND hwnd) {
    PostMessage(hwnd, WM_CLOSE, 0, 0);

    Sleep(180);

    if (!IsWindow(hwnd)) {
        RemoveWindowFromTray(hwnd);
    }
    else {
        ShowWindow(hwnd, SW_HIDE);
        SetForegroundWindow(hwnd);
    }
}

bool appCheck(HWND hwnd, bool RClick) {
    wchar_t windowName[256];
    GetWindowText(hwnd, windowName, 256);

    if (wcslen(windowName) == 0) {
        return false;
    }

    // Excluded window names
    std::wstring windowNameStr(windowName);
    if (ExcludedNames.find(windowNameStr) != ExcludedNames.end()) {
        return false;
    }

    std::wstring processName = getProcessName(hwnd);
    if (processName.empty()) {
        return false;
    }

    // Excluded processes
    if (ExcludedProcesses.find(processName) != ExcludedProcesses.end()) {
        return false;
    }

    if (hwnd == hwndMain) {
        return false;
    }

    if (RClick) {
        return true;
    }

    return MatchesAppName(hwnd);
}

void MinimizeAll() {
    HWND hwnd = GetTopWindow(NULL);
    while (hwnd) {
        if (appCheck(hwnd)) {
            MinimizeWindowToTray(hwnd);
        }
        hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
    }
}

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));
    if (strcmp(className, "TrayNotifyWnd") == 0) {
        EnumChildWindows(hwnd, EnumChildProc, lParam);
    }
    else if (strcmp(className, "SysPager") == 0) {
        EnumChildWindows(hwnd, EnumChildProc, lParam);
    }
    else if (strcmp(className, "ToolbarWindow32") == 0) {
        RECT r;
        GetClientRect(hwnd, &r);
        for (LONG x = 0; x < r.right; x += 5) {
            for (LONG y = 0; y < r.bottom; y += 5) {
                LPARAM lParam = MAKELPARAM(x, y);
                SendMessage(hwnd, WM_MOUSEMOVE, 0, lParam);
            }
        }
    }
    return TRUE;
}

bool IsTopWindow(HWND hwnd) {
    HWND hwndTopmost = NULL;
    HWND hwnd_ = GetTopWindow(0);
    wchar_t name_[256];
    GetWindowText(hwnd_, name_, 256);
    while (hwnd_) {
        if (wcslen(name_) > 0 && IsWindowVisible(hwnd_)) {
            hwndTopmost = hwnd_;
            break;
        }
        hwnd_ = GetNextWindow(hwnd_, GW_HWNDNEXT);
        GetWindowText(hwnd_, name_, 256);
    }
    if (hwndTopmost == hwnd) {
        return true;
    }
    return false;
}

void MinimizeAllInBackground() {
    std::thread t([]() {
        if (GetTickCount64() < 300000) {
            Sleep(11000);
        }
        MinimizeAll();
        });
    t.detach();
}

void LoadSettings() {
    appNames.clear();
    specialAppNames.clear();
    std::wifstream file(SETTINGS_FILE);
    file.imbue(std::locale(file.getloc(),
        new std::codecvt_utf8<wchar_t, 0x10ffff, std::generate_header>));

    if (!file) {
        std::wofstream file(SETTINGS_FILE, std::ios::out | std::ios::trunc);
        file << L"HOOKBOTH " << (HOOKBOTH ? L"true" : L"false") << std::endl;
        file << L"NOTASKBAR " << (NOTASKBAR ? L"true" : L"false") << std::endl;
    }
    else {
        try {
            std::wstring line;
            std::getline(file, line);
            HOOKBOTH = line.find(L"true") != std::string::npos;
            std::getline(file, line);
            NOTASKBAR = line.find(L"true") != std::string::npos;
            while (std::getline(file, line)) {
                if (!line.empty()) {
                    // If line begins with '*' it's a graphical (special) app; store without the '*'
                    if (line[0] == L'*') {
                        std::wstring baseName = line.substr(1);
                        appNames.insert(baseName);
                        specialAppNames.insert(baseName);
                    }
                    else {
                        appNames.insert(line);
                    }
                }
            }
            file.close();
            UpdateSpecialAppsList(specialAppNames);
        }
        catch (const std::exception&) {
            std::wstring backupFile = SETTINGS_FILE L".bak";
            CopyFile(SETTINGS_FILE, backupFile.c_str(), FALSE);
            std::wofstream file(SETTINGS_FILE, std::ios::out | std::ios::trunc);
            file << L"HOOKBOTH " << (HOOKBOTH ? L"true" : L"false") << std::endl;
            file << L"NOTASKBAR " << (NOTASKBAR ? L"true" : L"false") << std::endl;
        }
    }
    // Notify ImGui that the app list has changed
    MarkAppListDirty();
}

void SaveSettings() {
    std::wofstream file(SETTINGS_FILE, std::ios::out | std::ios::trunc);
    file.imbue(std::locale(file.getloc(),
        new std::codecvt_utf8<wchar_t, 0x10ffff, std::generate_header>));
    file << L"HOOKBOTH " << (HOOKBOTH ? L"true" : L"false") << std::endl;
    file << L"NOTASKBAR " << (NOTASKBAR ? L"true" : L"false") << std::endl;

    for (const auto& appName : appNames) {
        if (specialAppNames.find(appName) != specialAppNames.end()) {
            file << L"*" << appName << std::endl;
        }
        else {
            file << appName << std::endl;
        }
    }
    file.close();
    LoadSettings();
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);

        // Fill with color key (magenta)
        HBRUSH hBgBrush = CreateSolidBrush(RGB(255, 0, 255));
        FillRect(hdc, &rect, hBgBrush);
        DeleteObject(hBgBrush);

        // Draw border using the accent color
        COLORREF accentColor = GetSysColor(13); // COLOR_HIGHLIGHT
        HBRUSH hBrush = CreateSolidBrush(accentColor); // Use accent color
        int thickness = 4;

        // Top
        RECT r = { 0, 0, rect.right, thickness };
        FillRect(hdc, &r, hBrush);
        // Bottom
        r = { 0, rect.bottom - thickness, rect.right, rect.bottom };
        FillRect(hdc, &r, hBrush);
        // Left
        r = { 0, 0, thickness, rect.bottom };
        FillRect(hdc, &r, hBrush);
        // Right
        r = { rect.right - thickness, 0, rect.right, rect.bottom };
        FillRect(hdc, &r, hBrush);

        DeleteObject(hBrush);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void RegisterOverlayClass(HINSTANCE hInstance) {
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = OVERLAY_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    RegisterClassEx(&wc);
}

bool PowerToysAlwaysOnTop() {
    // check if powertoys is running
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{ sizeof(pe) };
    bool isRunning = false;
    for (Process32FirstW(h, &pe); Process32NextW(h, &pe);)
        if (!_wcsicmp(pe.szExeFile, L"PowerToys.exe"))
        {
            CloseHandle(h);
            isRunning = true;
            break;
        }

    if (!isRunning) {
        return false;
    }

    // read powertoys setting.json
    wchar_t localAppData[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) {
        return false;
    }

    std::wstring basePath(localAppData);
    std::wstring settingsPath = basePath + L"\\Microsoft\\PowerToys\\settings.json";
    std::wifstream file(settingsPath);
    if (!file) {
        return false;
    }

    bool enabled = false;
    std::wstring line;
    while (std::getline(file, line)) {
        if (line.find(L"\"AlwaysOnTop\":true") != std::wstring::npos) {
            enabled = true;
            break;
        }
    }
    file.close();

    if (!enabled) {
        return false;
    }

    std::wstring aotSettingsPath = basePath + L"\\Microsoft\\PowerToys\\AlwaysOnTop\\settings.json";
    file.open(aotSettingsPath);
    if (!file) {
        return false;
    }

    // emulate hotkeys for AOT
    int code = 0;
    while (std::getline(file, line)) {
        if (line.find(L"\"win\":true") != std::wstring::npos) keybd_event(VK_LWIN, 0, 0, 0);;
        if (line.find(L"\"ctrl\":true") != std::wstring::npos) keybd_event(VK_CONTROL, 0, 0, 0);
        if (line.find(L"\"alt\":true") != std::wstring::npos) keybd_event(VK_MENU, 0, 0, 0);
        if (line.find(L"\"shift\":true") != std::wstring::npos) keybd_event(VK_SHIFT, 0, 0, 0);

        size_t codePos = line.find(L"\"code\":");
        if (codePos != std::wstring::npos) {
            try {
                size_t start = codePos + 7;
                size_t end = line.find_first_of(L",}", start);
                std::wstring codeStr = line.substr(start, end - start);
                code = std::stoi(codeStr);
            }
            catch (...) {
                continue;
            }
        }

    }
    file.close();

    keybd_event((BYTE)code, 0, 0, 0);

    // release keys
    keybd_event((BYTE)code, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0);

    return true;
}

void AddOverlay(HWND target) {
    if (g_OverlayWindows.find(target) != g_OverlayWindows.end()) return;

    RECT rect;
    GetWindowRect(target, &rect);

    HWND overlay = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS_NAME,
        L"",
        WS_POPUP | WS_VISIBLE,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL
    );

    SetLayeredWindowAttributes(overlay, RGB(255, 0, 255), 0, LWA_COLORKEY);

    g_OverlayWindows[target] = overlay;
}

void RemoveOverlay(HWND target) {
    auto it = g_OverlayWindows.find(target);
    if (it != g_OverlayWindows.end()) {
        DestroyWindow(it->second);
        g_OverlayWindows.erase(it);
    }
}

void UpdateOverlayPosition(HWND target) {
    auto it = g_OverlayWindows.find(target);
    if (it == g_OverlayWindows.end()) return;

    HWND overlay = it->second;
    if (!IsWindow(target)) return;

    if (!IsWindowVisible(target) || IsIconic(target)) {
        ShowWindow(overlay, SW_HIDE);
    }
    else {
        ShowWindow(overlay, SW_SHOWNA);
        RECT rect;
        GetWindowRect(target, &rect);
        SetWindowPos(overlay, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

void UpdateOverlays() {
    for (auto it = g_OverlayWindows.begin(); it != g_OverlayWindows.end(); ) {
        HWND target = it->first;
        if (!IsWindow(target)) {
            DestroyWindow(it->second);
            it = g_OverlayWindows.erase(it);
            continue;
        }
        UpdateOverlayPosition(target);
        ++it;
    }
}

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    if (event == EVENT_SYSTEM_FOREGROUND)
    {
        if (GetForegroundWindow() == hwnd) { // good for windows things
            return;
        }
        if (appCheck(hwnd))
        {
            RestoreWindowFromTray(hwnd);
        }
    }
    else if (event == EVENT_OBJECT_LOCATIONCHANGE)
    {
        if (idObject == OBJID_WINDOW && idChild == CHILDID_SELF) {
            if (g_OverlayWindows.find(hwnd) != g_OverlayWindows.end()) {
                UpdateOverlayPosition(hwnd);
            }
        }
    }
}

void HandleMinimizeCommand(HWND hwnd) {
    if (appCheck((HWND)hwnd)) {
        MinimizeWindowToTray((HWND)hwnd);
    }
    else {
        // Special case for "Cancel" browser popup windows
        wchar_t windowName[256];
        GetWindowText(hwnd, windowName, 256);
        if (wcscmp(windowName, L"Cancel") == 0 && UseWindowName.count(getProcessName(hwnd))) {
            SendMessage(hwnd, BM_CLICK, 0, 0);
            return;
        }
        SendMessage(GetForegroundWindow(), WM_SYSCOMMAND, SC_MINIMIZE, 0);
    }
}

void HandleCloseCommand(HWND hwnd) {
    if (HOOKBOTH && appCheck((HWND)hwnd)) {
        MinimizeWindowToTray((HWND)hwnd);
    }
    else {
        SendMessage(GetForegroundWindow(), WM_SYSCOMMAND, SC_CLOSE, 0);
        PostMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
    }
}

// Quick action: temporarily minimize to Tray
void HandleMinimizeRightClickCommand(HWND hwnd) {
    if (appCheck(hwnd, true)) {
        MinimizeWindowToTray(hwnd);
    }
}

// Quick action: save app to appNames and settings
void HandleCloseRightClickCommand(HWND hwnd) {
    if (!appCheck(hwnd, true)) {
        return;
    }
    if (MatchesAppName(hwnd)) {
        return;
    }

    std::wstring processName = getProcessName(hwnd);
    size_t extPos = processName.rfind(L'.');

    appNames.insert(processName);
    SaveSettings();
    MinimizeWindowToTray(hwnd);
}

// Quick action: toggle Always on Top
void HandleMaximizeRightClickCommand(HWND hwnd) {
    if (!IsWindow(hwnd)) return;
    if (PowerToysAlwaysOnTop()) return;

    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    bool isTopMost = (exStyle & WS_EX_TOPMOST) != 0;

    if (isTopMost) {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        RemoveOverlay(hwnd);
    }
    else {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        AddOverlay(hwnd);
    }
}

// This function handles all window messages
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Handle ImGui messages first
    LRESULT imguiResult = HandleImGuiMessages(hwnd, msg, wParam, lParam);
    // Debug mouse messages
    switch (msg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_SETCURSOR:
    case WM_MOUSELEAVE:
    {
        // char buf[256];
        // sprintf_s(buf, sizeof(buf), "WndProc: msg=0x%X, HandleImGuiMessages returned=%ld\n", msg, (long)imguiResult);
        // OutputDebugStringA(buf);
        break;
    }
    default:
        break;
    }

    if (imguiResult != -1) return imguiResult; // -1 means message not handled

    switch (msg) {
    case WM_TIMER:
        if (wParam == IMGUI_TIMER_ID) {
            // Periodic ImGui render tick
            RenderImGuiFrame();
            UpdateOverlays();
            return 0;
        }
        break;
    case WM_PAINT:
        RenderImGuiFrame();
        ValidateRect(hwnd, NULL);
        return 0;
    case WM_ERASEBKGND:
        // Prevent background erasing to avoid flicker
        return 1;
    case WM_COMMAND:
    {
        switch (LOWORD(wParam)) {
        case IDM_RESTORE:
            RestoreWindowFromTray(hwndForMenu);
            break;
        case IDM_CLOSE:
            CloseWindowFromTray(hwndForMenu);
            break;
        case IDM_ABOUT:
            ShellExecute(NULL, L"open", ABOUT_URL, NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDM_EXIT:
            SendMessage(hwnd, WM_DESTROY, 0, 0);
            break;
            // Note: Checkbox and button handling is now done in ImGui directly
        }
        break;
    }
    case WM_MIN:
        HandleMinimizeCommand((HWND)lParam);
        break;
    case WM_X:
        HandleCloseCommand((HWND)lParam);
        break;
    case WM_MIN_R:
        HandleMinimizeRightClickCommand((HWND)lParam);
        break;
    case WM_X_R:
        HandleCloseRightClickCommand((HWND)lParam);
        break;
    case WM_MAX_R:
        HandleMaximizeRightClickCommand((HWND)lParam);
        break;
    case WM_REMTRAY:
        RestoreWindowFromTray((HWND)lParam);
        break;
    case WM_REFRTRAY:
        RefreshWindowInTray((HWND)lParam);
        break;
    case WM_TRAYCMD:
    {
        switch ((UINT)lParam) {
        case NIN_SELECT:
        {
            if (IsWindowVisible(hwndItems[wParam])) {
                if (IsTopWindow(hwndItems[wParam])) {
                    MinimizeWindowToTray(hwndItems[wParam]);
                }
                else {
                    ShowWindow(hwndItems[wParam], SW_RESTORE);
                    SetForegroundWindow(hwndItems[wParam]);
                }
            }
            else {
                RestoreWindowFromTray(hwndItems[wParam]);
            }
            break;
        }
        case WM_CONTEXTMENU:
            hwndForMenu = hwndItems[wParam];
            ExecuteMenu();
            break;
        case WM_MOUSEMOVE:
            // RefreshWindowInTray(hwndItems[wParam]);
            break;
        }
        break;
    }
    case WM_SYSCOMMAND:
    {
        if (wParam == SC_CLOSE && hwnd == hwndMain) {
            MinimizeWindowToTray(hwndMain);
            return 0;
        }
        break;
    }
    case WM_DESTROY:
    {
        NOTASKBAR = false;
        for (int i = 0; i < MAXTRAYITEMS; i++) {
            if (hwndItems[i]) {
                RestoreWindowFromTray(hwndItems[i]);
                RemoveWindowFromTray(hwndItems[i]);
            }
        }
        if (hLib) {
            UnRegisterHook();
            UnhookWinEvent(hEventHook);
            if (hLocationHook) UnhookWinEvent(hLocationHook);
            FreeLibrary(hLib);
        }
        RefreshTray();
        CleanupSharedMemory();
        CleanupImGui();
        PostQuitMessage(0);
        break;
    }
    default:
        if (msg == WM_TASKBAR_CREATED) {
            for (int i = 0; i < MAXTRAYITEMS; i++) {
                if (hwndItems[i]) {
                    AddToTray(i);
                }
            }
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPSTR /*szCmdLine*/, _In_ int /*iCmdShow*/)
{
    hInstance = hInst;
    InitializeSharedMemory();

    CheckForUpdates();

    LoadSettings();

    HWND existingApp = FindWindow(NAME, NAME);
    if (existingApp) {
        RefreshTray();
        MessageBox(NULL, L"Trayy is already running.", NAME, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    if (!(hLib = LoadLibrary(L"hook.dll"))) {
        MessageBox(NULL, L"Error loading hook.dll.", NAME, MB_OK | MB_ICONERROR);
        return 0;
    }

    if (!RegisterHook(hLib)) {
        MessageBox(NULL, L"Error setting hook procedure.", NAME, MB_OK | MB_ICONERROR);
        return 0;
    }

    hEventHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!hEventHook) {
        MessageBox(NULL, L"Error setting event hook procedure.", NAME, MB_OK | MB_ICONERROR);
        return 0;
    }

    hLocationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);

    for (int i = 0; i < MAXTRAYITEMS; i++) {
        hwndItems[i] = NULL;
    }

    WM_TASKBAR_CREATED = RegisterWindowMessage(L"TaskbarCreated");

    RegisterOverlayClass(hInstance);

    InitializeUI(hInstance);

    MinimizeAllInBackground();
    RefreshTray();
    if (updateAvailable) {
        SetTrayIconUpdate();
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
