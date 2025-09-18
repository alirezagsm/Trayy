#include "Trayy.h"
#include <vector>
#include <string>
#include <fstream>
#include <shellapi.h>
#include <thread>
#include <set>
#include <unordered_set>
#include <psapi.h>
#include <algorithm>
#include <codecvt>
#include <locale>

// Global variables
UINT WM_TASKBAR_CREATED;
HWINEVENTHOOK hEventHook;
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

std::unordered_set<std::wstring> ExcludedNames = { L"ApplicationFrameHost.exe", L"MSCTFIME UI", L"Default IME", L"EVR Fullscreen Window" };
std::unordered_set<std::wstring> ExcludedProcesses = { L"Explorer.EXE", L"SearchHost.exe", L"svchost.exe", L"taskhostw.exe", L"OneDrive.exe", L"TextInputHost.exe", L"SystemSettings.exe", L"RuntimeBroker.exe", L"SearchUI.exe", L"ShellExperienceHost.exe", L"msedgewebview2.exe", L"pwahelper.exe", L"conhost.exe", L"VCTIP.EXE", L"GameBarFTServer.exe" };
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

std::wstring IsInAppNames(HWND hwnd) {
    std::wstring processName = getProcessName(hwnd);
    wchar_t windowName[256];
    GetWindowText(hwnd, windowName, 256);
    if (UseWindowName.find(processName) != UseWindowName.end()) {
        processName = windowName;
    }

    bool found = false;
    for (const auto& appName : appNames) {
        if (!appName.empty() && processName.find(appName) != std::wstring::npos) {
            found = true;
            break;
        }
    }
    if (found) {
        return L"found";
    }
    size_t extPos = processName.rfind(L'.');
    if (extPos != std::wstring::npos) {
        processName = processName.substr(0, extPos);
    }
    return processName;
}


void RefreshWindowInTray(HWND hwnd) {
    int i = FindInTray(hwnd);
    if (i == -1) {
        return;
    }
    if (!IsWindow(hwnd)) {
        RemoveWindowFromTray(hwnd);
    }
    else {
        if (IsInAppNames(hwnd) != L"found") {
            RemoveWindowFromTray(hwnd);
            return;
        }

        NOTIFYICONDATA nid;
        ZeroMemory(&nid, sizeof(nid));
        nid.cbSize = NOTIFYICONDATA_V2_SIZE;
        nid.hWnd = hwnd;
        nid.uID = (UINT)i;
        nid.uFlags = NIF_TIP;
        GetWindowText(hwnd, nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]));
        Shell_NotifyIcon(NIM_MODIFY, &nid);
    }
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
    if (wcsstr(windowName, NAME) != nullptr) {
        ShowWindow(hwnd, SW_SHOW);
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
    RefreshWindowInTray(hwnd);
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
            if (processName == appName) {
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

    // Change to window name if needed
    bool switchedProcessName = false;
    if (UseWindowName.find(processName) != UseWindowName.end()) {
        processName = windowName;
        switchedProcessName = true;
    }

    // Multi-window support
    if (!switchedProcessName) {} {
        size_t prefixLen = std::min<size_t>(4, processName.size());
        std::wstring prefix = processName.substr(0, prefixLen);
        std::wstring windowLower = windowNameStr;
        std::wstring prefixLower = prefix;
        std::transform(windowLower.begin(), windowLower.end(), windowLower.begin(), ::towlower);
        std::transform(prefixLower.begin(), prefixLower.end(), prefixLower.begin(), ::towlower);
        if (windowLower.find(prefixLower) == std::wstring::npos) {
            std::wstring debugMsg = L"Skipping " + processName + L" with window name " + windowNameStr + L"\n";
            OutputDebugString(debugMsg.c_str());
            return false;
        }
    }

    if (RClick) {
        return true;
    }

    for (const auto& appName : appNames) {
        if (!appName.empty() && processName.find(appName) != std::wstring::npos) {
            return true;
        }
    }
    if (processName == NAME L".exe" && wcscmp(windowName, NAME) == 0) {
        return true;
    }
    return false;
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
void HandleMinimizeCommand(HWND hwnd) {
    if (appCheck((HWND)hwnd)) {
        MinimizeWindowToTray((HWND)hwnd);
    }
    else {
        SendMessage(GetForegroundWindow(), WM_SYSCOMMAND, SC_MINIMIZE, 0);
    }
}


void HandleCloseCommand(HWND hwnd) {
    if (HOOKBOTH && appCheck((HWND)hwnd)) {
        MinimizeWindowToTray((HWND)hwnd);
    }
    else {
        wchar_t windowName[256];
        GetWindowText(hwnd, windowName, 256);

        SendMessage(GetForegroundWindow(), WM_SYSCOMMAND, SC_CLOSE, 0);

        // if (wcslen(windowName) == 0) {
        //     SendMessage(GetForegroundWindow(), WM_SYSCOMMAND, SC_CLOSE, 0);
        // }
        // else {
        //     PostMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        // }
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
    std::wstring processName = IsInAppNames(hwnd);
    if (processName == L"found") {
        return;
    }
    appNames.insert(processName);
    SaveSettings();
    MinimizeWindowToTray(hwnd);

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
        if (wParam == SC_CLOSE) {
            SendMessage(GetForegroundWindow(), WM_SYSCOMMAND, SC_MINIMIZE, 0);
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

    for (int i = 0; i < MAXTRAYITEMS; i++) {
        hwndItems[i] = NULL;
    }

    WM_TASKBAR_CREATED = RegisterWindowMessage(L"TaskbarCreated");


    InitializeUI(hInstance);
    ShowAppInterface(true);

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
