#include "Core.h"
#include "Adjusters.h"

// SA radar icons: 64 sprites (CRadar::RadarBlipSprites, 0xBAA250) loaded from a table of texture names (0x8D0720,
// { name, mask } pairs). A blip with a sprite of 64 or more reads past the array (a byte and two timers of CRadar) as
// a texture and crashes the game when it is drawn, and the map legend uses a coordinate as the pointer to its text.
// With data\gtasa_radarBlipSpriteFilenames.dat (the fastman92 format: one line per sprite, its id and the name of its
// texture in models\hud.txd), the sprites move to an array for every id the file names, up to 255 (the game keeps the
// sprite of a blip in a byte). An optional third word is the GXT key of the sprite's text in the map legend, for the
// new sprites; without one the legend shows the icon without text.

namespace
{
    constexpr uintptr_t VanillaSprites = 0xBAA250;
    constexpr uintptr_t VanillaNames = 0x8D0720;
    constexpr uint32_t VanillaCount = 64;
    constexpr uint32_t MaxCount = 256;

    // operands pointing into the sprite array: the loops of CRadar::LoadTextures and CRadar::Shutdown, two indexed
    // accesses and seven addresses of single sprites
    const uintptr_t spriteRefs[] = {
        0x5827EB, 0x585942, 0x582E54, 0x5860F3,
        0x584A4E, 0x5876FF, 0x58777C, 0x58871E, 0x58794D, 0x587A45, 0x587AF1,
    };

    struct NameEntry
    {
        const char* name;
        const char* mask;
    };

    std::vector<std::string> names, keys;
    std::vector<NameEntry> table;
    uint32_t count = VanillaCount;

    bool Load(const std::filesystem::path& path, uint32_t cap)
    {
        std::ifstream file(path);
        if (!file)
            return false;
        names.assign(MaxCount, std::string());
        keys.assign(MaxCount, std::string());
        auto vanilla = (const NameEntry*)VanillaNames;
        for (uint32_t i = 0; i < VanillaCount; i++)
            names[i] = vanilla[i].name;

        std::string line;
        while (std::getline(file, line))
        {
            auto start = line.find_first_not_of(" \t");
            if (start == std::string::npos || line[start] == ';' || line[start] == '#')
                continue;
            int id = -1;
            char name[128]{}, key[16]{};
            if (sscanf_s(line.c_str() + start, "%d %127s %15s", &id, name, (unsigned)sizeof(name), key, (unsigned)sizeof(key)) < 1 ||
                id < 0 || id >= (int)cap)
                continue;
            names[id] = name;
            keys[id] = key;
            count = std::max(count, (uint32_t)id + 1);
        }
        return true;
    }

    // CRadar::DrawLegend: the default case of the switch over the sprite (esi), which continues at 0x582DA7 with the
    // text in eax.
    const char* __cdecl LegendText(int sprite)
    {
        static char empty[1] = "";
        if (sprite < 0 || sprite >= (int)count || keys[sprite].empty())
            return empty;
        auto get = (const char* (__thiscall*)(void*, const char*))0x6A0050; // CText::Get
        return get((void*)0xC1B340, keys[sprite].c_str());                // TheText
    }

    const uintptr_t legendDone = 0x582DA7;

    void __declspec(naked) LegendDefault()
    {
        __asm
        {
            push    esi
            call    LegendText
            add     esp, 4
            jmp     legendDone
        }
    }
}

namespace limits
{
    void InstallRadar()
    {
        if (!core::IsSA())
            return;
        auto setting = core::GetSetting("RadarBlipSprites", "unlimited");
        if (!setting.apply)
            return;
        auto path = FindDataFile(L"data\\gtasa_radarBlipSpriteFilenames.dat");
        if (path.empty())
            return;

        const mem::Expect expected[] = {
            { 0x5827F0, VanillaNames }, { 0x582806, VanillaNames + VanillaCount * 8 },   // LoadTextures: name table
            { 0x585952, VanillaSprites + VanillaCount * 4 },                            // Shutdown: end of the loop
        };
        if (!mem::CheckValues(expected, "RadarBlipSprites") || *(uint16_t*)0x5828AE != 0x870F ||
            0x5828B4 + *(int32_t*)0x5828B0 != 0x582DA3)
            return;
        if (!Load(path, std::min(core::MaxCapacity(setting, MaxCount), MaxCount)))
            return;

        auto moved = MoveArray("RadarBlipSprites", VanillaSprites, 4, VanillaCount, count, spriteRefs, false);
        if (!moved)
            return;
        memset(moved->data, 0, (size_t)count * 4); // CSprite2d: a texture pointer, set by LoadTextures

        table.resize(count);
        for (uint32_t i = 0; i < count; i++)
            table[i] = { names[i].c_str(), "" };
        mem::WriteDword(0x5827F0, (uint32_t)(uintptr_t)table.data());
        mem::WriteDword(0x582806, (uint32_t)(uintptr_t)(table.data() + count));
        mem::WriteDword(0x585952, (uint32_t)(uintptr_t)(moved->data + count * 4));
        mem::WriteDword(0x5828B0, (uint32_t)((uintptr_t)LegendDefault - 0x5828B4)); // ja to the default case

        core::Log("RadarBlipSprites: {} sprites from {}", count, path.string());
        core::RegisterLimit({ "RadarBlipSprites", [] { return (int)count; }, [] { return (int)count; }, [] { return (int)MaxCount; } });
    }
}
