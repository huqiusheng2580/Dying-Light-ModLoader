//
// Created by Seamus on 3/11/2026.
//
#pragma once

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "utils.hpp"

namespace config::detail {
    inline std::string trim(const std::string& s) {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        auto begin = std::find_if(s.begin(), s.end(), notSpace);
        auto end = std::find_if(s.rbegin(), s.rend(), notSpace).base();
        if (begin >= end) return {};
        return std::string(begin, end);
    }

    //strip " ; comment" / " # comment" style inline comments (needs whitespace before the marker)
    inline std::string stripInlineComment(const std::string& s) {
        for (size_t i = 1; i < s.size(); ++i) {
            if ((s[i] == ';' || s[i] == '#') && (s[i - 1] == ' ' || s[i - 1] == '\t'))
                return trim(s.substr(0, i));
        }
        return s;
    }
}

namespace config {
    using detail::trim;
    using detail::stripInlineComment;

    struct Line {
        enum class Type { Section, KeyValue, Raw };
        Type type = Type::Raw;
        std::string section;
        std::string key;
        std::string value;
        std::string raw; //original text (kept for comments/blank lines/sections)
    };

    struct Ini {
        std::vector<Line> lines;
        std::filesystem::path path;
    };

    inline std::filesystem::path GetConfigPath() {
       return std::filesystem::path(utils::GetGameDir()) / "Game_Mods" / "CE6ML.ini";
    }

    inline Ini parse(const std::filesystem::path& path) {
        Ini ini;
        ini.path = path;

        std::ifstream in(path);
        std::string currentSection;
        std::string text;
        bool firstLine = true;
        while (std::getline(in, text)) {
            if (!text.empty() && text.back() == '\r')
                text.pop_back();
            if (firstLine) { //skip UTF-8 BOM if present
                firstLine = false;
                if (text.starts_with("\xEF\xBB\xBF"))
                    text.erase(0, 3);
            }

            Line line;
            line.raw = text;
            std::string t = trim(text);

            if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
                line.type = Line::Type::Section;
                currentSection = trim(t.substr(1, t.size() - 2));
                line.section = currentSection;
            }
            else if (!t.empty() && t.front() != ';' && t.front() != '#') {
                if (auto pos = t.find('='); pos != std::string::npos) {
                    line.type = Line::Type::KeyValue;
                    line.section = currentSection;
                    line.key = trim(t.substr(0, pos));
                    line.value = stripInlineComment(trim(t.substr(pos + 1)));
                }
            }
            ini.lines.push_back(std::move(line));
        }
        return ini;
    }

    inline bool save(const Ini& ini) {
        std::ofstream out(ini.path, std::ios::trunc);
        if (!out) return false;
        for (const auto& l : ini.lines) {
            if (l.type == Line::Type::KeyValue)
                out << l.key << " = " << l.value << '\n';
            else
                out << l.raw << '\n';
        }
        return static_cast<bool>(out);
    }

    inline std::optional<std::string> getString(const Ini& ini, const std::string& section, const std::string& key) {
        for (const auto& l : ini.lines)
            if (l.type == Line::Type::KeyValue && l.section == section && l.key == key)
                return l.value;
        return std::nullopt;
    }

    inline bool getBool(const Ini& ini, const std::string& section, const std::string& key, bool defaultValue) {
        auto v = getString(ini, section, key);
        if (!v) return defaultValue;
        std::string s = *v;
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s == "true" || s == "1" || s == "yes" || s == "on";
    }

    //returns pak names from [load_order], sorted by numeric key (non-numeric keys keep file order, at the end)
    inline std::vector<std::string> getLoadOrder(const Ini& ini) {
        std::vector<std::pair<long long, std::string>> entries;
        long long fallback = std::numeric_limits<long long>::max() / 2;
        for (const auto& l : ini.lines) {
            if (l.type != Line::Type::KeyValue || l.section != "load_order" || l.value.empty())
                continue;
            long long idx = 0;
            auto res = std::from_chars(l.key.data(), l.key.data() + l.key.size(), idx);
            if (res.ec != std::errc() || res.ptr != l.key.data() + l.key.size())
                idx = fallback++;
            entries.emplace_back(idx, l.value);
        }
        std::stable_sort(entries.begin(), entries.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<std::string> out;
        out.reserve(entries.size());
        for (auto& e : entries)
            out.push_back(std::move(e.second));
        return out;
    }

    //inserts "N = pakName" after the last entry of [load_order], preserving comments elsewhere
    inline void appendLoadOrder(Ini& ini, const std::string& pakName) {
        long long maxIdx = 0;
        int sectionLine = -1;
        int lastKv = -1;
        bool inSection = false;

        for (size_t i = 0; i < ini.lines.size(); ++i) {
            const auto& l = ini.lines[i];
            if (l.type == Line::Type::Section) {
                inSection = (l.section == "load_order");
                if (inSection) sectionLine = static_cast<int>(i);
            }
            else if (l.type == Line::Type::KeyValue && inSection) {
                lastKv = static_cast<int>(i);
                long long idx = 0;
                if (std::from_chars(l.key.data(), l.key.data() + l.key.size(), idx).ec == std::errc())
                    maxIdx = std::max(maxIdx, idx);
            }
        }

        Line nl;
        nl.type = Line::Type::KeyValue;
        nl.section = "load_order";
        nl.key = std::to_string(maxIdx + 1);
        nl.value = pakName;

        if (lastKv >= 0)
            ini.lines.insert(ini.lines.begin() + lastKv + 1, nl);
        else if (sectionLine >= 0) {
            //no entries yet: insert after the section's leading comments/blank lines
            size_t pos = static_cast<size_t>(sectionLine) + 1;
            while (pos < ini.lines.size() && ini.lines[pos].type == Line::Type::Raw)
                ++pos;
            ini.lines.insert(ini.lines.begin() + pos, nl);
        }
        else {
            Line sec;
            sec.type = Line::Type::Section;
            sec.section = "load_order";
            sec.raw = "[load_order]";
            ini.lines.push_back(sec);
            ini.lines.push_back(nl);
        }
    }

    inline void writeDefault(const std::filesystem::path& path) {
        //NB: built with UTF-8 source/execution charset so these literals are stored as UTF-8
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec); //ini may live inside the mod dir
        std::ofstream out(path, std::ios::binary);
        out << "; CE6ML 配置文件\n"
            << "; 以 ; 或 # 开头的行为注释\n"
            << "\n"
            << "[general]\n"
            << "; 是否显示调试控制台 (true/false)\n"
            << "debug = false\n"
            << "\n"
            << "[paths]\n"
            << "; mod 目录：绝对路径，或相对于游戏目录的路径\n"
            << "mod_dir = Game_Mods\n"
            << "\n"
            << "[load_order]\n"
            << "; pak 加载顺序：数字越小越先加载\n"
            << "; 1 = example.pak\n";
    }

    //lazy singleton; creates the default ini on first use if missing
    inline Ini& Get() {
        static Ini ini = [] {
            auto path = GetConfigPath();
            if (!std::filesystem::exists(path))
                writeDefault(path);
            return parse(path);
        }();
        return ini;
    }
}
