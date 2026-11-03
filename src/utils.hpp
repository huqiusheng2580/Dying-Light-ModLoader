//
// Created by Seamus on 29/06/2026.
//
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <stdio.h>
#include <vector>
#include <filesystem>
#include <iostream>

namespace utils {
    inline void InitConsole()
    {
        AllocConsole();

        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$", "r", stdin);
    }

    inline uintptr_t FindPattern(HMODULE hModule, const char* signature)
    {
        static auto patternToByte = [](const char* pattern) {
            auto bytes = std::vector<int>{};
            auto start = const_cast<char*>(pattern);
            auto end = const_cast<char*>(pattern) + strlen(pattern);

            for (auto current = start; current < end; ++current) {
                if (*current == '?') {
                    ++current;
                    if (*current == '?') ++current;
                    bytes.push_back(-1);
                }
                else {
                    bytes.push_back(strtoul(current, &current, 16));
                }
            }
            return bytes;
        };

        auto dosHeader = (PIMAGE_DOS_HEADER)hModule;
        auto ntHeaders = (PIMAGE_NT_HEADERS)((std::uint8_t*)hModule + dosHeader->e_lfanew);

        auto sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
        auto patternBytes = patternToByte(signature);
        auto scanBytes = reinterpret_cast<std::uint8_t*>(hModule);

        auto s = patternBytes.size();
        auto d = patternBytes.data();

        for (auto i = 0ul; i < sizeOfImage - s; ++i) {
            bool found = true;
            for (auto j = 0ul; j < s; ++j) {
                if (scanBytes[i + j] != d[j] && d[j] != -1) {
                    found = false;
                    break;
                }
            }
            if (found) return reinterpret_cast<uintptr_t>(&scanBytes[i]);
        }
        return 0;
    }

    inline void PatchMemory(uintptr_t address, const std::vector<uint8_t>& bytes)
    {
        DWORD oldProtect;
        VirtualProtect((LPVOID)address, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect);

        memcpy((void*)address, bytes.data(), bytes.size());

        VirtualProtect((LPVOID)address, bytes.size(), oldProtect, &oldProtect);
    }

    inline std::string GetGameDir()
    {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path().string();
    }

    //build a path from a UTF-8 narrow string (config values may contain non-ASCII chars)
    inline std::filesystem::path PathFromUtf8(const std::string& s)
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (len <= 1) return std::filesystem::path(s);
        std::wstring wide(static_cast<size_t>(len - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, wide.data(), len);
        return std::filesystem::path(wide);
    }

    //convert an ANSI (ACP) narrow string to UTF-8
    inline std::string ToUtf8(const std::string& s)
    {
        int wlen = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
        if (wlen <= 1) return s;
        std::wstring wide(static_cast<size_t>(wlen - 1), L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, wide.data(), wlen);

        int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 1) return s;
        std::string utf8(static_cast<size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), len, nullptr, nullptr);
        return utf8;
    }
}
