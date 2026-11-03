//
// Created by Seamus on 29/06/2026.
//
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdio>
#include <print>
#include "SafetyHook.hpp"
#include "safetyhook_easy_shim.hpp"

namespace multiplayer {
    inline safetyhook::InlineHook hFs_check_zip_crc;
    inline safetyhook::InlineHook hAreDataAuthenticated;
    inline safetyhook::InlineHook hCalc_file_crc;

    inline bool __fastcall hkFs_check_zip_crc(void* _this){
        std::println("check_zip_crc called");
        return true;
    }

    inline bool __fastcall hkAreDataAuthenticated(void* _this){
        std::println("AreDataAuthenticatedToPlayMultiplayer called");
        return true;
    }

    inline bool __cdecl hkCalc_file_crc(void* args){
        std::println("calc_file_crc called");
        return true;
    }

    inline bool Init()
    {
        HMODULE hFs = GetModuleHandleA("filesystem_x64_rwdi.dll");
        HMODULE hEngine = GetModuleHandleA("engine_x64_rwdi.dll");
        if (!hFs || !hEngine) {
            printf("Game DLLs not found!\n");
            return false;
        }

        FARPROC pCalcCrc = GetProcAddress(hFs, "?calc_file_crc@fs@@YA_NAEAUCrcCalcArgs@1@@Z");
        if (pCalcCrc) {
            hCalc_file_crc = safetyhook::create_inline(reinterpret_cast<void*>(pCalcCrc), reinterpret_cast<void*>(&hkCalc_file_crc));
            std::println("Hooked calc_file_crc");
        }

        FARPROC pAuthCheck = GetProcAddress(hEngine, "?AreDataAuthenticatedToPlayMultiplayer@IGame@@QEBA_NXZ");
        if (pAuthCheck) {
            hAreDataAuthenticated = safetyhook::create_inline(reinterpret_cast<void*>(pAuthCheck), reinterpret_cast<void*>(&hkAreDataAuthenticated));
            std::println("Hooked AreDataAuthenticatedToPlayMultiplayer");
        }

        FARPROC pCrcCheck = GetProcAddress(hFs, "?check_zip_crc@izipped_buffer_file@fs@@UEAA_NXZ");
        if (pCrcCheck) {
            hFs_check_zip_crc = safetyhook::create_inline(reinterpret_cast<void*>(pCrcCheck), reinterpret_cast<void*>(&hkFs_check_zip_crc));
            std::println("Hooked check_zip_crc");
        }

        return true;
    }
}
