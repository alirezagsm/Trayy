
// Trayy v1.0
// Copyright(C) 2024 A. Ghasemi

// Based on RBTray with the following attribution:
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

static HHOOK _hMouse = NULL;
static HWND _hLastHit = NULL;

void ActivateWindow(HWND hwnd) {
    if (GetForegroundWindow() != hwnd) {
        if (IsIconic(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
        }
        SetForegroundWindow(hwnd);
    }
}

// Works for 32-bit and 64-bit apps
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MOUSEHOOKSTRUCT* info = (MOUSEHOOKSTRUCT*)lParam;
        if ((wParam == WM_NCLBUTTONDOWN) || (wParam == WM_NCLBUTTONUP)) {
            if (info->wHitTestCode == HTCLIENT) {
            }
            else {
                BOOL isHitMin = (info->wHitTestCode == HTMINBUTTON);
                BOOL isHitX = (info->wHitTestCode == HTCLOSE);
                if ((wParam == WM_NCLBUTTONDOWN) && (isHitMin || isHitX)) {
                    _hLastHit = info->hwnd;
                    return 1;
                }
                else if ((wParam == WM_NCLBUTTONUP) && (isHitMin || isHitX)) {
                    if (info->hwnd == _hLastHit) {
                        if (isHitMin) {
                            ActivateWindow(info->hwnd);
                            PostMessage(FindWindow(NAME, NAME), WM_MIN, 0, (LPARAM)info->hwnd);
                        }
                        else {
                            ActivateWindow(info->hwnd);
                            PostMessage(FindWindow(NAME, NAME), WM_X, 0, (LPARAM)info->hwnd);
                        }
                    }
                    _hLastHit = NULL;
                    return 1;
                }
                else {
                    _hLastHit = NULL;
                }
            }
        }
        else if ((wParam == WM_LBUTTONDOWN) || (wParam == WM_RBUTTONUP)) {
            _hLastHit = NULL;
        }
    }
    return CallNextHookEx(_hMouse, nCode, wParam, lParam);
}

BOOL DLLIMPORT RegisterHook(HMODULE hLib) {
    _hMouse = SetWindowsHookEx(WH_MOUSE, (HOOKPROC)MouseProc, hLib, 0);
    if (_hMouse == NULL) {
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
}
