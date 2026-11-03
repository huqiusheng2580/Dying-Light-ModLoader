//
// Created by Seamus on 5/06/2026.
//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>

#include "utils.hpp"
#include "config.hpp"
#include "multiplayerHooks.hpp"
#include "hooks.hpp"

typedef DWORD (WINAPI *XInputGetCapabilities_t)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI *XInputSetState_t)(DWORD, XINPUT_VIBRATION*);

XInputGetCapabilities_t oXInputGetCapabilities = nullptr;
XInputGetState_t oXInputGetState = nullptr;
XInputSetState_t oXInputSetState = nullptr;
HMODULE hOriginalDll = nullptr;

//load the real system DLL safely
void loadOriginalXinput() {
    char sysPath[MAX_PATH];
    GetSystemDirectoryA(sysPath, MAX_PATH);
    std::string realDllPath = std::string(sysPath) + "\\xinput1_3.dll";

    hOriginalDll = LoadLibraryA(realDllPath.c_str());
    if (hOriginalDll) {
        oXInputGetCapabilities = (XInputGetCapabilities_t)GetProcAddress(hOriginalDll, "XInputGetCapabilities");
        oXInputGetState = (XInputGetState_t)GetProcAddress(hOriginalDll, "XInputGetState");
        oXInputSetState = (XInputSetState_t)GetProcAddress(hOriginalDll, "XInputSetState");
    }
}

extern "C" DWORD WINAPI Proxy_XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities) {
    if (!oXInputGetCapabilities)
        return ERROR_DEVICE_NOT_CONNECTED;
    return oXInputGetCapabilities(dwUserIndex, dwFlags, pCapabilities);
}

extern "C" DWORD WINAPI Proxy_XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
    if (!oXInputGetState)
        return ERROR_DEVICE_NOT_CONNECTED;
    DWORD result = oXInputGetState(dwUserIndex, pState);
    return result;
}

extern "C" DWORD WINAPI Proxy_XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration) {
    if (!oXInputSetState)
        return ERROR_DEVICE_NOT_CONNECTED;
    return oXInputSetState(dwUserIndex, pVibration);
}

bool isDebug()
{
    return config::getBool(config::Get(), "general", "debug", false);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        loadOriginalXinput();
        //while( !::IsDebuggerPresent() )
            //::Sleep( 100 );
        if (isDebug()) {
            utils::InitConsole();
        }
        multiplayer::Init();
        hooks::Init();
    }

    return TRUE;
}
