#include "Trayy.h"
#include <Shellscalingapi.h>
#include <Psapi.h>
#include <windowsx.h>

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

inline void SendWindowAction(HWND hwnd, bool isMinimize, bool isMaximize, bool isRightClick = false) {
    HWND mainWindow = FindWindow(NAME, NAME);
    if (mainWindow) {
        UINT msg = 0;
        if (isRightClick) {
            if (isMaximize)
                msg = WM_MAX_R;
            else
                msg = isMinimize ? WM_MIN_R : WM_X_R;
        }
        else {
            if (isMaximize) return; // Default behavior for left click on maximize
            msg = isMinimize ? WM_MIN : WM_X;
        }
        if (msg)
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

bool IsSpecialApp(const std::wstring& processName, int& customWidth, int& customHeight) {
    if (!AccessSharedMemory() || !_pSharedData)
        return false;

    customWidth = 0;
    customHeight = 0;

    for (int i = 0; i < _pSharedData->count; i++) {
        std::wstring specialAppEntry = _pSharedData->specialApps[i];
        size_t firstSpace = specialAppEntry.find(L' ');
        std::wstring appName = specialAppEntry.substr(0, firstSpace);

        if (_wcsicmp(appName.c_str(), processName.c_str()) == 0) {
            size_t lastSpace = specialAppEntry.find_last_of(L' ');
            if (lastSpace != std::wstring::npos && lastSpace + 1 < specialAppEntry.length()) {
                std::wstring whStr = specialAppEntry.substr(lastSpace + 1);
                if (whStr.length() > 2 && (whStr[0] == L'w' || whStr[0] == L'W')) {
                    size_t hPos = std::wstring::npos;
                    for (size_t j = 1; j < whStr.length(); ++j) {
                        if (whStr[j] == L'h' || whStr[j] == L'H') {
                            hPos = j;
                            break;
                        }
                    }

                    if (hPos != std::wstring::npos && hPos > 1 && hPos < whStr.length() - 1) {
                        try {
                            std::wstring wStr = whStr.substr(1, hPos - 1);
                            std::wstring hStr = whStr.substr(hPos + 1);
                            if (!wStr.empty() && !hStr.empty()) {
                                customWidth = std::stoi(wStr);
                                customHeight = std::stoi(hStr);
                            }
                        }
                        catch (...) {
                            // Parsing failed, treat as a normal special app
                        }
                    }
                }
            }
            return true; // It's a special app
        }
    }

    if (processName == L"firefox.exe") // default to Graphical mode for Firefox
        return true;

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
            const BOOL isHitMax = (info->wHitTestCode == HTMAXBUTTON);
            const BOOL isHitX = (info->wHitTestCode == HTCLOSE);
            const BOOL isRight = (wParam == WM_NCRBUTTONDOWN || wParam == WM_NCRBUTTONUP);

            if (((wParam == WM_NCLBUTTONDOWN) || (wParam == WM_NCRBUTTONDOWN)) && (isHitMin || isHitMax || isHitX)) {
                _hLastHit = info->hwnd;
                if (ActivateWindow(info->hwnd))
                    return 1;
            }
            else if (((wParam == WM_NCLBUTTONUP) || (wParam == WM_NCRBUTTONUP)) && (isHitMin || isHitMax || isHitX)) {
                if (info->hwnd == _hLastHit) {
                    SendWindowAction(info->hwnd, isHitMin, isHitMax, isRight);
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
        int customWidth = 0, customHeight = 0;

        if (!processName.empty() && IsSpecialApp(processName, customWidth, customHeight)) {
            TITLEBARINFOEX tbi;
            tbi.cbSize = sizeof(TITLEBARINFOEX);
            SendMessage(hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&tbi);

            bool isHitMin = false;
            bool isHitMax = false;
            bool isHitX = false;

            // First, try the reliable GetTitleBarInfoEx method
            if (tbi.rgrect[2].right > tbi.rgrect[2].left && PtInRect(&tbi.rgrect[2], pt)) { // Minimize button
                isHitMin = true;
            }
            if (tbi.rgrect[3].right > tbi.rgrect[3].left && PtInRect(&tbi.rgrect[3], pt)) { // Maximize button
                isHitMax = true;
            }
            if (tbi.rgrect[5].right > tbi.rgrect[5].left && PtInRect(&tbi.rgrect[5], pt)) { // Close button
                isHitX = true;
            }

            // Fallback to manual calculation if GetTitleBarInfoEx fails or for custom dimensions
            if (!isHitMin && !isHitMax && !isHitX) {
                RECT windowRect;
                if (!GetWindowRect(hwnd, &windowRect))
                    return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);

                float dpiScale = GetWindowDpiScale(hwnd);
                int windowWidth = windowRect.right - windowRect.left;
                int relativeX = pt.x - windowRect.left;
                int relativeY = pt.y - windowRect.top;
                int titleBarHeight, buttonWidth;

                if (customWidth > 0 && customHeight > 0) {
                    titleBarHeight = customHeight * dpiScale;
                    buttonWidth = customWidth * dpiScale;
                }
                else {
                    const int captionHeight = GetSystemMetrics(SM_CYCAPTION);
                    const int frameHeight = GetSystemMetrics(SM_CYFRAME);
                    const int borderPadding = GetSystemMetrics(SM_CXPADDEDBORDER);
                    float aspectRatio = 1.64f;
                    int offsetY = -12;
                    if (processName == L"thunderbird.exe") {
                        aspectRatio = 1.5f;
                        offsetY = -1;
                    }
                    titleBarHeight = (captionHeight + frameHeight + borderPadding * 2 + offsetY) * dpiScale;
                    buttonWidth = titleBarHeight * aspectRatio;
                }

                int closeButtonLeft = windowWidth - buttonWidth;
                int maximizeButtonLeft = closeButtonLeft - buttonWidth;
                int minimizeButtonLeft = maximizeButtonLeft - buttonWidth;

                bool isInTitleBar = (relativeY < titleBarHeight);
                if (isInTitleBar) {
                    isHitX = (relativeX >= closeButtonLeft);
                    isHitMax = (relativeX >= maximizeButtonLeft) && (relativeX < closeButtonLeft);
                    isHitMin = (relativeX >= minimizeButtonLeft) && (relativeX < maximizeButtonLeft);
                }
            }

            // Handle button clicks for special apps
            bool isRight = (wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP);
            if ((wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN)) {
                if (isHitX || isHitMin || isHitMax) {
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
                if (isHitX || isHitMin || isHitMax) {
                    SendWindowAction(hwnd, isHitMin, isHitMax, isRight);
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
