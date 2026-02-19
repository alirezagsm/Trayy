#define BUILD_HOOK_DLL
#include "Trayy.h"
#include <Shellscalingapi.h>
#include <Psapi.h>
#include <windowsx.h>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <set>
#include <string_view>
#include <regex>

static HHOOK _hMouse = NULL;
static HHOOK _hLLMouse = NULL;
static HWND _hLastHit = NULL;
static HANDLE _hSharedMemory = NULL;
static TrayySharedConfig* _pSharedData = NULL;
static std::unordered_map<DWORD, std::wstring> _pidCache;

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

inline bool SendWindowAction(HWND hwnd, bool isMinimize, bool isMaximize, bool isRightClick = false) {
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
            if (isMaximize) return false; // Default behavior for left click on maximize
            msg = isMinimize ? WM_MIN : WM_X;
        }
        if (msg) {
            PostMessage(mainWindow, msg, 0, reinterpret_cast<LPARAM>(hwnd));
            return true;
        }
    }
    return false;
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

    _pSharedData = (TrayySharedConfig*)MapViewOfFile(
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

std::wstring getProcessName(HWND hwnd) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) return L"";

    auto it = _pidCache.find(processId);
    if (it != _pidCache.end()) {
        return it->second;
    }

    wchar_t processName[MAX_PATH] = L"";

    if (processId == GetCurrentProcessId()) {
        if (GetModuleFileName(NULL, processName, MAX_PATH)) {
            wchar_t* pName = wcsrchr(processName, L'\\');
            std::wstring name = (pName ? pName + 1 : processName);
            _pidCache[processId] = name;
            return name;
        }
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess == NULL) {
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    }

    if (hProcess == NULL)
        return L"";

    DWORD size = MAX_PATH;
    if (QueryFullProcessImageName(hProcess, 0, processName, &size)) {
        wchar_t* pName = wcsrchr(processName, L'\\');
        std::wstring name = (pName ? pName + 1 : processName);
        _pidCache[processId] = name;
        CloseHandle(hProcess);
        return name;
    }

    // fallback
    HMODULE hMod;
    DWORD cbNeeded;
    if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
        GetModuleBaseName(hProcess, hMod, processName, sizeof(processName) / sizeof(TCHAR));
        _pidCache[processId] = processName;
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
                catch (...) {}
            }
        }
    }
    return appName;
}

bool IsExcluded(HWND hwnd, std::wstring& outProcessName, std::wstring& outWindowName) {
    AccessSharedMemory();

    wchar_t windowName[256];
    GetWindowText(hwnd, windowName, 256);
    outWindowName = windowName;

    if (outWindowName.empty() || ExcludedNames.find(outWindowName) != ExcludedNames.end()) {
        return true;
    }

    outProcessName = getProcessName(hwnd);
    if (outProcessName.empty()) {
        return true;
    }

    if (ExcludedProcesses.find(outProcessName) != ExcludedProcesses.end()) {
        return true;
    }

    HWND mainWindow = FindWindow(NAME, NAME);
    if (hwnd == mainWindow) {
        return true;
    }

    return false;
}

struct AppMatchResult {
    bool matched;
    int customWidth;
    int customHeight;

    AppMatchResult() : matched(false), customWidth(0), customHeight(0) {}
};

bool ParseDimensions(std::wstring_view remainder, int& w, int& h, std::wstring_view& titlePart) {
    if (remainder.length() >= 6) {
        size_t hPos = remainder.find_last_of(L"hH");
        if (hPos != std::wstring_view::npos && hPos > 2 && hPos < remainder.length() - 1) {
            size_t wSearchEnd = hPos - 1;
            size_t wPos = remainder.find_last_of(L"wW", wSearchEnd);

            if (wPos != std::wstring_view::npos && (wPos == 0 || remainder[wPos - 1] == L' ')) {
                try {
                    std::wstring wStr(remainder.substr(wPos + 1, hPos - wPos - 1));
                    std::wstring hStr(remainder.substr(hPos + 1));
                    w = std::stoi(wStr);
                    h = std::stoi(hStr);

                    if (wPos > 0) {
                        size_t titleEnd = wPos;
                        while (titleEnd > 0 && remainder[titleEnd - 1] == L' ') titleEnd--;
                        titlePart = remainder.substr(0, titleEnd);
                    }
                    else {
                        titlePart = std::wstring_view();
                    }
                    return true;
                }
                catch (...) {}
            }
        }
    }
    titlePart = remainder;
    return false;
}


bool CheckTitleMatch(const std::wstring& windowName, std::wstring_view titlePartRaw) {
    std::wstring_view prefixRegex = L"regex:";

    if (titlePartRaw.empty()) return true;

    auto trimSpaces = [](std::wstring_view s) -> std::wstring_view {
        while (!s.empty() && s.front() == L' ') s.remove_prefix(1);
        while (!s.empty() && s.back() == L' ') s.remove_suffix(1);
        return s;
        };

    titlePartRaw = trimSpaces(titlePartRaw);
    if (titlePartRaw.empty()) return true;

    auto startsWithNoCase = [](std::wstring_view s, std::wstring_view p) -> bool {
        return (s.length() > p.length() && _wcsnicmp(s.data(), p.data(), p.length()) == 0);
        };

    if (startsWithNoCase(titlePartRaw, prefixRegex)) {

        try {
            // regex
            std::wstring_view patternView = trimSpaces(titlePartRaw.substr(prefixRegex.length()));
            if (patternView.empty()) return true;

            std::wstring patternStr(patternView);
            std::wregex pattern(patternStr);
            return std::regex_search(windowName, pattern);
        }
        catch (...) {
            return false;
        }
    }

    // exact (case-sensitive substring)
    return windowName.find(titlePartRaw) != std::wstring::npos;
}

bool CheckAppMatch(const wchar_t* rawEntry, const std::wstring& processName, const std::wstring& windowName, bool isStandard, AppMatchResult& result) {
    if (rawEntry[0] == L'\0') return false;

    std::wstring_view entry(rawEntry);

    auto trimSpaces = [](std::wstring_view s) -> std::wstring_view {
        while (!s.empty() && s.front() == L' ') s.remove_prefix(1);
        while (!s.empty() && s.back() == L' ') s.remove_suffix(1);
        return s;
        };

    entry = trimSpaces(entry);

    // Be forgiving if the user accidentally prefixes the whole line.
    // Expected syntax is: "<process>.exe [titlePart|regex:<pattern>]".
    std::wstring_view prefixRegex = L"regex:";
    if (entry.length() > prefixRegex.length() && _wcsnicmp(entry.data(), prefixRegex.data(), prefixRegex.length()) == 0) {
        entry = trimSpaces(entry.substr(prefixRegex.length()));
    }

    if (isStandard) {
        bool useWindowName = (UseWindowName.find(processName) != UseWindowName.end());
        std::wstring matchTarget = useWindowName ? windowName : processName;

        if (!useWindowName) {
            if (entry.length() >= processName.length()) {
                if (_wcsnicmp(entry.data(), processName.c_str(), processName.length()) != 0) return false;
            }
            else {
                return false;
            }
        }

        std::wstring appNameEntryStr(entry);
        std::wstring appName = GetCleanAppName(appNameEntryStr);

        size_t firstSpace = appName.find(L' ');
        std::wstring firstWord = (firstSpace == std::wstring::npos) ? appName : appName.substr(0, firstSpace);

        bool firstWordHasExtension = (firstWord.find(L'.') != std::wstring::npos);

        if (firstWordHasExtension) {
            if (useWindowName) return false;

            std::wstring titlePart = (firstSpace != std::wstring::npos) ? appName.substr(firstSpace + 1) : L"";
            if (firstWord == L"thunderbird.exe" && titlePart.empty()) {
                titlePart = L" - Mozilla Thunderbird";
            }

            if (processName == firstWord) {
                if (CheckTitleMatch(windowName, titlePart)) {
                    result.matched = true;
                    return true;
                }
            }
        }
        else {
            if (useWindowName) {
                if (matchTarget.find(appName) != std::wstring::npos) {
                    result.matched = true;
                    return true;
                }
            }
        }
        return false;
    }

    // Is graphical
    if (entry.length() < processName.length()) return false;
    if (_wcsnicmp(entry.data(), processName.c_str(), processName.length()) != 0) return false;

    if (entry.length() > processName.length() && entry[processName.length()] != L' ') return false;

    std::wstring_view remainder;
    if (entry.length() > processName.length()) {
        remainder = entry.substr(processName.length());
        size_t firstNonSpace = remainder.find_first_not_of(L" ");
        if (firstNonSpace != std::wstring_view::npos) {
            remainder = remainder.substr(firstNonSpace);
        }
        else {
            remainder = std::wstring_view();
        }
    }

    std::wstring_view titlePart = remainder;
    int w = 0, h = 0;

    if (ParseDimensions(remainder, w, h, titlePart)) {
        result.customWidth = w;
        result.customHeight = h;
    }

    if (!CheckTitleMatch(windowName, titlePart)) {
        return false;
    }

    result.matched = true;
    return true;
}

bool MatchesStandardApp(const std::wstring& processName, const std::wstring& windowNameStr) {
    if (processName == NAME L".exe" && windowNameStr == NAME) {
        return true;
    }

    AppMatchResult result;
    for (int i = 0; i < _pSharedData->standardCount; i++) {
        if (CheckAppMatch(_pSharedData->standardApps[i], processName, windowNameStr, true, result)) {
            return true;
        }
    }
    return false;
}

bool MatchesgraphicalApp(const std::wstring& processName, const std::wstring& windowName, int& customWidth, int& customHeight) {
    customWidth = 0;
    customHeight = 0;

    if (_pSharedData) {
        AppMatchResult result;
        for (int i = 0; i < _pSharedData->graphicalCount; i++) {
            if (CheckAppMatch(_pSharedData->graphicalApps[i], processName, windowName, false, result)) {
                customWidth = result.customWidth;
                customHeight = result.customHeight;
                return true;
            }
        }
    }
    return false;
}

bool DLLIMPORT appCheck(HWND hwnd, bool RClick) {
    if (!AccessSharedMemory() || !_pSharedData)
        return false;

    std::wstring processName, windowName;
    if (IsExcluded(hwnd, processName, windowName)) {
        return false;
    }

    if (RClick) {
        return true;
    }

    return MatchesStandardApp(processName, windowName);
}


LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0)
        return CallNextHookEx(_hMouse, nCode, wParam, lParam);

    MOUSEHOOKSTRUCT* info = reinterpret_cast<MOUSEHOOKSTRUCT*>(lParam);
    if (!info)
        return CallNextHookEx(_hMouse, nCode, wParam, lParam);

    bool isRight = (wParam == WM_NCRBUTTONDOWN || wParam == WM_NCRBUTTONUP);

    if (!appCheck(info->hwnd, isRight)) {
        return CallNextHookEx(_hMouse, nCode, wParam, lParam);
    }

    if ((wParam == WM_NCLBUTTONDOWN) || (wParam == WM_NCLBUTTONUP) || (wParam == WM_NCRBUTTONDOWN) || (wParam == WM_NCRBUTTONUP)) {
        if (info->wHitTestCode != HTCLIENT) {
            const BOOL isHitMin = (info->wHitTestCode == HTMINBUTTON);
            const BOOL isHitMax = (info->wHitTestCode == HTMAXBUTTON);
            const BOOL isHitX = (info->wHitTestCode == HTCLOSE);

            if (((wParam == WM_NCLBUTTONDOWN) || (wParam == WM_NCRBUTTONDOWN)) && (isHitMin || isHitMax || isHitX)) {
                if (!isRight && isHitMax) {
                    _hLastHit = NULL;
                }
                else {
                    _hLastHit = info->hwnd;
                    if (ActivateWindow(info->hwnd))
                        return 1;
                }
            }
            else if (((wParam == WM_NCLBUTTONUP) || (wParam == WM_NCRBUTTONUP)) && (isHitMin || isHitMax || isHitX)) {
                if (info->hwnd == _hLastHit) {
                    if (SendWindowAction(info->hwnd, isHitMin, isHitMax, isRight)) {
                        _hLastHit = NULL;
                        return 1;
                    }
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

float GetWindowDpiScale(HWND hwnd) {
    UINT dpiX = 96, dpiY = 96;
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor) {
        GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    }
    if (dpiY == 0) dpiY = 96;
    return dpiY / 96.0f;
}

static HWND _lastCachedHwnd = NULL;
static bool _lastCachedResult = false;
static int _lastCachedWidth = 0;
static int _lastCachedHeight = 0;
static std::wstring _lastCachedProcess;

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

        bool shouldProcess = false;
        int customWidth = 0, customHeight = 0;
        std::wstring processName;

        if (hwnd == _lastCachedHwnd && _lastCachedHwnd != NULL) {
            if (!_lastCachedResult) {
                return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);
            }
            customWidth = _lastCachedWidth;
            customHeight = _lastCachedHeight;
            processName = _lastCachedProcess;
            shouldProcess = true;
        }
        else {
            std::wstring windowName;
            if (!IsExcluded(hwnd, processName, windowName)) {
                if (!processName.empty() && MatchesgraphicalApp(processName, windowName, customWidth, customHeight)) {
                    shouldProcess = true;
                }
            }

            _lastCachedHwnd = hwnd;
            _lastCachedResult = shouldProcess;
            _lastCachedWidth = customWidth;
            _lastCachedHeight = customHeight;
            _lastCachedProcess = shouldProcess ? processName : L"";
        }

        if (!shouldProcess) {
            return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);
        }

        TITLEBARINFOEX tbi;
        tbi.cbSize = sizeof(TITLEBARINFOEX);
        DWORD_PTR result;
        LRESULT queryResult = SendMessageTimeout(hwnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&tbi,
            SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT,
            50, &result);

        bool isHitMin = false;
        bool isHitMax = false;
        bool isHitX = false;
        bool useTitleBarInfo = (queryResult != 0);

        if (useTitleBarInfo) {
            if (tbi.rgrect[2].right > tbi.rgrect[2].left && PtInRect(&tbi.rgrect[2], pt)) isHitMin = true;
            if (tbi.rgrect[3].right > tbi.rgrect[3].left && PtInRect(&tbi.rgrect[3], pt)) isHitMax = true;
            if (tbi.rgrect[5].right > tbi.rgrect[5].left && PtInRect(&tbi.rgrect[5], pt)) isHitX = true;
        }

        if ((!isHitMin && !isHitMax && !isHitX) || (customWidth > 0)) {
            RECT windowRect;
            if (!GetWindowRect(hwnd, &windowRect))
                return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);

            float dpiScale = GetWindowDpiScale(hwnd);
            int windowWidth = windowRect.right - windowRect.left;
            int relativeX = pt.x - windowRect.left;
            int relativeY = pt.y - windowRect.top;
            int titleBarHeight, buttonWidth;

            if (customWidth > 0 && customHeight > 0) {
                titleBarHeight = (int)(customHeight * dpiScale);
                buttonWidth = (int)(customWidth * dpiScale);
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
                titleBarHeight = (int)((captionHeight + frameHeight + borderPadding * 2 + offsetY) * dpiScale);
                buttonWidth = (int)(titleBarHeight * aspectRatio);
            }

            int closeButtonLeft = windowWidth - buttonWidth;
            int maximizeButtonLeft = closeButtonLeft - buttonWidth;
            int minimizeButtonLeft = maximizeButtonLeft - buttonWidth;

            if (relativeY < titleBarHeight) {
                if (relativeX >= closeButtonLeft) isHitX = true;
                else if (relativeX >= maximizeButtonLeft) isHitMax = true;
                else if (relativeX >= minimizeButtonLeft) isHitMin = true;
            }
        }

        bool isRight = (wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP);
        bool isDown = (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN);
        bool isUp = (wParam == WM_LBUTTONUP || wParam == WM_RBUTTONUP);

        if (isHitX || isHitMin || isHitMax) {
            if (isDown) {
                if (!isRight && isHitMax) {
                    _hLastHit = NULL;
                }
                else {
                    _hLastHit = hwnd;
                    if (ActivateWindow(hwnd)) return 1;
                }
            }
            else if (isUp && hwnd == _hLastHit) {
                if (SendWindowAction(hwnd, isHitMin, isHitMax, isRight)) {
                    _hLastHit = NULL;
                    return 1;
                }
                _hLastHit = NULL;
            }
        }
        else if (isUp) {
            if (hwnd == _hLastHit) _hLastHit = NULL;
        }
    }

    return CallNextHookEx(_hLLMouse, nCode, wParam, lParam);
}

BOOL DLLIMPORT RegisterHook(HMODULE hLib) {
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
