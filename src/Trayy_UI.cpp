#include "Trayy.h"
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"
#include <d3d11.h>
#include <vector>
#include <algorithm>
#include <string>
#include <mutex>
#include <cstring>
#include <Windows.h>
#include <thread>
#include <Shellscalingapi.h>
#include <cmath>

int DESKTOP_PADDING = 15;
static inline float DPI_SCALE = 1.0f;
static inline int WINDOW_WIDTH = 333;
static inline int WINDOW_HEIGHT = 500;
static inline int BUTTON_HEIGHT = 45;
static inline int MODIFY_BUTTON_WIDTH = 22;
static constexpr int BASE_WINDOW_WIDTH = 300;
static constexpr int BASE_WINDOW_HEIGHT = 500;
static constexpr const char* DEFAULT_FONT_PATH = "C:\\Windows\\Fonts\\segoeui.ttf";
static constexpr const char* DEFAULT_BFONT_PATH = "C:\\Windows\\Fonts\\segoeuib.ttf";
static inline int FONT_SIZE = 18;
static constexpr int IMGUI_TIMER_MS = 16; // ~60 FPS

float GetWindowDpiScale(HWND hwnd) {
    UINT dpiX = 96, dpiY = 96;
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor) {
    }
    GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    if (dpiY == 0) dpiY = 96;
    return dpiY / 96.0f;
}

// DirectX11 data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Protects the UI-side cache of app names when updated from the UI thread
static std::mutex g_appCacheMutex;

// UI State
static bool g_ImGuiInitialized = false;
static std::vector<std::pair<std::wstring, std::string>> g_appListCache; // Cache converted strings
static bool g_appListDirty = true; // Flag to update cache when needed
static float g_scrollVelocity = 0.0f;

// Note: hwndMain is defined in Trayy.cpp; we use the extern from Trayy.h

HWND CreateMainWindow(HINSTANCE hInstance) {
    RECT rect;
    HWND taskbar = FindWindow(L"Shell_traywnd", NULL);
    if (!taskbar) {
        // Fallback to centering on screen if taskbar isn't found
        rect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    }
    else {
        GetWindowRect(taskbar, &rect);
    }
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
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); // Set default arrow cursor
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = NAME;

    if (!RegisterClass(&wc)) {
        MessageBox(NULL, L"Error creating window class", NAME, MB_OK | MB_ICONERROR);
        return NULL;
    }


    HWND hwndMain = CreateWindowEx(WS_EX_TOPMOST, NAME, NAME, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION,
        x, y, WINDOW_WIDTH, WINDOW_HEIGHT, hwndBase, NULL, hInstance, NULL);

    if (!hwndMain) {
        MessageBox(NULL, L"Error creating main window", NAME, MB_OK | MB_ICONERROR);
        return NULL;
    }

    return hwndMain;
}

static void StartImGuiTimer() {
    if (hwndMain) {
        SetTimer(hwndMain, IMGUI_TIMER_ID, IMGUI_TIMER_MS, NULL);
    }
}
static void StopImGuiTimer() {
    if (hwndMain) {
        KillTimer(hwndMain, IMGUI_TIMER_ID);
    }
}

void InitializeUI(HINSTANCE hInstance) {
    DPI_SCALE = GetWindowDpiScale(hwndMain);
    WINDOW_WIDTH *= DPI_SCALE;
    WINDOW_HEIGHT *= DPI_SCALE;
    DESKTOP_PADDING *= DPI_SCALE;
    BUTTON_HEIGHT *= DPI_SCALE;
    MODIFY_BUTTON_WIDTH *= DPI_SCALE;
    FONT_SIZE *= DPI_SCALE;

    hwndBase = CreateWindowEx(0, L"STATIC", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    hwndMain = CreateMainWindow(hInstance);
    if (hwndMain) {
        if (!InitializeImGui(hwndMain)) {
            MessageBox(NULL, L"Error initializing UI", NAME, MB_OK | MB_ICONERROR);
            return;
        }
    }
    MinimizeWindowToTray(hwndMain);
    StopImGuiTimer();
}

void ExecuteMenu() {
    HMENU hMenu;
    POINT point;

    hMenu = CreatePopupMenu();
    if (!hMenu) {
        MessageBox(NULL, L"Error creating menu.", L"Trayy", MB_OK | MB_ICONERROR);
        return;
    }

    if (hwndForMenu == hwndMain) {
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

    SetForegroundWindow(hwndMain);
    TrackPopupMenu(hMenu, TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RIGHTALIGN | TPM_BOTTOMALIGN, point.x, point.y, 0, hwndMain, NULL);

    PostMessage(hwndMain, WM_USER, 0, 0);
    DestroyMenu(hMenu);
}

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &out[0], size, nullptr, nullptr);
    out.resize(static_cast<size_t>(size - 1));
    return out;
}

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (size <= 1) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &out[0], size);
    out.resize(static_cast<size_t>(size - 1));
    return out;
}

// set the cursor position to the end of the buffer
static int InputText_SetCursorToEndCallback(ImGuiInputTextCallbackData* data) {
    if (!data) return 0;
    bool* pFlag = reinterpret_cast<bool*>(data->UserData);
    if (pFlag && *pFlag && data->Buf) {
        int len = static_cast<int>(std::strlen(data->Buf));
        data->CursorPos = len;
        data->SelectionStart = data->CursorPos;
        data->SelectionEnd = data->CursorPos;
        *pFlag = false;
    }
    return 0;
}

// Detect dark mode
static bool SysDarkModeEnabled() {
    BOOL isDark = FALSE;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD value = 0;
        DWORD valueSize = sizeof(value);
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &valueSize) == ERROR_SUCCESS) {
            isDark = (value == 0);
        }
        RegCloseKey(hKey);
    }
    return isDark == TRUE;
}

void CreateRenderTarget() {
    if (!g_pSwapChain || !g_pd3dDevice) {
        return; // nothing to do
    }
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr) || pBackBuffer == nullptr) {
        // Unable to obtain back buffer; bail out safely
        return;
    }
    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) {
        // If creation failed ensure view pointer remains null
        if (g_mainRenderTargetView) {
            g_mainRenderTargetView->Release();
            g_mainRenderTargetView = nullptr;
        }
    }
}

// DirectX11 setup functions
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (FAILED(res) || !g_pSwapChain || !g_pd3dDevice || !g_pd3dDeviceContext) {
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void HandleUpdateButtonClick(HWND hwnd) {
    std::thread([] {
        CheckAndUpdate(VERSION);
        }).detach();
}

// Small helpers to reduce repetition in ImGui code
static void PushGreenButtonStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
    // Use the window background color for button text so it adapts to light/dark mode
    ImVec4 textCol = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
}

static void PushRedButtonStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    ImVec4 textCol = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
}

static void PushBlueButtonStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.26f, 0.59f, 0.98f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.69f, 0.98f, 1.0f));
    ImVec4 textCol = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
}

static void PushDarkBlueButtonStyle() {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.44f, 0.78f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.52f, 0.90f, 1.0f));
    ImVec4 textCol = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    ImGui::PushStyleColor(ImGuiCol_Text, textCol);
}

static void PopButtonStyle() { ImGui::PopStyleColor(3); }

static void ReplaceAppName(const std::wstring& oldName, const std::wstring& newName, bool preserveSpecialFlag) {
    if (oldName == newName || newName.empty()) return;
    bool wasSpecial = (specialAppNames.find(oldName) != specialAppNames.end());
    appNames.erase(oldName);
    specialAppNames.erase(oldName);
    appNames.insert(newName);
    if (preserveSpecialFlag && wasSpecial) specialAppNames.insert(newName);
    MarkAppListDirty();
}

static void InsertAppFromBuffer(const char* buf) {
    if (!buf || buf[0] == '\0') return;
    std::wstring w = Utf8ToWide(std::string(buf));
    if (!w.empty()) {
        appNames.insert(w);
        MarkAppListDirty();
    }
}

bool InitializeImGui(HWND hwnd) {
    std::wstring debugStr = std::to_wstring(DPI_SCALE);
    OutputDebugStringW(debugStr.c_str());


    // DirectX
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        return false;
    }

    // ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;
    io.IniFilename = nullptr; // Disable imgui.ini file

    // ImGui style
    if (SysDarkModeEnabled()) {
        ImGui::StyleColorsDark();
    }
    else {
        ImGui::StyleColorsLight();
    }
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.Colors[ImGuiCol_Button] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
    float fontSize = FONT_SIZE;
    style.FontSizeBase = fontSize;

    ImFont* mainFont = io.Fonts->AddFontFromFileTTF(DEFAULT_FONT_PATH, fontSize);
    if (mainFont) {
        io.FontDefault = mainFont;
    }

    // Setup backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    g_ImGuiInitialized = true;
    MarkAppListDirty(); // Initial cache update

    // Set idle refresh timer
    StartImGuiTimer();

    return true;
}

void CleanupImGui() {
    if (g_ImGuiInitialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_ImGuiInitialized = false;
    }
    if (hwndMain) {
        KillTimer(hwndMain, IMGUI_TIMER_ID);
    }

    CleanupDeviceD3D();
}

void UpdateAppListCache() {
    std::lock_guard<std::mutex> lk(g_appCacheMutex);
    g_appListCache.clear();

    std::vector<std::wstring> sortedApps(appNames.begin(), appNames.end());
    std::sort(sortedApps.begin(), sortedApps.end());

    g_appListCache.reserve(sortedApps.size());
    for (const auto& wideStr : sortedApps) {
        std::string utf8Str = WideToUtf8(wideStr);
        // Do not modify visible name; mode is controlled by the toggle button in the list
        g_appListCache.emplace_back(wideStr, std::move(utf8Str));
    }

    g_appListDirty = false;
}


void MarkAppListDirty() {
    std::lock_guard<std::mutex> lk(g_appCacheMutex);
    g_appListDirty = true;
}

void RenderImGuiFrame() {
    // Handle resize
    if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
        CleanupRenderTarget();
        if (g_pSwapChain) {
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
        }
        g_ResizeWidth = g_ResizeHeight = 0;
        CreateRenderTarget();
    }

    // Update app list cache only when needed
    if (g_appListDirty) {
        UpdateAppListCache();
    }

    // Apply smooth scrolling
    ImGuiIO& io = ImGui::GetIO();
    if (std::abs(g_scrollVelocity) > 0.001f) {
        float step = g_scrollVelocity * 0.15f; // Adjust smoothing speed here
        if (std::abs(step) < 0.01f) step = (g_scrollVelocity > 0) ? 0.01f : -0.01f; // Min speed
        if (std::abs(step) > std::abs(g_scrollVelocity)) step = g_scrollVelocity; // Don't overshoot

        io.MouseWheel += step;
        g_scrollVelocity -= step;
    }
    else {
        g_scrollVelocity = 0.0f;
    }

    // Start the ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Render the main UI window
    RenderMainUI();

    // Rendering
    ImGui::Render();
    const float clear_color[4] = { 0.94f, 0.94f, 0.94f, 1.00f }; // Light gray background
    if (g_pd3dDeviceContext && g_mainRenderTargetView) {
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    if (g_pSwapChain) {
        g_pSwapChain->Present(1, 0); // Present with vsync
    }
}

void customText(ImFont* font, const ImVec4& color, const char* text) {
    ImGui::PushFont(font);
    ImGui::TextColored(color, text);
    ImGui::PopFont();
}

void RenderMainUI() {
    static bool showHelpOverlay = false;
    ImFont* boldFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(DEFAULT_BFONT_PATH, FONT_SIZE * 1.0f);
    if (!boldFont) {
        boldFont = ImGui::GetIO().Fonts->Fonts[0];
    }
    // OutputDebugStringW(L"Rendering Main UI\n");
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    if (!ImGui::Begin("Trayy Settings", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (showHelpOverlay) {
        ImGui::Text("Help & Information");

        ImGui::SameLine();

        float rightEdge = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        const char* backLabel = "Back";
        float backWidth = ImGui::CalcTextSize(backLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorPosX(rightEdge - backWidth);

        PushRedButtonStyle();
        ImGui::PushFont(boldFont);
        if (ImGui::Button(backLabel)) {
            showHelpOverlay = false;
        }
        ImGui::PopFont();
        PopButtonStyle();

        ImGui::Separator();
        ImGui::Spacing();

        float listHeight = ImGui::GetContentRegionAvail().y - BUTTON_HEIGHT; // Leave space for buttons
        static const ImVec4 ColorText = ImVec4(0.2f, 0.7f, 0.2f, 1.0f);
        if (ImGui::BeginChild("HelpScrollRegion", ImVec2(0, listHeight), false)) {
            ImVec4 highlightColor = ColorText;

            customText(boldFont, ColorText, "Do not show on Taskbar:");
            ImGui::TextWrapped("Listed app will not appear on the Windows taskbar.");
            ImGui::Dummy(ImVec2(0, 5));

            customText(boldFont, ColorText, "Send to Tray also when Closed:");
            ImGui::TextWrapped("Clicking the X button will minimize the app to the system tray instead of closing it.");
            ImGui::Dummy(ImVec2(0, 5));

            customText(boldFont, ColorText, "App List Rules:");

            ImGui::Bullet(); ImGui::Text("No extension:");
            ImGui::SameLine(); customText(boldFont, highlightColor, "WhatsApp Web");
            ImGui::Indent(); ImGui::TextWrapped("Matches browser tab names for web apps."); ImGui::Unindent();

            ImGui::Bullet(); ImGui::Text("With extension:");
            ImGui::SameLine(); customText(boldFont, highlightColor, "thunderbird.exe");
            ImGui::Indent(); ImGui::TextWrapped("Matches app process names as seen in Task Manager."); ImGui::Unindent();

            ImGui::Bullet(); ImGui::Text("Window name:");
            ImGui::SameLine(); customText(boldFont, highlightColor, "thunderbird.exe Write");
            ImGui::Indent(); ImGui::TextWrapped("Optional: Targets only app windows with the specific provided title for the given process."); ImGui::Unindent();

            ImGui::Bullet(); ImGui::Text("Graphical mode:");
            ImGui::SameLine(); customText(boldFont, highlightColor, "Obsidian.exe w55h50");
            ImGui::Indent(); ImGui::TextWrapped("Optional: Refines the bounding box used for Graphical mode with the given button width and height in pixels."); ImGui::Unindent();

            ImGui::Dummy(ImVec2(0, 5));

            customText(boldFont, ColorText, "Modes:");

            ImGui::Bullet(); ImGui::Text("N-Normal:");
            ImGui::Indent();
            ImGui::TextWrapped("Use Windows API to efficiently customize handling behavior when clicking control buttons on the standard title bar.");
            ImGui::Unindent();

            ImGui::Bullet(); ImGui::Text("G-Graphical:");
            ImGui::Indent();
            ImGui::TextWrapped("Intercept clicks in the top-right corner to detect control button presses. Intended for applications without standard title bars.");
            ImGui::Unindent();

            customText(boldFont, ColorText, "Quick Actions:");

            ImGui::Bullet(); ImGui::Text("Right-click Minimize:");
            ImGui::Indent();
            ImGui::TextWrapped("Send the app to tray without adding it to the app list.");
            ImGui::Unindent();

            ImGui::Bullet(); ImGui::Text("Right-click Close:");
            ImGui::Indent();
            ImGui::TextWrapped("Send the app to tray and permanently add it to the app list.");
            ImGui::Unindent();

            ImGui::Bullet(); ImGui::Text("Right-click Maximize:");
            ImGui::Indent();
            ImGui::TextWrapped("Toggle always-on-top. This action will use PowerToys if possible.");
            ImGui::Unindent();

            ImGui::Dummy(ImVec2(0, 5));
        }

        ImGui::EndChild();
        ImGui::Spacing();

        PushGreenButtonStyle();
        ImGui::PushFont(boldFont);
        if (ImGui::Button("Support the Trayy project", ImVec2(-1, -1))) {
            std::thread([] {
                ShellExecuteA(NULL, "open", "https://github.com/alirezagsm/Trayy", NULL, NULL, SW_SHOWNORMAL);
                }).detach();
        }
        ImGui::PopFont();
        ImGui::PopStyleColor(3);
        ImGui::End();
        return;
    }

    if (updateAvailable) {
        PushGreenButtonStyle();
        ImGui::PushFont(boldFont);
        if (ImGui::Button("Update Available", ImVec2(-1, 0))) HandleUpdateButtonClick(hwndMain);
        ImGui::PopFont();
        PopButtonStyle();
        ImGui::Spacing();
    }

    ImGui::Checkbox("Do not show on Taskbar", &NOTASKBAR);
    ImGui::SameLine();

    const char* helpText = " ? ";
    float helpTextWidth = ImGui::CalcTextSize(helpText).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float rightEdge = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
    ImGui::SetCursorPosX(rightEdge - helpTextWidth);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushFont(boldFont);
    if (ImGui::Button(helpText)) {
        showHelpOverlay = true;
    }
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::Checkbox("Send to Tray also when Closed", &HOOKBOTH);

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.0f));
    ImGui::PopStyleColor();
    float listHeight = ImGui::GetContentRegionAvail().y - BUTTON_HEIGHT; // Leave space for buttons

    std::vector<std::pair<std::wstring, std::string>> localCache;
    {
        std::lock_guard<std::mutex> lk(g_appCacheMutex);
        localCache = g_appListCache;
    }

    if (ImGui::BeginChild("AppList", ImVec2(-1, listHeight), true)) {
        static int editingIndex = -1;
        static char editBuffer[256] = "";
        static bool addingNew = false;
        static bool requestFocusForEdit = false;
        static bool requestFocusForNew = false;
        constexpr size_t EDIT_BUF_SZ = sizeof(editBuffer);
        float spacingWidth = 4.0f;
        float frameH = ImGui::GetFrameHeight();
        float btnWidth = (MODIFY_BUTTON_WIDTH > frameH) ? MODIFY_BUTTON_WIDTH : frameH;
        float availWidth = ImGui::GetContentRegionAvail().x;

        // render each row
        auto renderRow = [&](int i) -> bool {
            ImGui::PushID(i);
            if (editingIndex == i) {
                // selected
                float textWidthSelected = availWidth - btnWidth - 2 * spacingWidth;
                ImGui::SetNextItemWidth(textWidthSelected);
                if (requestFocusForEdit) { ImGui::SetKeyboardFocusHere(); }
                bool enterPressed = ImGui::InputText("##edit", editBuffer, EDIT_BUF_SZ,
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways,
                    InputText_SetCursorToEndCallback, &requestFocusForEdit);
                ImGui::SameLine(0, spacingWidth);
                PushGreenButtonStyle();
                if (ImGui::Button("+", ImVec2(btnWidth, btnWidth))) {
                    if (std::strlen(editBuffer) > 0) {
                        std::wstring newName = Utf8ToWide(editBuffer);
                        const std::wstring& oldName = localCache[i].first;
                        ReplaceAppName(oldName, newName, true);
                        editBuffer[0] = '\0';
                    }
                    editingIndex = -1;
                }
                PopButtonStyle();
                if (enterPressed) {
                    if (std::strlen(editBuffer) > 0) {
                        std::wstring newName = Utf8ToWide(editBuffer);
                        const std::wstring& oldName = localCache[i].first;
                        ReplaceAppName(oldName, newName, true);
                    }
                    editingIndex = -1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) editingIndex = -1;
            }
            else {
                // unselected
                ImVec2 row_start_pos = ImGui::GetCursorScreenPos();

                float textWidth = availWidth - 2 * btnWidth - 3 * spacingWidth;
                ImVec2 itemSize = ImVec2(textWidth, frameH);
                const char* label = localCache[i].second.c_str();

                // Use an InvisibleButton
                bool clicked = ImGui::InvisibleButton(label, itemSize);
                ImVec2 text_area_min = ImGui::GetItemRectMin();
                ImVec2 text_area_max = ImGui::GetItemRectMax();
                ImDrawList* draw_list = ImGui::GetWindowDrawList();

                ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
                ImVec2 text_pos = ImVec2(
                    text_area_min.x + ImGui::GetStyle().FramePadding.x,
                    text_area_min.y + (itemSize.y - label_size.y) * 0.5f
                );
                draw_list->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), label);

                if (clicked) {
                    editingIndex = i;
                    addingNew = false;
                    requestFocusForEdit = true;
                    std::string nameWithoutSpecial = localCache[i].second;
                    auto pos = nameWithoutSpecial.rfind(" *");
                    if (pos != std::string::npos && pos + 2 == nameWithoutSpecial.size())
                        nameWithoutSpecial.resize(pos);
                    strncpy_s(editBuffer, nameWithoutSpecial.c_str(), EDIT_BUF_SZ - 1);
                }

                // Draw the buttons next to the text area
                ImGui::SameLine(0, spacingWidth);
                bool isSpecial = (specialAppNames.find(localCache[i].first) != specialAppNames.end());
                if (isSpecial) {
                    PushDarkBlueButtonStyle();
                    if (ImGui::Button("G", ImVec2(btnWidth, btnWidth))) {
                        specialAppNames.erase(localCache[i].first);
                        UpdateSpecialAppsList(specialAppNames);
                        MarkAppListDirty();
                    }
                    PopButtonStyle();
                }
                else {
                    PushBlueButtonStyle();
                    if (ImGui::Button("N", ImVec2(btnWidth, btnWidth))) {
                        specialAppNames.insert(localCache[i].first);
                        UpdateSpecialAppsList(specialAppNames);
                        MarkAppListDirty();
                    }
                    PopButtonStyle();
                }

                ImGui::SameLine(0, spacingWidth);
                PushRedButtonStyle();
                bool deleteClicked = ImGui::Button("X", ImVec2(btnWidth, btnWidth));

                // Define hover zone
                ImVec2 row_end_pos = ImGui::GetItemRectMax();
                ImVec2 full_row_min = row_start_pos;
                ImVec2 full_row_max = ImVec2(row_end_pos.x, row_start_pos.y + frameH);
                PopButtonStyle();


                // Highlight
                if (ImGui::IsMouseHoveringRect(full_row_min, full_row_max)) {
                    draw_list->AddRect(text_area_min, text_area_max, ImGui::GetColorU32(ImGuiCol_HeaderHovered), ImGui::GetStyle().FrameRounding);
                }

                if (deleteClicked) {
                    const std::wstring& appToDelete = localCache[i].first;
                    RestoreWindowFromTray(appToDelete);
                    appNames.erase(appToDelete);
                    specialAppNames.erase(appToDelete);
                    UpdateSpecialAppsList(specialAppNames);
                    MarkAppListDirty();
                    editingIndex = -1;
                    ImGui::PopID();
                    return true;
                }
            }

            ImGui::PopID();
            return false;
            };

        for (int i = 0; i < (int)localCache.size(); ++i) {
            if (renderRow(i)) break;
        }

        if (addingNew) {
            ImGui::PushID("new_item");
            float textWidth = availWidth - btnWidth - 2 * spacingWidth;
            ImGui::SetNextItemWidth(textWidth);

            if (requestFocusForNew) ImGui::SetKeyboardFocusHere();

            bool enterPressed = ImGui::InputText("##new", editBuffer, EDIT_BUF_SZ,
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways,
                InputText_SetCursorToEndCallback, &requestFocusForNew);

            ImGui::SameLine(0, spacingWidth);
            PushGreenButtonStyle();
            ImGui::PushFont(boldFont);
            if (ImGui::Button("+", ImVec2(btnWidth, btnWidth))) {
                InsertAppFromBuffer(editBuffer);
                editBuffer[0] = '\0';
                addingNew = false;
            }
            ImGui::PopFont();
            PopButtonStyle();

            if (enterPressed) {
                InsertAppFromBuffer(editBuffer);
                editBuffer[0] = '\0';
                addingNew = false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                addingNew = false;
                editBuffer[0] = '\0';
            }
            ImGui::PopID();
        }
        else {
            PushGreenButtonStyle();
            ImGui::PushFont(boldFont);
            if (ImGui::Button("+", ImVec2(MODIFY_BUTTON_WIDTH, 0))) {
                addingNew = true; editingIndex = -1; editBuffer[0] = '\0'; requestFocusForNew = true;
            }
            ImGui::PopFont();
            PopButtonStyle();
            ImGui::SameLine();
            ImGui::Text("Add new application");
        }
        ImGui::Dummy(ImVec2(0, MODIFY_BUTTON_WIDTH));
    }
    ImGui::EndChild();

    ImGui::Spacing();

    PushBlueButtonStyle();
    ImGui::PushFont(boldFont);
    if (ImGui::Button("Save Settings", ImVec2(-1, -1))) {
        SaveSettings();
        RefreshTray();
        MinimizeWindowToTray(hwndMain);
        ReinstateTaskbarState();
    }
    ImGui::PopFont();
    PopButtonStyle();
    ImGui::End();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT HandleImGuiMessages(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_MOUSEWHEEL) {
        short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_scrollVelocity += (float)zDelta / WHEEL_DELTA;
        return 0; // Handle manually for smooth scrolling
    }

    LRESULT imguiResult = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    if (imguiResult) {
        // Respond to keyboard inputs immediately
        if ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
            (msg >= 0x0281 && msg <= 0x0291)) {
            RenderImGuiFrame();
        }
        else {
            // For mouse and other inputs, invalidate to render on next timer tick
            InvalidateRect(hWnd, NULL, FALSE);
        }
        return 0;
    }

    // Handle window-specific messages
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_ResizeWidth = (UINT)LOWORD(lParam);
            g_ResizeHeight = (UINT)HIWORD(lParam);
            return 0;
        }
        break;
    case WM_SHOWWINDOW:
        // wParam == TRUE -> shown; FALSE -> hidden
        if (wParam) StartImGuiTimer(); else StopImGuiTimer();
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    }
    return -1;
}
