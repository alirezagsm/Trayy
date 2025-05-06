#include "Trayy.h"
#include <winhttp.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <locale>
#include <codecvt>
#include <shldisp.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")

std::wstring GetLatestReleaseTag() {
    HINTERNET hSession = WinHttpOpen(L"Trayy Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return L"";

    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return L"";
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/repos/alirezagsm/Trayy/releases/latest", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"";
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"";
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"";
    }

    DWORD dwSize = 0;
    std::string response;
    do {
        WinHttpQueryDataAvailable(hRequest, &dwSize);
        if (dwSize > 0) {
            char* buffer = new char[dwSize + 1];
            ZeroMemory(buffer, dwSize + 1);
            DWORD dwDownloaded = 0;
            WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded);
            response.append(buffer, dwDownloaded);
            delete[] buffer;
        }
    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // Parse tag_name from JSON response (robust UTF-8 parsing)
    size_t tagPos = response.find("\"tag_name\":");
    if (tagPos != std::string::npos) {
        size_t start = response.find('"', tagPos + 11);
        if (start != std::string::npos) {
            start++;
            size_t end = response.find('"', start);
            if (end != std::string::npos) {
                std::string tag = response.substr(start, end - start);
                std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
                return converter.from_bytes(tag);
            }
        }
    }

    return L"";
}

void SetTrayIconUpdate() {
    NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA) };
    nid.hWnd = hwndMain;
    nid.uID = 0;
    nid.uFlags = NIF_ICON;
    nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON2));
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}


bool NeedsUpdate(const std::wstring& latestVersion) {
    auto parseVersion = [](const std::wstring& v) -> std::vector<int> {
        std::vector<int> parts;
        size_t start = (v[0] == L'v' || v[0] == L'V') ? 1 : 0;
        size_t end = v.find(L'.', start);
        while (end != std::wstring::npos) {
            parts.push_back(std::stoi(v.substr(start, end - start)));
            start = end + 1;
            end = v.find(L'.', start);
        }
        if (start < v.size())
            parts.push_back(std::stoi(v.substr(start)));
        return parts;
        };

    std::vector<int> curVer = parseVersion(VERSION);
    std::vector<int> latVer = parseVersion(latestVersion);

    // Pad shorter version with zeros
    size_t maxLen = (std::max)(curVer.size(), latVer.size());
    curVer.resize(maxLen, 0);
    latVer.resize(maxLen, 0);

    for (size_t i = 0; i < maxLen; ++i) {
        if (latVer[i] > curVer[i]) {
            return true; // Update needed
        }
        if (latVer[i] < curVer[i]) {
            return false; // Current is newer
        }
    }
    // Versions are equal
    return false;
}

void CheckForUpdates() {
    std::wstring latestVersion = GetLatestReleaseTag();
    if (latestVersion.empty()) return;
    if (NeedsUpdate(latestVersion)) {
        updateAvailable = true;
    }
}

bool DownloadLatestRelease(const std::wstring& downloadUrl, const std::wstring& outputPath) {
    HINTERNET hSession = WinHttpOpen(L"Trayy Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return false;

    URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
    wchar_t hostName[256], urlPath[1024];
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = _countof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = _countof(urlPath);

    if (!WinHttpCrackUrl(downloadUrl.c_str(), 0, 0, &urlComp)) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, urlComp.lpszHostName, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComp.lpszUrlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::ofstream outFile(outputPath, std::ios::binary);
    if (!outFile.is_open()) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD dwSize = 0;
    do {
        WinHttpQueryDataAvailable(hRequest, &dwSize);
        if (dwSize > 0) {
            char* buffer = new char[dwSize];
            DWORD dwDownloaded = 0;
            WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded);
            outFile.write(buffer, dwDownloaded);
            delete[] buffer;
        }
    } while (dwSize > 0);

    outFile.close();
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return true;
}

void PerformUpdate(const std::wstring& currentExePath, const std::wstring& downloadPath) {
    std::wstring tempDir = std::filesystem::temp_directory_path().wstring();
    std::wstring extractPath = tempDir + L"\\TrayyUpdate";
    std::wstring psScriptPath = tempDir + L"\\TrayyUpdater.ps1";
    std::wofstream script(psScriptPath);
    script << L"Expand-Archive -Path '" << downloadPath << L"' -DestinationPath '" << extractPath << L"' -Force\n";
    script << L"Start-Sleep -Seconds 2\n";
    script << L"Move-Item -Path '" << extractPath << L"\\Trayy.exe' -Destination '" << currentExePath << L"' -Force\n";
    script << L"Move-Item -Path '" << extractPath << L"\\hook.dll' -Destination '"
        << (std::filesystem::path(currentExePath).parent_path() / L"hook.dll").wstring() << L"' -Force\n";
    script << L"Start-Process -FilePath '" << currentExePath << L"'\n";
    script.close();
    std::wstring args = L"-ExecutionPolicy Bypass -NoProfile -File \"" + psScriptPath + L"\"";
    ShellExecuteW(NULL, L"open", L"powershell.exe", args.c_str(), NULL, SW_HIDE);
    SendMessage(GetForegroundWindow(), WM_SYSCOMMAND, SC_CLOSE, 0);
    ExitProcess(0);
}

void CheckAndUpdate(const std::wstring& currentVersion) {
    std::wstring latestVersion = GetLatestReleaseTag();
    if (latestVersion.empty()) {
        MessageBoxW(NULL, L"Failed to check for updates.", L"Update Checker", MB_OK | MB_ICONERROR);
        return;
    }

    if (latestVersion != currentVersion) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring downloadUrl = L"https://github.com/alirezagsm/Trayy/releases/download/" + latestVersion + L"/Trayy.zip";
        std::wstring tempDir = std::filesystem::temp_directory_path().wstring();
        std::wstring downloadPath = tempDir + L"\\Trayy.zip";

        if (DownloadLatestRelease(downloadUrl, downloadPath)) {
            PerformUpdate(exePath, downloadPath);
        }
        else {
            MessageBoxW(NULL, L"Failed to download the update.", L"Update Error", MB_OK | MB_ICONERROR);
        }
    }
    else {
        MessageBoxW(NULL, L"You are using the latest version.", L"Up to Date", MB_OK | MB_ICONINFORMATION);
    }
}