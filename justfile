# Fallout 4 Access - Task Runner

set shell := ["powershell", "-NoProfile", "-Command"]

# Mod Organizer 2 mod folder
mo2_mod := "E:\\Games\\ModOrganizer\\Fallout 4\\mods\\fallout4access"

# Sync ESP from MO2 mod directory to Data/ in this repo
sync-esp:
    Copy-Item "{{mo2_mod}}\fallout4access.esp" "Data\fallout4access.esp"
