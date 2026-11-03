//
// Created by Seamus on 29/06/2026.
//
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <print>
#include "globals.hpp"
#include "SafetyHook.hpp"
#include "safetyhook_easy_shim.hpp"

#include "loader.hpp"
#include "utils.hpp"

namespace hooks {
    inline const char* packLoaderLoadAOB = "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 41 54 41 56 48 8D 6C 24 D9 48 81 EC 90 00 00 00";
    inline const char* rpaczAOB = "85 C0 74 10 FF C7 48 83 C6 08 83 FF 02";

    inline safetyhook::InlineHook hAddSource;
    inline safetyhook::InlineHook hInitGameScript;
    inline safetyhook::InlineHook hPackLoaderLoad;

    inline bool __fastcall hkAddSource(const char* path, unsigned int flags){
        return hAddSource.call<bool>(path, flags);
    }

    inline bool __fastcall hkPackLoaderLoad(void* pRuntime, const char* path, void** outPack,
        EIsGlobalPack isGlobal, EIsContentPack isContent, EUseCachePartition isCache,
        EIsCrossLevelPack isCross, EPackKind kind)
    {
        if (!loader::packsLoaded)
            loader::loadRpacks(pRuntime, hPackLoaderLoad);

        loader::packsLoaded = true;

        return  hPackLoaderLoad.call<bool>(pRuntime, path, outPack, isGlobal, isContent, isCache, isCross, kind);
    }

    inline void __fastcall hkInitGameScript(LPCSTR scriptName, uint8_t flags){
        hInitGameScript.call<void>(scriptName, flags);
        std::println("InitializeGameScript called with params scriptName={} flags={}", scriptName, static_cast<int>(flags));
        std::println("Loading paks!");
        loader::LoadPaks(hAddSource);
    }

    inline bool Init()
    {
        HMODULE hFs = GetModuleHandleA("filesystem_x64_rwdi.dll");
        HMODULE hEngine = GetModuleHandleA("engine_x64_rwdi.dll");
        if (!hFs || !hEngine) {
            printf("Game DLLs not found!\n");
            return false;
        }

        uintptr_t patchJumpAddr = utils::FindPattern(hEngine, rpaczAOB);
        if (patchJumpAddr)
        {
            uintptr_t targetByte = patchJumpAddr + 2;
            std::vector<uint8_t> patchBytes = { 0xEB };

            utils::PatchMemory(targetByte, patchBytes);
            std::println("rpacz loading enabled! (Patched 74 -> Eb at 0x{}", targetByte);
        }
        else {
            std::println("Failed to find rpacz jmp! Can be ignored safely if on DIDE/DIRDE");
        }

        uintptr_t rpackAddr = utils::FindPattern(hEngine, packLoaderLoadAOB);
        if (rpackAddr) {
            hPackLoaderLoad = safetyhook::create_inline(reinterpret_cast<void*>(rpackAddr), reinterpret_cast<void*>(&hkPackLoaderLoad));
        }

        FARPROC pAddSource = GetProcAddress(hFs, "?add_source@fs@@YA_NPEBDW4ENUM@FFSAddSourceFlags@@@Z");
        if (pAddSource) {
            hAddSource = safetyhook::create_inline(reinterpret_cast<void*>(pAddSource), reinterpret_cast<void*>(&hkAddSource));
        }

        FARPROC pInitGameScript = GetProcAddress(hEngine, "InitializeGameScript");
        if (pInitGameScript) {
            hInitGameScript = safetyhook::create_inline(reinterpret_cast<void*>(pInitGameScript), reinterpret_cast<void*>(&hkInitGameScript));
        }

        return true;
    }
}
