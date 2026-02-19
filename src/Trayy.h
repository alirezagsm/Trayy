#pragma once
#define LEAN_AND_MEAN
#define UNICODE
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include <windows.h>
#include <string>
#include <unordered_set>

inline const std::unordered_set<std::wstring> ExcludedNames = { L"Cancel", L"File Upload", L"ApplicationFrameHost.exe", L"MSCTFIME UI", L"Default IME", L"EVR Fullscreen Window" };
inline const std::unordered_set<std::wstring> ExcludedProcesses = { L"Chrom Legacy Window", L"Explorer.EXE", L"SearchHost.exe", L"svchost.exe", L"taskhostw.exe", L"OneDrive.exe", L"TextInputHost.exe", L"SystemSettings.exe", L"RuntimeBroker.exe", L"SearchUI.exe", L"ShellExperienceHost.exe", L"msedgewebview2.exe", L"pwahelper.exe", L"conhost.exe", L"VCTIP.EXE", L"GameBarFTServer.exe" };
inline const std::unordered_set<std::wstring> UseWindowName = { L"chrome.exe", L"firefox.exe", L"opera.exe", L"msedge.exe", L"iexplore.exe", L"brave.exe", L"vivaldi.exe", L"chromium.exe" };

#define MAXTRAYITEMS 64
#define MAX_SPECIAL_APPS 16

#define NAME L"Trayy"
#define VERSION L"v3.0"
#define SETTINGS_FILE L".settings.ini"
#define ABOUT_URL L"https://www.github.com/alirezagsm/Trayy"

#define IDI_ICON1        101
#define IDI_ICON2        102
#define ID_CHECKBOX1     103
#define ID_CHECKBOX2     104
#define ID_BUTTON        105
#define ID_UPDATE_BUTTON 106
#define ID_APPLIST       107

#define WM_MIN          0x0401
#define WM_X            0x0402
#define WM_MIN_R        0x0403
#define WM_X_R          0x0404
#define WM_REMTRAY      0x0405
#define WM_REFRTRAY     0x0406
#define WM_TRAYCMD      0x0407
#define WM_MAX_R        0x0408
#define IDM_RESTORE     0x1001
#define IDM_CLOSE       0x1002
#define IDM_EXIT        0x1003
#define IDM_ABOUT       0x1005
#define IMGUI_TIMER_ID  0x0501

#define SHARED_MEM_NAME L"TrayyConfig"
#define MAX_APPS 128
#define SHARED_MEM_SIZE sizeof(TrayySharedConfig)
#ifdef BUILD_HOOK_DLL
#define DLLIMPORT __declspec(dllexport)
#else
#define DLLIMPORT __declspec(dllimport)
#endif
#define OVERLAY_CLASS_NAME L"TrayyOverlay"

// Shared Memory
typedef struct {
    bool hookBoth;
    bool noTaskbar;
    int graphicalCount;
    wchar_t graphicalApps[MAX_SPECIAL_APPS][MAX_PATH];
    int standardCount;
    wchar_t standardApps[MAX_APPS][MAX_PATH];
} TrayySharedConfig;

// Global access
extern HWND hwndMain;
extern HWND hwndBase;
extern HWND hwndForMenu;
extern HINSTANCE hInstance;
extern std::unordered_set<std::wstring> appNames;
extern std::unordered_set<std::wstring> graphicalAppNames;
extern bool HOOKBOTH;
extern bool NOTASKBAR;
extern bool updateAvailable;
extern int DESKTOP_PADDING;

// Hook-related functions
BOOL DLLIMPORT RegisterHook(HMODULE);
void DLLIMPORT UnRegisterHook();
bool DLLIMPORT appCheck(HWND hwnd, bool RClick = false);
extern HANDLE hSharedMemory;
extern TrayySharedConfig* pSharedData;

// Trayy.cpp
void MinimizeWindowToTray(HWND hwnd);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void MinimizeAll();
void RefreshTray();
void SaveSettings();
void ReinstateTaskbarState();
void RestoreWindowFromTray(HWND hwnd);
void RestoreWindowFromTray(std::wstring appName);

// Trayy_UI.cpp
void InitializeUI(HINSTANCE hInstance);
void ShowAppInterface(bool minimizeToTray);
void ExecuteMenu();
void HandleUpdateButtonClick(HWND hwnd);
void SetTrayIconUpdate();
bool InitializeImGui(HWND hwnd);
void CleanupImGui();
void RenderImGuiFrame();
void RenderMainUI();
void MarkAppListDirty();
LRESULT HandleImGuiMessages(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Allow UI to notify shared-memory of graphical apps
BOOL UpdateSharedConfig();

// updater.cpp
void CheckForUpdates();
void CheckAndUpdate(const std::wstring& currentVersion);
