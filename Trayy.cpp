
// Trayy v1.0
// Copyright(C) 2024 A. Ghasemi

// Based on RBTray v4.14 with the following attribution:
// Copyright (C) 1998-2010  Nikolay Redko, J.D. Purcell
// Copyright (C) 2015 Benbuck Nason

// This program is free software : you can redistribute it and /or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.If not, see < https://www.gnu.org/licenses/>.

#include "Trayy.h"
#include <vector>
#include <string>
#include <fstream>
#include <CommCtrl.h>
#include <shellapi.h>
#include <thread>
#include <set>
#include <stdio.h>
#include <psapi.h>

#pragma comment(lib, "Gdi32.lib")

static UINT WM_TASKBAR_CREATED;

static HINSTANCE hInstance;
static HMODULE hLib;
static HWND hwndMain;
static HWND hwndItems[MAXTRAYITEMS];
static HWND hwndForMenu;

static std::vector<std::wstring> appNames;

static bool HOOKBOTH = true;
static bool NOTASKBAR = false;
static bool NOTSAKBAR_changed = false;

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

static bool AddToTray(int i) {
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

static void MinimizeWindowToTray(HWND hwnd) {
    // Don't minimize MDI child windows
    if ((UINT)GetWindowLongPtr(hwnd, GWL_EXSTYLE) & WS_EX_MDICHILD) {
        return;
    }

    // If hwnd is a child window, find parent window (e.g. minimize button in
    // Office 2007 (ribbon interface) is in a child window)
    if ((UINT)GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CHILD) {
        hwnd = GetAncestor(hwnd, GA_ROOT);
    }

    // Hide window before AddWindowToTray call because sometimes RefreshWindowInTray
    // can be called from inside ShowWindow before program window is actually hidden
    // and as a result RemoveWindowFromTray is called which immediately removes just
    // added tray icon.
    ShowWindow(hwnd, SW_HIDE);

    // Add icon to tray if it's not already there
    if (FindInTray(hwnd) == -1) {
        if (!AddWindowToTray(hwnd)) {
            // If there is something wrong with tray icon restore program window.
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

static bool RemoveWindowFromTray(HWND hwnd) {
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

static void RestoreWindowFromTray(HWND hwnd) {
    if (NOTASKBAR) {
        LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TOOLWINDOW; // Add WS_EX_TOOLWINDOW extended style
        exStyle &= ~(WS_EX_APPWINDOW); // Add WS_EX_TOOLWINDOW extended style
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
        NOTSAKBAR_changed = true;
    }
    else if (NOTSAKBAR_changed) {
        LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        exStyle &= ~(WS_EX_TOOLWINDOW); // Remove WS_EX_TOOLWINDOW extended style
        exStyle |= WS_EX_APPWINDOW; // Add WS_EX_TOOLWINDOW extended style
        SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
        NOTSAKBAR_changed = false;
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

static void CloseWindowFromTray(HWND hwnd) {
    // Use PostMessage to avoid blocking if the program brings up a dialog on exit.
    // Also, Explorer windows ignore WM_CLOSE messages from SendMessage.
    PostMessage(hwnd, WM_CLOSE, 0, 0);

    Sleep(50);
    if (IsWindow(hwnd)) {
        Sleep(50);
    }

    if (!IsWindow(hwnd)) {
        // Closed successfully
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

void ExecuteMenu() {
    HMENU hMenu;
    POINT point;

    hMenu = CreatePopupMenu();
    if (!hMenu) {
        MessageBox(NULL, L"Error creating menu.", L"Trayy", MB_OK | MB_ICONERROR);
        return;
    }
    AppendMenu(hMenu, MF_STRING, IDM_RESTORE, L"Restore");
    AppendMenu(hMenu, MF_STRING, IDM_CLOSE, L"Close");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL); //--------------
    AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"Exit Trayy");
    AppendMenu(hMenu, MF_STRING, IDM_ABOUT, L"Visit GitHub");

    GetCursorPos(&point);
    SetForegroundWindow(hwndMain);

    TrackPopupMenu(hMenu, TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RIGHTALIGN | TPM_BOTTOMALIGN, point.x, point.y, 0, hwndMain, NULL);

    PostMessage(hwndMain, WM_USER, 0, 0);
    DestroyMenu(hMenu);
}

void setLVItems(HWND hwndList) {
    ListView_DeleteAllItems(hwndList);

    LVITEM lvi;
    lvi.mask = LVIF_TEXT;
    lvi.iSubItem = 0;
    for (int i = 0; i < MAXTRAYITEMS; i++)
    {
        lvi.iItem = i;
        lvi.pszText = (LPWSTR)L"";
        ListView_InsertItem(hwndList, &lvi);
    }
    for (int i = 0; i < appNames.size(); i++)
    {
        lvi.iItem = i;
        lvi.pszText = (LPWSTR)appNames[i].c_str();
        ListView_InsertItem(hwndList, &lvi);

    }
}

bool appCheck(HWND lParam) {
    wchar_t windowName[256];
    GetWindowText(lParam, windowName, 256);

    if (!IsWindowVisible(lParam)) {
        return false;
    }

    DWORD processId;
    GetWindowThreadProcessId(lParam, &processId);

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (hProcess == NULL)
        return false;

    TCHAR processName[MAX_PATH] = L"";
    HMODULE hMod;
    DWORD cbNeeded;

    if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded))
    {
        GetModuleBaseName(hProcess, hMod, processName, sizeof(processName) / sizeof(TCHAR));
    }

    CloseHandle(hProcess);
    std::vector<std::wstring> BrowserNames = { L"chrome.exe", L"firefox.exe", L"opera.exe", L"msedge.exe", L"iexplore.exe", L"brave.exe", L"vivaldi.exe", L"chromium.exe" };
    // change to window title if it's a browser
    for (const auto& browser : BrowserNames) {
        if (wcscmp(processName, browser.c_str()) == 0) {
            wcscpy(processName, windowName);
            break;
        }
    }

    for (const auto& appName : appNames) {
        if (wcslen(appName.c_str()) > 0 && wcsstr(processName, appName.c_str()) != nullptr) {
            return true;
        }
    }
    if (wcscmp(processName, NAME L".exe") == 0 && wcscmp(windowName, NAME) == 0) {
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

void ButtonCallback(HWND hwnd)
{
    appNames.clear();
    std::wofstream file(L"apps.ini");
    std::set<std::wstring> uniqueAppNames;

    for (int i = 0; i < ListView_GetItemCount(GetDlgItem(hwnd, ID_GUI)); i++)
    {
        wchar_t buffer[256];
        ListView_GetItemText(GetDlgItem(hwnd, ID_GUI), i, 0, buffer, 256);
        if (wcslen(buffer) > 0) {
            uniqueAppNames.insert(buffer);
        }
    }
    file << "HOOKBOTH " << (HOOKBOTH ? "true" : "false") << std::endl;
    file << "NOTASKBAR " << (NOTASKBAR ? "true" : "false") << std::endl;
    for (const auto& appName : uniqueAppNames) {
        appNames.push_back(appName);
        file << appName << std::endl;
    }
    file.close();
    setLVItems(GetDlgItem(hwnd, ID_GUI));
    // MinimizeWindowToTray(hwnd);
    MinimizeAll();
    MinimizeWindowToTray(hwndMain);

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
    std::thread t([]() {
        HWND taskbar = FindWindow(L"Shell_TrayWnd", NULL);
        EnumChildWindows(taskbar, EnumChildProc, 0);
        });
    t.join();
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

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
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
            HOOKBOTH = IsDlgButtonChecked(hwnd, ID_CHECKBOX1) == BST_CHECKED;
            break;
        case ID_CHECKBOX2:
            NOTASKBAR = IsDlgButtonChecked(hwnd, ID_CHECKBOX2) == BST_CHECKED;
            break;
        case IDM_EXIT:
            SendMessage(hwnd, WM_DESTROY, 0, 0);
            break;
        case ID_BUTTON:
            ButtonCallback(hwnd);
            break;
        }
        break;
    case WM_MIN:
        if (appCheck((HWND)lParam)) {
            MinimizeWindowToTray((HWND)lParam);
        }
        else
        {
            SendMessage(GetForegroundWindow(), WM_SYSCOMMAND, SC_MINIMIZE, 0);
        }
        break;
    case WM_X:
        if (HOOKBOTH && appCheck((HWND)lParam)) {
            MinimizeWindowToTray((HWND)lParam);
        }
        else
        {
            SendMessage(GetForegroundWindow(), WM_SYSCOMMAND, SC_CLOSE, 0);
        }
        break;
    case WM_REMTRAY:
        RestoreWindowFromTray((HWND)lParam);
        break;
    case WM_REFRTRAY:
        RefreshWindowInTray((HWND)lParam);
        break;
    case WM_TRAYCMD:
        switch ((UINT)lParam) {
        case NIN_SELECT:
        {
            if (IsWindowVisible(hwndItems[wParam])) {
                if (IsTopWindow(hwndItems[wParam])) {
                    MinimizeWindowToTray(hwndItems[wParam]);
                }
                else {
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
    case WM_SYSCOMMAND:
    {
        if (wParam == SC_CLOSE) {
            SendMessage(GetForegroundWindow(), WM_SYSCOMMAND, SC_MINIMIZE, 0);
        }
        break;
    }
    case WM_NOTIFY:
    {
        switch (((LPNMHDR)lParam)->code)
        {
        case NM_DBLCLK:
        case NM_CLICK:
        {
            int index = ListView_GetNextItem(GetDlgItem(hwnd, ID_GUI), -1, LVNI_SELECTED);
            if (index >= 0)
            {
                ListView_EditLabel(GetDlgItem(hwnd, ID_GUI), index);
                SendMessage(GetDlgItem(hwnd, ID_GUI), LVM_EDITLABEL, index, 0);
            }
            break;
        }
        case LVN_ENDLABELEDIT:
        {

            NMLVDISPINFO* pDispInfo = (NMLVDISPINFO*)lParam;
            if (pDispInfo->item.pszText != NULL)
            {
                ListView_SetItemText(GetDlgItem(hwnd, ID_GUI), pDispInfo->item.iItem, 0, pDispInfo->item.pszText);
            }
            if (GetAsyncKeyState(VK_UP) & 0x8000)
            {
                ListView_EditLabel(GetDlgItem(hwnd, ID_GUI), pDispInfo->item.iItem - 1);
            }
            else if (GetAsyncKeyState(VK_DOWN) & 0x8000)
            {
                ListView_EditLabel(GetDlgItem(hwnd, ID_GUI), pDispInfo->item.iItem + 1);
            }
            break;
        }
        case LVN_KEYDOWN:
        {
            LPNMLVKEYDOWN pKey = (LPNMLVKEYDOWN)lParam;
            switch (pKey->wVKey)
            {
            case VK_DELETE:
            {
                int index = ListView_GetNextItem(GetDlgItem(hwnd, ID_GUI), -1, LVNI_SELECTED);
                if (index >= 0)
                {
                    ListView_DeleteItem(GetDlgItem(hwnd, ID_GUI), index);
                }
                break;
            }
            case VK_RETURN:
            {
                int index = ListView_GetNextItem(GetDlgItem(hwnd, ID_GUI), -1, LVNI_SELECTED);
                if (index >= 0)
                {
                    ListView_EditLabel(GetDlgItem(hwnd, ID_GUI), index);
                }
                break;
            }
            case VK_F1:
            {
                ShellExecute(NULL, L"open", ABOUT_URL, NULL, NULL, SW_SHOWNORMAL);
                break;
            }
            case VK_UP:
            {
                int index = ListView_GetNextItem(GetDlgItem(hwnd, ID_GUI), -1, LVNI_SELECTED);
                if (index > 0)
                {
                    ListView_SetItemState(GetDlgItem(hwnd, ID_GUI), index - 1, LVIS_SELECTED, LVIS_SELECTED);
                    ListView_SetItemState(GetDlgItem(hwnd, ID_GUI), index, 0, LVIS_SELECTED);

                }
                break;
            }
            case VK_DOWN:
            {
                int index = ListView_GetNextItem(GetDlgItem(hwnd, ID_GUI), -1, LVNI_SELECTED);
                if (index < ListView_GetItemCount(GetDlgItem(hwnd, ID_GUI)) - 1)
                {
                    ListView_SetItemState(GetDlgItem(hwnd, ID_GUI), index + 1, LVIS_SELECTED, LVIS_SELECTED);
                    ListView_SetItemState(GetDlgItem(hwnd, ID_GUI), index, 0, LVIS_SELECTED);

                }
                break;
            }
            }
            break;

        }
        break;
        }
    }
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
            FreeLibrary(hLib);
        }
        RefreshTray();
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

// Wait for windows startup on first launch
void MinimizeAllInBackground() {
    std::thread t([]() {
        Sleep(3000);
        MinimizeAll();
        });
    t.detach();
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /*hPrevInstance*/, _In_ LPSTR /*szCmdLine*/, _In_ int /*iCmdShow*/) {

    std::wifstream file(L"apps.ini");
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



    bool shouldExit = false;


    hwndMain = FindWindow(NAME, NAME);
    if (hwndMain) {
        if (shouldExit) {
            SendMessage(hwndMain, WM_CLOSE, 0, 0);
        }
        else {
            MessageBox(NULL, L"Trayy is already running.", NAME, MB_OK | MB_ICONINFORMATION);
        }
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

    // window parameters
    int width = 300;
    int height = 350;
    int buttonHeight = 50;
    int boxHeight = 25;
    int padding = 20;
    int nApps = 20;

    RECT rect;
    HWND taskbar = FindWindow(L"Shell_traywnd", NULL);
    GetWindowRect(taskbar, &rect);
    int x = rect.right - width - padding;
    int y;
    if (rect.top == 0) {
        y = rect.bottom + padding;
    }
    else {
        y = rect.top - height - padding;
    }


    for (int i = 0; i < MAXTRAYITEMS; i++) {
        hwndItems[i] = NULL;
    }
    WM_TASKBAR_CREATED = RegisterWindowMessage(L"TaskbarCreated");


    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wc.hCursor = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = NAME;
    if (!RegisterClass(&wc)) {
        MessageBox(NULL, L"Error creating window class", NAME, MB_OK | MB_ICONERROR);
        return 0;
    }

    // create main window
    hwndMain = CreateWindowEx(WS_EX_TOPMOST, NAME, NAME, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION, x, y, width, height, NULL, NULL, hInstance, NULL);
    if (!hwndMain) {
        MessageBox(NULL, L"Error creating window.", NAME, MB_OK | MB_ICONERROR);
        return 0;
    }

    // get real client size
    int scrollBarWidth = GetSystemMetrics(SM_CXVSCROLL);
    int scrollBarHeight = GetSystemMetrics(SM_CYHSCROLL);
    width -= scrollBarWidth;
    height -= scrollBarHeight;

    // add two checkboxes
    HWND hwndCheckbox1 = CreateWindowEx(0, L"BUTTON", L"Send to Tray also when Closed", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, width, boxHeight, hwndMain, (HMENU)ID_CHECKBOX1, hInstance, NULL);
    HWND hwndCheckbox2 = CreateWindowEx(0, L"BUTTON", L"Do not show on Taskbar", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, boxHeight, width, boxHeight, hwndMain, (HMENU)ID_CHECKBOX2, hInstance, NULL);
    SendMessage(hwndCheckbox1, BM_SETCHECK, HOOKBOTH ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(hwndCheckbox2, BM_SETCHECK, NOTASKBAR ? BST_CHECKED : BST_UNCHECKED, 0);

    // create list view
    HWND hwndList = CreateWindowEx(0, WC_LISTVIEW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_EDITLABELS | LVS_NOCOLUMNHEADER, 0, 2 * boxHeight, width, height - (buttonHeight + boxHeight + boxHeight + scrollBarHeight), hwndMain, (HMENU)ID_GUI, hInstance, NULL);

    LVCOLUMN lvc;
    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvc.fmt = LVCFMT_LEFT;
    lvc.cx = width - scrollBarWidth;
    lvc.pszText = (LPWSTR)L"";
    lvc.iSubItem = 0;
    ListView_InsertColumn(hwndList, 0, &lvc);
    setLVItems(hwndList);
    ListView_SetExtendedListViewStyle(hwndList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    // create save button
    HWND hwndButton = CreateWindowEx(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, height - (buttonHeight + scrollBarHeight), width, buttonHeight, hwndMain, (HMENU)ID_BUTTON, hInstance, NULL);

    // set colors
    COLORREF c1 = RGB(255, 255, 255);
    COLORREF c2 = RGB(20, 20, 20);

    ListView_SetBkColor(hwndList, c1);
    ListView_SetTextBkColor(hwndList, c1);
    ListView_SetTextColor(hwndList, c2);

    // set fonts
    HFONT hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Sergoe UI");
    HFONT hFont_bold = CreateFont(16, 0, 0, 0, FW_DEMIBOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Sergoe UI");
    SendMessage(hwndList, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hwndButton, WM_SETFONT, (WPARAM)hFont_bold, TRUE);
    SendMessage(hwndCheckbox1, WM_SETFONT, (WPARAM)hFont_bold, TRUE);
    SendMessage(hwndCheckbox2, WM_SETFONT, (WPARAM)hFont_bold, TRUE);

    MinimizeWindowToTray(hwndMain);
    RefreshTray();
    MinimizeAllInBackground();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
