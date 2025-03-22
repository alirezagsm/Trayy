#include "Trayy.h"
#include <Shellscalingapi.h>
#include <Psapi.h>

static HHOOK _hMouse = NULL;
static HHOOK _hLLMouse = NULL;
static HWND _hLastHit = NULL;

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

inline void SendWindowAction(HWND hwnd, bool isMinimize) {
    HWND mainWindow = FindWindow(NAME, NAME);
    if (mainWindow) {
        PostMessage(mainWindow, 
                    isMinimize ? WM_MIN : WM_X, 
                    0, 
                    reinterpret_cast<LPARAM>(hwnd));
    }
}

// Mouse hook to handle non-client area clicks
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0)
        return CallNextHookEx(_hMouse, nCode, wParam, lParam);
    
    MOUSEHOOKSTRUCT* info = reinterpret_cast<MOUSEHOOKSTRUCT*>(lParam);
    if (!info)
        return CallNextHookEx(_hMouse, nCode, wParam, lParam);
    
    if ((wParam == WM_NCLBUTTONDOWN) || (wParam == WM_NCLBUTTONUP)) {
        if (info->wHitTestCode != HTCLIENT) {
            const BOOL isHitMin = (info->wHitTestCode == HTMINBUTTON);
            const BOOL isHitX = (info->wHitTestCode == HTCLOSE);
            
            if ((wParam == WM_NCLBUTTONDOWN) && (isHitMin || isHitX)) {
                _hLastHit = info->hwnd;
                if (ActivateWindow(info->hwnd))
                    return 1;
            }
            else if ((wParam == WM_NCLBUTTONUP) && (isHitMin || isHitX)) {
                if (info->hwnd == _hLastHit) {
                    SendWindowAction(info->hwnd, isHitMin);
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
    return dpiY / 96.0f;
}

// Low-level Mouse hook to handle special apps
LRESULT CALLBACK LLMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0)
        return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);

    MSLLHOOKSTRUCT* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
    if (!info)
        return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);
    
    if (wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP) {
        POINT pt = info->pt;
        HWND hwnd = WindowFromPoint(pt);
        
        if (!hwnd)
            return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);
            
        std::wstring processName = getProcessName(hwnd);
        
        if (!processName.empty() && wcscmp(processName.c_str(), L"thunderbird.exe") == 0) {
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
            const int baseButtonWidth = GetSystemMetrics(SM_CXSIZE);
            
            int titleBarHeight = static_cast<int>((captionHeight + frameHeight + borderPadding) * dpiScale) + 4;
            int buttonWidth = static_cast<int>(baseButtonWidth * dpiScale * 1.431); // button width to height ratio
            int closeButtonLeft = windowWidth - buttonWidth;
            int maximizeButtonLeft = closeButtonLeft - buttonWidth;
            int minimizeButtonLeft = maximizeButtonLeft - buttonWidth;

            // Determine hits
            bool isInTitleBar = (relativeY < titleBarHeight);
            bool isHitX = isInTitleBar && (relativeX >= closeButtonLeft);
            bool isHitMin = isInTitleBar && (relativeX >= minimizeButtonLeft) && (relativeX < maximizeButtonLeft);

            // Handle button clicks for Thunderbird
            if (wParam == WM_LBUTTONDOWN) {
                if (isHitX || isHitMin) {
                    _hLastHit = hwnd;
                    if (ActivateWindow(hwnd)) {
                        return 1; // Consume the message
                    }
                } else {
                    _hLastHit = NULL;
                }
            } 
            else if (wParam == WM_LBUTTONUP && hwnd == _hLastHit) {
                if (isHitX || isHitMin) {
                    SendWindowAction(hwnd, isHitMin);
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
}
