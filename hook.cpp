#include "Trayy.h"
#include <Shellscalingapi.h>
#include <Psapi.h>

static HHOOK _hMouse = NULL;
static HHOOK _hLLMouse = NULL;
static HWND _hLastHit = NULL;
static HANDLE _hSharedMemory = NULL;
static SpecialAppsSharedData* _pSharedData = NULL;

bool ActivateWindow(HWND hwnd) {
    if (!IsWindow(hwnd))
        return false;

    if (GetForegroundWindow() == hwnd)
        return true;

    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);

    SetForegroundWindow(hwnd);
    return (GetForegroundWindow() == hwnd);
}

inline void SendWindowAction(HWND hwnd, bool isMinimize, bool isRightClick = false) {
    HWND mainWindow = FindWindow(NAME, NAME);
    if (mainWindow) {
        UINT msg = 0;
        if (isRightClick) {
            msg = isMinimize ? WM_MIN_R : WM_X_R;
        }
        else {
            msg = isMinimize ? WM_MIN : WM_X;
        }
        PostMessage(mainWindow, msg, 0, reinterpret_cast<LPARAM>(hwnd));
    }
}

bool AccessSharedMemory() {
    if (_pSharedData != NULL)
        return true;

    _hSharedMemory = OpenFileMapping(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        SHARED_MEM_NAME);

    if (_hSharedMemory == NULL) {
        return false;
    }

    _pSharedData = (SpecialAppsSharedData*)MapViewOfFile(
        _hSharedMemory,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        SHARED_MEM_SIZE);

    if (_pSharedData == NULL) {
        CloseHandle(_hSharedMemory);
        _hSharedMemory = NULL;
        return false;
    }

    return true;
}

void ReleaseSharedMemory() {
    if (_pSharedData) {
        UnmapViewOfFile(_pSharedData);
        _pSharedData = NULL;
    }

    if (_hSharedMemory) {
        CloseHandle(_hSharedMemory);
        _hSharedMemory = NULL;
    }
}

bool IsSpecialApp(const std::wstring& processName) {
    if (!AccessSharedMemory() || !_pSharedData)
        return false;

    std::wstring nameWithoutExt = processName;

    size_t extPos = nameWithoutExt.rfind(L'.');
    if (extPos != std::wstring::npos) {
        nameWithoutExt = nameWithoutExt.substr(0, extPos);
    }

    for (int i = 0; i < _pSharedData->count; i++) {
        if (_wcsicmp(_pSharedData->specialApps[i], nameWithoutExt.c_str()) == 0) {
            return true;
        }
    }

    return false;
}

// Mouse hook to handle non-client area clicks
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0)
        return CallNextHookEx(_hMouse, nCode, wParam, lParam);

    MOUSEHOOKSTRUCT* info = reinterpret_cast<MOUSEHOOKSTRUCT*>(lParam);
    if (!info)
        return CallNextHookEx(_hMouse, nCode, wParam, lParam);


    // Handle left and right clicks on non-client area
    if ((wParam == WM_NCLBUTTONDOWN) || (wParam == WM_NCLBUTTONUP) || (wParam == WM_NCRBUTTONDOWN) || (wParam == WM_NCRBUTTONUP)) {
        if (info->wHitTestCode != HTCLIENT) {
            const BOOL isHitMin = (info->wHitTestCode == HTMINBUTTON);
            const BOOL isHitX = (info->wHitTestCode == HTCLOSE);
            const BOOL isRight = (wParam == WM_NCRBUTTONDOWN || wParam == WM_NCRBUTTONUP);

            if (((wParam == WM_NCLBUTTONDOWN) || (wParam == WM_NCRBUTTONDOWN)) && (isHitMin || isHitX)) {
                _hLastHit = info->hwnd;
                if (ActivateWindow(info->hwnd))
                    return 1;
            }
            else if (((wParam == WM_NCLBUTTONUP) || (wParam == WM_NCRBUTTONUP)) && (isHitMin || isHitX)) {
                if (info->hwnd == _hLastHit) {
                    SendWindowAction(info->hwnd, isHitMin, isRight);
                    _hLastHit = NULL;
                    return 1;
                }
                _hLastHit = NULL;
            }
            else {
                _hLastHit = NULL;
            }
        }
    }
    else if ((wParam == WM_LBUTTONDOWN) || (wParam == WM_RBUTTONUP)) {
        _hLastHit = NULL;
    }

    return CallNextHookEx(_hMouse, nCode, wParam, lParam);
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

float GetWindowDpiScale(HWND hwnd) {
    UINT dpiX = 96, dpiY = 96;
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor) {
        GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    }
    if (dpiY == 0) dpiY = 96;
    return dpiY / 96.0f;
}

// Low-level Mouse hook to handle special apps
LRESULT CALLBACK LLMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0)
        return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);

    MSLLHOOKSTRUCT* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
    if (!info)
        return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);

    if (wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP || wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP) {
        POINT pt = info->pt;
        HWND hwnd = WindowFromPoint(pt);

        if (!hwnd)
            return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);

        std::wstring processName = getProcessName(hwnd);

        if (!processName.empty() && IsSpecialApp(processName)) {
            // Calculate position relative to window
            RECT windowRect;
            if (!GetWindowRect(hwnd, &windowRect))
                return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);

            int windowWidth = windowRect.right - windowRect.left;
            int relativeX = pt.x - windowRect.left;
            int relativeY = pt.y - windowRect.top;

            // Get DPI scale
            float dpiScale = GetWindowDpiScale(hwnd);

            // Calculate sizes
            const int captionHeight = GetSystemMetrics(SM_CYCAPTION);
            const int frameHeight = GetSystemMetrics(SM_CYFRAME);
            const int borderPadding = GetSystemMetrics(SM_CXPADDEDBORDER);

            // custom scaling for specific apps
            float aspectRatio = 1.64f;
            int offsetY = -12;
            if (processName == L"thunderbird.exe") {
                aspectRatio = 1.5f;
                offsetY = -1;
            }
            int titleBarHeight = (captionHeight + frameHeight + borderPadding * 2 + offsetY) * dpiScale;
            int buttonWidth = titleBarHeight * aspectRatio;
            int closeButtonLeft = windowWidth - buttonWidth;
            int maximizeButtonLeft = closeButtonLeft - buttonWidth;
            int minimizeButtonLeft = maximizeButtonLeft - buttonWidth;

            // Determine hits
            bool isInTitleBar = (relativeY < titleBarHeight);
            bool isHitX = isInTitleBar && (relativeX >= closeButtonLeft);
            bool isHitMin = isInTitleBar && (relativeX >= minimizeButtonLeft) && (relativeX < maximizeButtonLeft);

            // Handle button clicks for special apps
            bool isRight = (wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP);
            if ((wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN)) {
                if (isHitX || isHitMin) {
                    _hLastHit = hwnd;
                    if (ActivateWindow(hwnd)) {
                        return 1; // Consume the message
                    }
                }
                else {
                    _hLastHit = NULL;
                }
            }
            else if ((wParam == WM_LBUTTONUP || wParam == WM_RBUTTONUP) && hwnd == _hLastHit) {
                if (isHitX || isHitMin) {
                    SendWindowAction(hwnd, isHitMin, isRight);
                    _hLastHit = NULL;
                    return 1;
                }
            }
        }
    }
    else if (wParam == WM_RBUTTONUP) {
        _hLastHit = NULL;
    }

    return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);
}

BOOL DLLIMPORT RegisterHook(HMODULE hLib) {
    // Set DPI awareness
    if (FAILED(SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE))) {
        SetProcessDPIAware();
    }

    _hMouse = SetWindowsHookEx(WH_MOUSE, MouseProc, hLib, 0);
    _hLLMouse = SetWindowsHookEx(WH_MOUSE_LL, LLMouseProc, hLib, 0);

    if (_hMouse == NULL && _hLLMouse == NULL) {
        UnRegisterHook();
        return FALSE;
    }
    return TRUE;
}

void DLLIMPORT UnRegisterHook() {
    if (_hMouse) {
        UnhookWindowsHookEx(_hMouse);
        _hMouse = NULL;
    }
    if (_hLLMouse) {
        UnhookWindowsHookEx(_hLLMouse);
        _hLLMouse = NULL;
    }
    ReleaseSharedMemory();
}
