// Launch-at-login registration. The OS entry itself is the source of truth -
// nothing is stored in the vault or settings, so the toggle always reflects
// what the system will actually do at boot.
//   Windows: HKCU\Software\Microsoft\Windows\CurrentVersion\Run
//   macOS:   ~/Library/LaunchAgents/io.tacklebox.wallet.plist
//   Linux:   $XDG_CONFIG_HOME/autostart/tacklebox.desktop
#pragma once

#include <string>

namespace tb {

bool autostartSupported();  // desktop platforms only
bool autostartEnabled();
bool setAutostart(bool enabled, std::string* error = nullptr);

}  // namespace tb
