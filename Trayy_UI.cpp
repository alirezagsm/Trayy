#include "Trayy_UI.h"
#include "Trayy.h"
#include <set>
#include <fstream>
#include <CommCtrl.h>
#include <shellapi.h>

// UI parameters and constants
#define DPI_SCALE (GetDpiForWindow(GetDesktopWindow()) / 96.0)
#define WINDOW_WIDTH (int)(300 * DPI_SCALE)
#define WINDOW_HEIGHT (int)(350 * DPI_SCALE)
#define BUTTON_HEIGHT (int)(47 * DPI_SCALE)
#define BOX_HEIGHT (int)(25 * DPI_SCALE)
#define DESKTOP_PADDING (int)(20 * DPI_SCALE)
#define LEFT_PADDING (int)(5 * DPI_SCALE)
#define SAFETY_MARGIN (int)(5 * DPI_SCALE)

// Color constants
#define COLOR_TEXT_BG RGB(255, 255, 255)
#define COLOR_TEXT RGB(20, 20, 20)
#define COLOR_BG GetSysColor(COLOR_BTNFACE)

// Font constants
#define FONT_NAME L"Segoe UI"
#define FONT_SIZE_NORMAL (int)(21 * DPI_SCALE)
#define FONT_SIZE_BOLD (int)(17 * DPI_SCALE)
#define FONT_WEIGHT_NORMAL FW_NORMAL
#define FONT_WEIGHT_BOLD FW_DEMIBOLD

// Global font objects to prevent memory leaks
HFONT g_hFontNormal = NULL;
HFONT g_hFontBold = NULL;
HBRUSH g_hBackgroundBrush = NULL;

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

void HandleSaveButtonClick(HWND hwnd) {
    appNames.clear();
    std::set<std::wstring> uniqueAppNames;

    for (int i = 0; i < ListView_GetItemCount(GetDlgItem(hwnd, ID_GUI)); i++)
    {
        wchar_t buffer[256];
        ListView_GetItemText(GetDlgItem(hwnd, ID_GUI), i, 0, buffer, 256);
        if (wcslen(buffer) > 0) {
            uniqueAppNames.insert(buffer);
        }
    }
    
    for (const auto& appName : uniqueAppNames) {
        appNames.push_back(appName);
    }
    
    SaveSettings();
    setLVItems(GetDlgItem(hwnd, ID_GUI));
    MinimizeAll();
    RefreshTray();
}

void HandleListViewNotifications(HWND hwnd, LPARAM lParam) {
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
    }
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
        SendMessage(GetForegroundWindow(), WM_SYSCOMMAND, SC_CLOSE, 0);
    }
}

void HandleCheckboxClick(HWND hwnd, int checkboxId) {
    if (checkboxId == ID_CHECKBOX1) {
        HOOKBOTH = IsDlgButtonChecked(hwnd, ID_CHECKBOX1) == BST_CHECKED;
    }
    else if (checkboxId == ID_CHECKBOX2) {
        NOTASKBAR = IsDlgButtonChecked(hwnd, ID_CHECKBOX2) == BST_CHECKED;
    }
}

void UpdateUIFromSettings(HWND hwnd) {
    SendMessage(GetDlgItem(hwnd, ID_CHECKBOX1), BM_SETCHECK, HOOKBOTH ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(GetDlgItem(hwnd, ID_CHECKBOX2), BM_SETCHECK, NOTASKBAR ? BST_CHECKED : BST_UNCHECKED, 0);
    
    setLVItems(GetDlgItem(hwnd, ID_GUI));
}

HWND CreateMainWindow(HINSTANCE hInstance) {
    RECT rect;
    HWND taskbar = FindWindow(L"Shell_traywnd", NULL);
    GetWindowRect(taskbar, &rect);
    int x = rect.right - WINDOW_WIDTH - DESKTOP_PADDING;
    int y;
    if (rect.top == 0) {
        y = rect.bottom + DESKTOP_PADDING;
    }
    else {
        y = rect.top - WINDOW_HEIGHT - DESKTOP_PADDING;
    }

    // Create window class
    WNDCLASS wc = { 0 };
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
        return NULL;
    }

    // Create main window
    HWND hwndMain = CreateWindowEx(WS_EX_TOPMOST, NAME, NAME, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION, 
                                   x, y, WINDOW_WIDTH, WINDOW_HEIGHT, hwndBase, NULL, hInstance, NULL);
    
    if (!hwndMain) {
        MessageBox(NULL, L"Error creating window.", NAME, MB_OK | MB_ICONERROR);
        return NULL;
    }

    return hwndMain;
}

void SetupListView(HWND hwndList, int width) {
    LVCOLUMN lvc;
    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    lvc.fmt = LVCFMT_LEFT;
    lvc.cx = width - GetSystemMetrics(SM_CXVSCROLL);
    lvc.pszText = (LPWSTR)L"";
    lvc.iSubItem = 0;
    ListView_InsertColumn(hwndList, 0, &lvc);
    setLVItems(hwndList);
    ListView_SetExtendedListViewStyle(hwndList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
}

void SetupWindowControls(HWND hwndMain, HINSTANCE hInstance) {
    // Get real client size
    int scrollBarWidth = GetSystemMetrics(SM_CXVSCROLL);
    int scrollBarHeight = GetSystemMetrics(SM_CYHSCROLL);
    int width = WINDOW_WIDTH - scrollBarWidth + SAFETY_MARGIN;
    int height = WINDOW_HEIGHT - scrollBarHeight + SAFETY_MARGIN;

    // Add checkboxes
    HWND hwndCheckbox1 = CreateWindowEx(0, L"BUTTON", L"Send to Tray also when Closed", 
                                       WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 
                                       LEFT_PADDING, 0, width, BOX_HEIGHT, 
                                       hwndMain, (HMENU)ID_CHECKBOX1, hInstance, NULL);
    
    HWND hwndCheckbox2 = CreateWindowEx(0, L"BUTTON", L"Do not show on Taskbar", 
                                       WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 
                                       LEFT_PADDING, BOX_HEIGHT, width, BOX_HEIGHT, 
                                       hwndMain, (HMENU)ID_CHECKBOX2, hInstance, NULL);
    
    SendMessage(hwndCheckbox1, BM_SETCHECK, HOOKBOTH ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(hwndCheckbox2, BM_SETCHECK, NOTASKBAR ? BST_CHECKED : BST_UNCHECKED, 0);

    // Create list view
    HWND hwndList = CreateWindowEx(0, WC_LISTVIEW, L"", 
                                  WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_EDITLABELS | LVS_NOCOLUMNHEADER, 
                                  0, 2 * BOX_HEIGHT, width, 
                                  height - (BUTTON_HEIGHT + BOX_HEIGHT + BOX_HEIGHT + scrollBarHeight), 
                                  hwndMain, (HMENU)ID_GUI, hInstance, NULL);

    SetupListView(hwndList, width);

    // Create save button
    CreateWindowEx(0, L"BUTTON", L"Save", 
                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                 -SAFETY_MARGIN, height - (BUTTON_HEIGHT + scrollBarHeight) - SAFETY_MARGIN, 
                 width + SAFETY_MARGIN, BUTTON_HEIGHT, 
                 hwndMain, (HMENU)ID_BUTTON, hInstance, NULL);

    ApplyUIStyles(hwndMain, hwndList);
}

void ApplyUIStyles(HWND hwndMain, HWND hwndList) {
    
    // Clean up
    if (g_hBackgroundBrush) DeleteObject(g_hBackgroundBrush);
    g_hBackgroundBrush = CreateSolidBrush(COLOR_BG);
    SetClassLongPtr(hwndMain, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(g_hBackgroundBrush));
    if (g_hFontNormal) DeleteObject(g_hFontNormal);
    if (g_hFontBold) DeleteObject(g_hFontBold);

    // Set parameters
    ListView_SetBkColor(hwndList, COLOR_BG);
    ListView_SetTextBkColor(hwndList, COLOR_TEXT_BG);
    ListView_SetTextColor(hwndList, COLOR_TEXT);

    g_hFontNormal = CreateFont(
        FONT_SIZE_NORMAL, 0, 0, 0, 
        FONT_WEIGHT_NORMAL, FALSE, FALSE, FALSE, 
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, 
        DEFAULT_QUALITY, DEFAULT_PITCH, FONT_NAME);
    
    g_hFontBold = CreateFont(
        FONT_SIZE_BOLD, 0, 0, 0, 
        FONT_WEIGHT_BOLD, FALSE, FALSE, FALSE, 
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, 
        DEFAULT_QUALITY, DEFAULT_PITCH, FONT_NAME);
    
    SendMessage(GetDlgItem(hwndMain, ID_GUI), WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    SendMessage(GetDlgItem(hwndMain, ID_BUTTON), WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
    SendMessage(GetDlgItem(hwndMain, ID_CHECKBOX1), WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
    SendMessage(GetDlgItem(hwndMain, ID_CHECKBOX2), WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
}

void CleanupResources() {

    if (g_hFontNormal) {
        DeleteObject(g_hFontNormal);
        g_hFontNormal = NULL;
    }
    if (g_hFontBold) {
        DeleteObject(g_hFontBold);
        g_hFontBold = NULL;
    }

    if (g_hBackgroundBrush) {
        DeleteObject(g_hBackgroundBrush);
        g_hBackgroundBrush = NULL;
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
    wchar_t buffer[256];
    GetWindowText(hwndForMenu, buffer, 256);
    if (wcscmp(buffer, NAME) == 0) {
        AppendMenu(hMenu, MF_STRING, IDM_RESTORE, L"App List");
        AppendMenu(hMenu, MF_STRING, IDM_ABOUT, L"About");
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL); //--------------
        AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"Exit Trayy");
    }
    else {
        AppendMenu(hMenu, MF_STRING, IDM_RESTORE, L"Restore");
        AppendMenu(hMenu, MF_STRING, IDM_CLOSE, L"Close");
    }
    GetCursorPos(&point);

    SetForegroundWindow(hwndBase);
    TrackPopupMenu(hMenu, TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RIGHTALIGN | TPM_BOTTOMALIGN, point.x, point.y, 0, hwndMain, NULL);

    PostMessage(hwndMain, WM_USER, 0, 0);
    DestroyMenu(hMenu);
}

void InitializeUI(HINSTANCE hInstance) {
    hwndMain = CreateMainWindow(hInstance);
    if (hwndMain) {
        SetupWindowControls(hwndMain, hInstance);
    }
}

void ShowAppInterface(bool minimizeToTray) {
    if (minimizeToTray) {
        MinimizeWindowToTray(hwndMain);
    } else {
        ShowWindow(hwndMain, SW_SHOW);
        SetForegroundWindow(hwndMain);
    }
}
