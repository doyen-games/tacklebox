// Per-OS application data locations.
//   Windows: %APPDATA%\TackleBox
//   macOS:   ~/Library/Application Support/TackleBox
//   Linux:   $XDG_DATA_HOME/tacklebox or ~/.local/share/tacklebox
#pragma once

#include <filesystem>

namespace tb {

// Data directory (created on first call).
std::filesystem::path dataDir();

// Mobile shells point the data dir at the app sandbox (SDL_GetPrefPath)
// BEFORE anything calls dataDir(); no-op afterwards.
void overrideDataDir(const std::filesystem::path& dir);

std::filesystem::path vaultFile();     // dataDir()/vault.tbx
std::filesystem::path settingsFile();  // dataDir()/settings.json (cosmetic only)

// Atomic file write: temp file + flush + rename over the target. Returns false
// on any I/O failure. `keepBackup` first renames an existing target to
// <name>.bak so a mid-write crash can never destroy the only copy.
bool atomicWrite(const std::filesystem::path& target, const std::string& content,
                 bool keepBackup);

}  // namespace tb
