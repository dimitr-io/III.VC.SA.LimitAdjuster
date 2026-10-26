#include "Core.h"
#include "Adjusters.h"

// SA limits on model ids outside the model stores, and the directory of the clothes archive. Vehicles and peds added
// with model ids beyond the game's ranges hit them:
// - KillableModels: CDarkel counts the kills of each player per model in a table for ids 0 to 799 (int16[800][2] at
//   0x969A50). A ped or vehicle with a higher id writes past it when it is killed or blown up.
// - CarGeneratorModels: CTheCarGenerators::CreateCarGenerator only accepts -1 (random) and ids 400 to 630, so parked
//   cars of vehicles with higher ids are never created.
// - PaintjobModels: a texture dictionary named after a vehicle and a number (e.g. angel1.txd) is a paintjob of it;
//   CVehicleModelInfo::AssignRemapTxd only looks among the ids 400 to 630.
// - ClothesDirectory: the directory of models\player.img has a static buffer of 550 entries; the game skips the files
//   of a bigger archive beyond those, so their clothes can't be worn.

namespace
{
    constexpr uint32_t MaxModelId = 20000; // the game's model info array

    // CDarkel::RegisteredKills. HOODLUM has the reset, query and total functions in its own section.
    constexpr uintptr_t KillTable = 0x969A50;
    constexpr uint32_t KillIds = 800;
    const uintptr_t killRefs[] = { 0x43D6A7, 0x43D6CF, 0x43D6EA, 0x43DE70, 0x43DF5F };
    const uintptr_t killRefsHoodlum[] = { 0x1561637, 0x1564C2F, 0x156990A, 0x43DE70, 0x43DF5F };

    void InstallKillableModels()
    {
        auto setting = core::GetSetting("KillableModels", "unlimited");
        if (!setting.apply)
            return;
        auto ids = std::max(core::MaxCapacity(setting, MaxModelId), KillIds);
        if (ids == KillIds)
            return;

        auto countOperand = core::SA(0x43D6AC, 0x156163C); // mov ecx, 800 of the reset loop
        const mem::Expect count[] = { { countOperand, KillIds } };
        if (!mem::CheckValues(count, "KillableModels"))
            return;
        auto refs = core::hoodlum ? std::span<const uintptr_t>(killRefsHoodlum) : std::span<const uintptr_t>(killRefs);
        auto moved = limits::MoveArray("KillableModels", KillTable, 4, KillIds, ids, refs); // one int16 per player
        if (!moved)
            return;
        mem::WriteDword(countOperand, ids);
        core::RegisterLimit({ "KillableModels", [] { return -1; }, [ids] { return (int)ids; }, [ids] { return (int)ids; } });
    }

    // The last model id accepted, as the immediate of a compare (cmp ecx, 276h) or a push (push 276h).
    void RaiseLastId(const char* key, uintptr_t operand, uint32_t vanillaLast)
    {
        auto setting = core::GetSetting(key, "unlimited");
        if (!setting.apply)
            return;
        auto last = std::max(core::MaxCapacity(setting, MaxModelId), vanillaLast + 1) - 1;
        const mem::Expect expected[] = { { operand, vanillaLast } };
        if (last == vanillaLast || !mem::CheckValues(expected, key))
            return;
        mem::WriteDword(operand, last);
        core::RegisterLimit({ key, [] { return -1; }, [last] { return (int)last + 1; }, [last] { return (int)last + 1; } });
    }

    // CClothes: a CDirectory at 0xBC12C0 set up with the buffer and its size at two places (push 0BBCDC8h; push 226h).
    constexpr uint32_t ClothesEntries = 550;
    constexpr uint32_t ClothesCeiling = 8192;
    const uintptr_t clothesSetups[] = { 0x5A4190, 0x5A69E3 };

    void InstallClothesDirectory()
    {
        auto setting = core::GetSetting("ClothesDirectory", "unlimited");
        if (!setting.apply)
            return;
        auto entries = std::max(core::MaxCapacity(setting, ClothesCeiling), ClothesEntries);
        if (entries == ClothesEntries)
            return;
        std::vector<mem::Expect> expected;
        for (auto at : clothesSetups)
        {
            expected.push_back({ at + 1, 0xBBCDC8 });
            expected.push_back({ at + 6, ClothesEntries });
        }
        if (!mem::CheckValues(expected, "ClothesDirectory"))
            return;
        static std::vector<uint8_t> buffer;
        buffer.assign((size_t)entries * 32, 0); // CDirectory::DirectoryInfo
        for (auto at : clothesSetups)
        {
            mem::WriteDword(at + 1, (uint32_t)(uintptr_t)buffer.data());
            mem::WriteDword(at + 6, entries);
        }
        core::RegisterLimit({ "ClothesDirectory", [] { return *(int*)(0xBC12C0 + 8); }, [entries] { return (int)entries; }, [entries] { return (int)entries; } });
    }
}

namespace limits
{
    void InstallModelIds()
    {
        if (!core::IsSA())
            return;
        InstallKillableModels();
        RaiseLastId("CarGeneratorModels", 0x6F31B3, 630); // CTheCarGenerators::CreateCarGenerator
        RaiseLastId("PaintjobModels", 0x4C93C3, 630);     // CVehicleModelInfo::AssignRemapTxd
        InstallClothesDirectory();
    }
}
