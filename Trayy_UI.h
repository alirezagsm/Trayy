#pragma once
#include "Trayy.h"
#include <CommCtrl.h>
#include <string>

// UI
HWND CreateMainWindow(HINSTANCE hInstance);
void SetupWindowControls(HWND hwndMain, HINSTANCE hInstance);
void ApplyUIStyles(HWND hwndMain, HWND hwndList);
void SetupListView(HWND hwndList, int width);
void InitializeUI(HINSTANCE hInstance);
void ShowAppInterface(bool minimizeToTray);
void UpdateUIFromSettings(HWND hwnd);
void setLVItems(HWND hwndList);
void ExecuteMenu();
void CleanupResources();

// UI message handling
void HandleMinimizeCommand(HWND hwnd);
void HandleCloseCommand(HWND hwnd);
void HandleCheckboxClick(HWND hwnd, int checkboxId);
void HandleSaveButtonClick(HWND hwnd);
void HandleListViewNotifications(HWND hwnd, LPARAM lParam);
