#pragma once
#include "stdafx.h"
#include "GameMemory.h"
#include <filesystem>

namespace limits
{
    void InstallPools();
    void InstallRenderLists();
    void InstallModelStores();
    void InstallStreaming();
    void InstallIpl();
    void InstallLights();
    void InstallEffects();
    void InstallHandling();
    void InstallVehicleAudio();
    void InstallProcedural();
    void InstallScm();
    void InstallOverlay();
    void InstallRadar();
    void InstallTrains();
    void InstallModelIds();

    // Streaming memory chosen by the adjuster, in bytes (0 when the game's own value is used).
    uint32_t StreamingBudget();

    // A data file the adjuster reads itself (e.g. L"data\\gtasa_vehicleAudioSettings.cfg"): the copy of the enabled
    // Mod Loader mod with the highest priority, else the game's. Empty when there is none.
    std::filesystem::path FindDataFile(std::wstring_view relative);

    // A static array of the game moved into reserved address space. Pages are committed when the game writes past
    // what's committed, so arrays the game fills without a bound check can't overflow into other variables.
    struct MovedArray
    {
        uint8_t* data = nullptr;
        uint32_t capacity = 0;
        mem::Reserve reserve;
    };

    // Moves `count` elements at `base` to new space for `capacity` elements and rewrites `refs` (operands pointing into
    // the old array). Returns nullptr and leaves the game alone when any operand isn't the expected one.
    MovedArray* MoveArray(std::string_view name, uintptr_t base, size_t elemSize, uint32_t count, uint32_t capacity,
                          std::span<const uintptr_t> refs, bool copy = true);
}
