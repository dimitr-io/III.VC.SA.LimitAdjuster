#include "Core.h"
#include "Adjusters.h"
#include <algorithm>
#include <climits>
#include <cwctype>

// Data files read by the adjuster itself (data\gtasa_vehicleAudioSettings.cfg and the fastman92 formats) are opened
// with the C++ library, which Mod Loader doesn't redirect. A mod that ships one inside "modloader" would be ignored,
// so it is looked up the way Mod Loader would pick it: the enabled mod with the highest priority that has the file
// (in a "data" folder, like the game's), otherwise the game folder.

namespace
{
    std::wstring Lower(std::wstring text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
        return text;
    }

    std::wstring Trim(const std::wstring& text)
    {
        auto start = text.find_first_not_of(L" \t\r\n");
        if (start == std::wstring::npos)
            return {};
        auto end = text.find_last_not_of(L" \t\r\n");
        return text.substr(start, end - start + 1);
    }

    // The parts of modloader\modloader.ini that decide which mod's file is used.
    struct ModLoaderConfig
    {
        bool ignoreAll = false;
        std::vector<std::wstring> ignored;                 // lower case
        std::unordered_map<std::wstring, int> priority;    // lower case mod folder -> priority (default 50)
    };

    ModLoaderConfig ReadModLoaderConfig()
    {
        ModLoaderConfig config;
        std::wifstream file(L"modloader\\modloader.ini");
        if (!file)
            return config;

        std::vector<std::pair<std::wstring, std::wstring>> lines; // section, line
        std::wstring section, line, profile = L"default";
        while (std::getline(file, line))
        {
            line = Trim(line.substr(0, line.find(L';')));
            if (line.empty())
                continue;
            if (line.front() == L'[' && line.back() == L']')
            {
                section = Lower(line.substr(1, line.size() - 2));
                continue;
            }
            lines.emplace_back(section, line);
            auto eq = line.find(L'=');
            if (section == L"folder.config" && eq != std::wstring::npos && Lower(Trim(line.substr(0, eq))) == L"profile")
                profile = Lower(Trim(line.substr(eq + 1)));
        }

        auto prefix = L"profiles." + profile + L".";
        for (auto& [sec, text] : lines)
        {
            auto eq = text.find(L'=');
            if (sec == prefix + L"config" && eq != std::wstring::npos && Lower(Trim(text.substr(0, eq))) == L"ignoreallmods")
                config.ignoreAll = Lower(Trim(text.substr(eq + 1))) == L"true";
            else if (sec == prefix + L"ignoremods")
                config.ignored.push_back(Lower(text));
            else if (sec == prefix + L"priority" && eq != std::wstring::npos)
                config.priority[Lower(Trim(text.substr(0, eq)))] = _wtoi(Trim(text.substr(eq + 1)).c_str());
        }
        return config;
    }
}

namespace limits
{
    std::filesystem::path FindDataFile(std::wstring_view relative)
    {
        std::filesystem::path wanted(relative);
        auto name = Lower(wanted.filename().wstring());
        auto folder = Lower(wanted.parent_path().filename().wstring());

        std::filesystem::path best;
        int bestPriority = INT_MIN;
        std::error_code error;
        auto config = ReadModLoaderConfig();
        if (!config.ignoreAll)
        {
            for (std::filesystem::directory_iterator mod(L"modloader", error), end; !error && mod != end; mod.increment(error))
            {
                auto modName = Lower(mod->path().filename().wstring());
                if (!mod->is_directory(error) || modName.empty() || modName[0] == L'.' ||
                    std::find(config.ignored.begin(), config.ignored.end(), modName) != config.ignored.end())
                    continue;
                auto it = config.priority.find(modName);
                int priority = it != config.priority.end() ? it->second : 50;
                std::error_code inner;
                for (std::filesystem::recursive_directory_iterator file(mod->path(), std::filesystem::directory_options::skip_permission_denied, inner), last;
                     !inner && file != last; file.increment(inner))
                {
                    if (!file->is_regular_file(inner) || Lower(file->path().filename().wstring()) != name ||
                        Lower(file->path().parent_path().filename().wstring()) != folder)
                        continue;
                    if (priority > bestPriority)
                    {
                        best = file->path();
                        bestPriority = priority;
                    }
                }
            }
        }
        if (best.empty() && GetFileAttributesW(std::wstring(relative).c_str()) != INVALID_FILE_ATTRIBUTES)
            best = wanted;
        return best;
    }
}
