#include "Trayy.h"
#include "Trayy_UI.h"
#include <vector>
#include <string>
#include <fstream>
#include <shellapi.h>
#include <thread>
#include <set>
#include <stdio.h>
#include <psapi.h>

#pragma comment(lib, "Gdi32.lib")

// Global variables
UINT WM_TASKBAR_CREATED;
HWINEVENTHOOK hEventHook;
HINSTANCE hInstance;
HMODULE hLib;
HWND hwndBase;
HWND hwndMain;
HWND hwndItems[MAXTRAYITEMS];
HWND hwndForMenu;
std::vector<std::wstring> appNames;
bool HOOKBOTH = true;
bool NOTASKBAR = false;

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

bool AddToTray(int i) {  // Removed 'static' keyword
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

static bool RemoveFromTray(int i) {
    NOTIFYICONDATA nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = NOTIFYICONDATA_V2_SIZE;
    nid.hWnd = hwndMain;
    nid.uID = (UINT)i;
    if (!Shell_NotifyIcon(NIM_DELETE, &nid)) {
        return false;
    }
    return true;
}

bool RemoveWindowFromTray(HWND hwnd) {
    int i = FindInTray(hwnd);
    if (i == -1) {
        return false;
    }
    if (!RemoveFromTray(i)) {
        return false;
    }
    hwndItems[i] = NULL;
    return true;
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
}

void CloseWindowFromTray(HWND hwnd) {
    ShowWindow(hwnd, SW_SHOW);
    PostMessage(hwnd, WM_CLOSE, 0, 0);

    Sleep(50);
    if (IsWindow(hwnd)) {
        Sleep(50);
    }

    if (!IsWindow(hwnd)) {
        RemoveWindowFromTray(hwnd);
    }
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

bool appCheck(HWND lParam, bool restore) {
    wchar_t windowName[256];
    GetWindowText(lParam, windowName, 256);

    if (wcslen(windowName) == 0) {
        return false;
    }

    if (restore) {
        if (IsWindowVisible(lParam)) {
            return false;
        }
    }

    std::vector<std::wstring> ExcludedNames = { L"ApplicationFrameHost.exe", L"MSCTFIME UI", L"Default IME", L"EVR Fullscreen Window" };
    for (const auto& exclusion : ExcludedNames) {
        if (wcsstr(windowName, exclusion.c_str()) != nullptr) {
            return false;
        }
    }

    std::wstring processName = getProcessName(lParam);
    if (processName.empty()) {
        return false;
    }

    // Exclude some processes
    std::vector<const wchar_t*> ExcludedProcesses = { L"Explorer.EXE", L"SearchHost.exe", L"svchost.exe", L"taskhostw.exe", L"OneDrive.exe" ,L"TextInputHost.exe", L"SystemSettings.exe", L"RuntimeBroker.exe", L"SearchUI.exe", L"ShellExperienceHost.exe", L"msedgewebview2.exe", L"pwahelper.exe", L"conhost.exe", L"VCTIP.EXE", L"GameBarFTServer.exe" };
    for (const auto& exclusion : ExcludedProcesses) {
        if (wcscmp(processName.c_str(), exclusion) == 0) {
            return false;
        }
    }

    // change to window name if needed
    std::vector<std::wstring> swithNames = { L"thunderbird.exe", L"chrome.exe", L"firefox.exe", L"opera.exe", L"msedge.exe", L"iexplore.exe", L"brave.exe", L"vivaldi.exe", L"chromium.exe" };
    for (const auto& swithName : swithNames) {
        if (processName == swithName) {
            processName = windowName;
            break;
        }
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

void RefreshTray() {
    for (int i = 0; i < MAXTRAYITEMS; i++) {
        if (hwndItems[i] && IsWindow(hwndItems[i])) {
            HICON hIcon = GetWindowIcon(hwndItems[i]);
            
            NOTIFYICONDATA nid;
            ZeroMemory(&nid, sizeof(nid));
            nid.cbSize = NOTIFYICONDATA_V2_SIZE;
            nid.hWnd = hwndMain;
            nid.uID = (UINT)i;
            nid.uFlags = NIF_ICON | NIF_TIP;
            nid.hIcon = hIcon;
            GetWindowText(hwndItems[i], nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]));
            
            Shell_NotifyIcon(NIM_MODIFY, &nid);
        }
    }
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
            Sleep(10000);
        }
        MinimizeAll();
    });
    t.detach();
}

void SaveSettings() {
    std::wofstream file(SETTINGS_FILE, std::ios::out | std::ios::trunc);
    file << "HOOKBOTH " << (HOOKBOTH ? "true" : "false") << std::endl;
    file << "NOTASKBAR " << (NOTASKBAR ? "true" : "false") << std::endl;
    
    for (const auto& appName : appNames) {
        file << appName << std::endl;
    }
    file.close();
    RefreshTray();
}

void LoadSettings() {
    std::wifstream file(SETTINGS_FILE);
    if (!file) {
        std::wofstream file(SETTINGS_FILE, std::ios::out | std::ios::trunc);
        file << "HOOKBOTH " << (HOOKBOTH ? "true" : "false") << std::endl;
        file << "NOTASKBAR " << (NOTASKBAR ? "true" : "false") << std::endl;
    }
    else {
        try {
            std::wstring line;
            std::getline(file, line);
            HOOKBOTH = line.find(L"true") != std::string::npos;
            std::getline(file, line);
            NOTASKBAR = line.find(L"true") != std::string::npos;

            while (std::getline(file, line))
            {
                if (!line.empty())
                {
                    appNames.push_back(line);
                }
            }
            file.close();
        }
        catch (const std::exception&)
        {
            std::wstring backupFile = SETTINGS_FILE L".bak";
            CopyFile(SETTINGS_FILE, backupFile.c_str(), FALSE);
            std::wofstream file(SETTINGS_FILE, std::ios::out | std::ios::trunc);
            file << "HOOKBOTH " << (HOOKBOTH ? "true" : "false") << std::endl;
            file << "NOTASKBAR " << (NOTASKBAR ? "true" : "false") << std::endl;
        }
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
}

// This function handles all window messages
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
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
        case ID_CHECKBOX1:
            HandleCheckboxClick(hwnd, ID_CHECKBOX1);
            break;
        case ID_CHECKBOX2:
            HandleCheckboxClick(hwnd, ID_CHECKBOX2);
            break;
        case IDM_EXIT:
            SendMessage(hwnd, WM_DESTROY, 0, 0);
            break;
        case ID_BUTTON:
            HandleSaveButtonClick(hwnd);
            break;
        }
        break;
    }
    case WM_MIN:
        HandleMinimizeCommand((HWND)lParam);
        break;
    case WM_X:
        HandleCloseCommand((HWND)lParam);
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
            RefreshWindowInTray(hwndItems[wParam]);
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
    case WM_NOTIFY:
        HandleListViewNotifications(hwnd, lParam);
        break;
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
        CleanupResources();
        
        // Your existing PostQuitMessage call
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

    // Create hwndBase to be the main window that is hidden and used to receive messages
    hwndBase = CreateWindowEx(0, WC_STATIC, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    // Initialize windows items array
    for (int i = 0; i < MAXTRAYITEMS; i++) {
        hwndItems[i] = NULL;
    }
    
    WM_TASKBAR_CREATED = RegisterWindowMessage(L"TaskbarCreated");

    InitializeUI(hInstance);
    ShowAppInterface(true);
    
    MinimizeAllInBackground();
    RefreshTray();

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
