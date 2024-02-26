
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

#define UNICODE
#include <windows.h>

#define MAXTRAYITEMS 64

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

#define DLLIMPORT __declspec(dllexport)

BOOL DLLIMPORT RegisterHook(HMODULE);
void DLLIMPORT UnRegisterHook();
