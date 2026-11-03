//
// Created by Seamus on 29/06/2026.
//
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <print>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <unordered_set>
#include "globals.hpp"
#include "SafetyHook.hpp"

#include "utils.hpp"
#include "config.hpp"

namespace loader::detail {
    inline std::filesystem::path modFolderHandler()
    {
        std::string modDirSetting = config::getString(config::Get(), "paths", "mod_dir").value_or("mods");

        std::filesystem::path modDir = utils::PathFromUtf8(modDirSetting);
        if (modDir.is_relative())
            modDir = std::filesystem::path(utils::GetGameDir()) / modDir;

        if (!std::filesystem::exists(modDir)) {
            std::filesystem::create_directories(modDir);
            MessageBoxW(nullptr, L"Created mods directory!", L"CE6ML", MB_OK);
        }

        return modDir;
    }
}

namespace loader {
    inline bool packsLoaded = false;

    inline void LoadPaks(safetyhook::InlineHook& hAddSource)
    {
        constexpr unsigned int folderFlags = 7;
        constexpr unsigned int pakFlags = 1;

        auto& ini = config::Get();
        std::filesystem::path modFolder = detail::modFolderHandler();

        std::unordered_set<std::string> loadedPaksSet;

        //load paks in the order defined by [load_order] in CE6ML.ini
        for (const auto& pakName : config::getLoadOrder(ini)) {
            std::filesystem::path pakPath = modFolder / utils::PathFromUtf8(pakName);

            if (std::filesystem::exists(pakPath) and is_regular_file(pakPath)) {
                hAddSource.call<bool>(pakPath.string().c_str(), pakFlags);
                loadedPaksSet.insert(pakName);
                std::println("Loading {} from load order", pakName);
            }
            else {
                std::println("{} Is in load order but missing from folder!", pakName);
            }
        }

        //find paks not in load order, and append them to the ini
        bool iniChanged = false;
        for (const auto& entry: std::filesystem::directory_iterator(modFolder)) {
            const auto ext = entry.path().extension().string();

            if (ext == ".pak" || ext == ".mpak")
            {
                //filenames from the fs are ANSI; the ini stores UTF-8, so normalize before comparing
                std::string fileName = utils::ToUtf8(entry.path().filename().string());

                //if it hasn't been loaded yet its a new mod
                if (!loadedPaksSet.contains(fileName)) {
                    hAddSource.call<bool>(entry.path().string().c_str(), pakFlags);
                    loadedPaksSet.insert(fileName);
                    std::println("Loading new mod {}", fileName);

                    config::appendLoadOrder(ini, fileName);
                    iniChanged = true;
                }
            }
        }
        if (iniChanged)
            config::save(ini);

        hAddSource.call<bool>(modFolder.string().c_str(), folderFlags);
    }

    inline void loadRpacks(void* pRuntime, safetyhook::InlineHook& hPackLoaderLoad){

        auto modFolder = detail::modFolderHandler();

        for (const auto& entry : std::filesystem::directory_iterator(modFolder))
        {
            if (entry.path().extension() == ".rpack")
            {
                std::string filename = entry.path().filename().string();
                std::string cleanName = entry.path().stem().string();

                if (cleanName.ends_with("_pc")) {
                    cleanName = cleanName.substr(0, cleanName.length() - 3);
                }
                std::println("Loading rpack {}", filename);

                auto result = hPackLoaderLoad.call<bool>(pRuntime, cleanName.c_str(), nullptr, EIsGlobalPack::Yes, EIsContentPack::No, EUseCachePartition::Yes, EIsCrossLevelPack::Yes, (EPackKind)0);
                if (!result) {
                    std::println("Failed to load {}", filename);
                }
            }
        }
    }
}
