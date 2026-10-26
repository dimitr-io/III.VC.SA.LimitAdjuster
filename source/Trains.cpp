#include "Core.h"
#include "Adjusters.h"
#include <sstream>

// SA trains. The vehicles of a train come from a table of 16 types of 16 model ids each (0x8D44F8): the engine and up
// to 14 carriages, ended by 0. Types 0 to 7 are the trains of the countryside track, 8 and 9 the trams; scripts create
// the others. It is read in three places as "type * 64 + table" (CTrain::CreateMissionTrain and twice in the
// generation of the trains of the tracks).
// - With data\gtasa_trainTypeCarriages.dat (the fastman92 format: one line per type, its number and the IDE names of
//   its vehicles), the table is read from the file. Types the file doesn't name keep the game's vehicles; a number
//   above 15 adds types for scripts. The names are turned into model ids when the game first reads the table, after
//   the IDE files.
// - A mission train is made of models the game expects the script to have loaded (the trains of the tracks request
//   theirs and wait). A script that loads the wrong ones (TTDISA's "Roadside Assistance" loads the boxcar for a type
//   with flatcars) crashed the game creating a vehicle of a model that isn't loaded; those models are loaded first.

namespace
{
    constexpr uintptr_t VanillaTable = 0x8D44F8;
    constexpr uint32_t VanillaTypes = 16;
    constexpr uint32_t RowSize = 16;
    constexpr uint32_t MaxTypes = 256;

    constexpr uintptr_t MissionTrainAdd = 0x6F75D5; // add eax, table (CTrain::CreateMissionTrain)
    const uintptr_t trackAdds[] = { 0x6F7B15, 0x6F7D2C }; // add esi, table / add ecx, table

    std::vector<int32_t> table;
    uintptr_t tableAddress = VanillaTable; // the table the game reads (the hooks replace the instructions that hold it)
    std::vector<std::vector<std::string>> names; // per type, empty when the file doesn't name it
    uint32_t types = VanillaTypes;
    std::once_flag resolved;
    std::vector<SafetyHookMid> hooks;

    bool Load(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file)
            return false;
        names.assign(MaxTypes, {});
        std::string line;
        while (std::getline(file, line))
        {
            auto start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line[start] == ';' || line[start] == '#')
                continue;
            std::istringstream words(line.substr(start));
            int type = -1;
            if (!(words >> type) || type < 0 || type >= (int)MaxTypes)
                continue;
            auto& row = names[type];
            row.clear();
            std::string name;
            while (row.size() < RowSize - 1 && words >> name)
                row.push_back(name);
            if (!row.empty())
                types = std::max(types, (uint32_t)type + 1);
        }
        return true;
    }

    void Resolve()
    {
        auto getModelInfo = (void* (__cdecl*)(const char*, int*))0x4C5940;
        std::string unknown;
        for (uint32_t type = 0; type < types; type++)
        {
            if (names[type].empty())
                continue;
            auto row = &table[type * RowSize];
            std::fill_n(row, RowSize, 0);
            int n = 0;
            for (auto& name : names[type])
            {
                int id = -1;
                if (getModelInfo(name.c_str(), &id) && id >= 0)
                    row[n++] = id;
                else
                    unknown += " " + name;
            }
        }
        if (!unknown.empty())
            core::Log("TrainTypeCarriages: vehicles the game doesn't have:{}", unknown);
    }

    void ResolveHook(SafetyHookContext&)
    {
        std::call_once(resolved, Resolve);
    }

    // before "add eax, table": eax = type * 64
    void MissionTrainHook(SafetyHookContext& ctx)
    {
        std::call_once(resolved, Resolve);
        auto row = (const int32_t*)(tableAddress + ctx.eax);
        auto requestModel = (void (__cdecl*)(int, int))0x4087E0;          // CStreaming::RequestModel(id, flags)
        auto loadAllRequestedModels = (void (__cdecl*)(bool))0x40EA10;    // CStreaming::LoadAllRequestedModels
        bool requested = false;
        for (uint32_t i = 0; i < RowSize && row[i]; i++)
        {
            if (*(const uint8_t*)(0x8E4CC0 + row[i] * 20 + 0x10) != 1)  // CStreaming::ms_aInfoForModel[id], 1 = loaded
            {
                requestModel(row[i], 8);                                // as the trains of the tracks do
                requested = true;
            }
        }
        if (requested)
            loadAllRequestedModels(false);
    }
}

namespace limits
{
    void InstallTrains()
    {
        if (!core::IsSA())
            return;
        auto setting = core::GetSetting("TrainTypeCarriages", "unlimited");
        if (!setting.apply)
            return;
        const mem::Expect expected[] = { { MissionTrainAdd + 1, VanillaTable }, { trackAdds[0] + 2, VanillaTable }, { trackAdds[1] + 2, VanillaTable } };
        if (!mem::CheckValues(expected, "TrainTypeCarriages") || *(uint8_t*)MissionTrainAdd != 0x05)
            return;

        auto path = FindDataFile(L"data\\gtasa_trainTypeCarriages.dat");
        if (!path.empty() && Load(path))
        {
            types = std::min(types, std::min(core::MaxCapacity(setting, MaxTypes), MaxTypes));
            table.assign((size_t)types * RowSize, 0);
            memcpy(table.data(), (void*)VanillaTable, VanillaTypes * RowSize * sizeof(int32_t)); // types the file doesn't name
            auto address = (uint32_t)(uintptr_t)table.data();
            tableAddress = address;
            mem::WriteDword(MissionTrainAdd + 1, address);
            for (auto add : trackAdds)
            {
                mem::WriteDword(add + 2, address);
                hooks.push_back(safetyhook::create_mid(add, ResolveHook));
            }
            core::Log("TrainTypeCarriages: {} types from {}", types, path.string());
            core::RegisterLimit({ "TrainTypeCarriages", [] { return (int)types; }, [] { return (int)types; }, [] { return (int)MaxTypes; } });
        }
        else
            std::call_once(resolved, [] {}); // the game's table, nothing to resolve
        hooks.push_back(safetyhook::create_mid(MissionTrainAdd, MissionTrainHook));
    }
}
