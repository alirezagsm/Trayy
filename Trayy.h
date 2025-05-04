#pragma once
#define LEAN_AND_MEAN
#define UNICODE
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_set>

#define MAXTRAYITEMS 64
#define MAX_SPECIAL_APPS 16

#define NAME L"Trayy"
#define SETTINGS_FILE L".settings.ini"
#define ABOUT_URL L"https://www.github.com/alirezagsm/Trayy"

#define IDI_ICON1       101
#define ID_CHECKBOX1    102
#define ID_CHECKBOX2    103
#define ID_BUTTON       104
#define ID_APPLIST      105
#define ID_GUI          106

#define WM_MIN          0x0401
#define WM_X            0x0402
#define WM_REMTRAY      0x0403
#define WM_REFRTRAY     0x0404
#define WM_TRAYCMD      0x0405
#define IDM_RESTORE     0x1001
#define IDM_CLOSE       0x1002
#define IDM_EXIT        0x1003
#define IDM_LIST        0x1004
#define IDM_ABOUT       0x1005

#define SHARED_MEM_NAME L"TraySpecialApps"
#define SHARED_MEM_SIZE (MAX_SPECIAL_APPS * MAX_PATH * sizeof(wchar_t))

#define DLLIMPORT __declspec(dllexport)

// Shared Memory
typedef struct {
    int count;
    wchar_t specialApps[MAX_SPECIAL_APPS][MAX_PATH];
} SpecialAppsSharedData;
BOOL DLLIMPORT InitializeSharedMemory();
void DLLIMPORT CleanupSharedMemory();
BOOL DLLIMPORT UpdateSpecialAppsList(const std::vector<std::wstring>& specialApps);

// Global access
extern HWND hwndMain;
extern HWND hwndBase;
extern HWND hwndItems[MAXTRAYITEMS];
extern HWND hwndForMenu;
extern std::unordered_set<std::wstring> appNames;
extern std::unordered_set<std::wstring> specialAppNames;
extern bool HOOKBOTH;
extern bool NOTASKBAR;

// Global variables
extern UINT WM_TASKBAR_CREATED;
extern HWINEVENTHOOK hEventHook;
extern HINSTANCE hInstance;
extern HMODULE hLib;
extern HANDLE hSharedMemory;
extern SpecialAppsSharedData* pSharedData;

// Hook-related functions
BOOL DLLIMPORT RegisterHook(HMODULE);
void DLLIMPORT UnRegisterHook();

// Core functions
void MinimizeWindowToTray(HWND hwnd);
void RestoreWindowFromTray(HWND hwnd);
void CloseWindowFromTray(HWND hwnd);
void RefreshWindowInTray(HWND hwnd);
bool RemoveWindowFromTray(HWND hwnd);
int FindInTray(HWND hwnd);
bool AddToTray(int i);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
std::wstring getProcessName(HWND hwnd);
bool appCheck(HWND lParam, bool restore = false);
bool IsTopWindow(HWND hwnd);
HICON GetWindowIcon(HWND hwnd);
void MinimizeAll();
void MinimizeAllInBackground();
void RefreshTray();
void LoadSettings();
void SaveSettings();
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);