#pragma once

#include <string>
#include <filesystem>

// Patches the main.pak with widescreen resources from PvZWidescreen GitHub repo.
// Must be called after SDL_Init() so SDL_GetPrefPath works.
// Returns true if already patched or patched successfully.
bool PatchWidescreenPak(const std::filesystem::path& theResourceDir);
