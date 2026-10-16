# Fallout 4 Access - Task Runner

set shell := ["powershell", "-NoProfile", "-Command"]

# Mod Organizer 2 mod folder (override with MO2_MOD env var)
mo2_mod := env_var_or_default("MO2_MOD", "E:\\Games\\ModOrganizer\\Fallout 4\\mods\\fallout4access")

# Build the release archive into dist/ exactly as CI does (version from xmake.lua)
package:
    xmake build
    pwsh -NoProfile -File tools/package-release.ps1

# Check the committed Papyrus .pex files still match their .psc sources
check-papyrus:
    pwsh -NoProfile -File tools/check-papyrus.ps1

# Sync ESP from MO2 mod directory to Data/ in this repo
sync-esp:
    Copy-Item "{{mo2_mod}}\fallout4access.esp" "Data\fallout4access.esp"
