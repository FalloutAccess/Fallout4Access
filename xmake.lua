-- set minimum xmake version
set_xmakever("2.8.2")

-- includes
set_config("commonlib_json", true)
includes("lib/commonlibf4")

-- set project
set_project("fallout4access")
set_version("0.0.2")
set_license("MIT")

-- set defaults
set_languages("c++23")
set_warnings("allextra")

-- set policies
set_policy("package.requires_lock", true)

-- add rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

package("sral")
    add_deps("cmake")
    set_sourcedir(path.join(os.scriptdir(), "lib", "SRAL"))
    on_load(function (package)
        if not package:config("shared") then
            package:add("defines", "SRAL_STATIC")
        end
    end)
    on_install(function (package)
        local configs = {}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release" or "RelWithDebInfo"))
        table.insert(configs, "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"))
        table.insert(configs, "-DBUILD_TESTING=OFF")
        table.insert(configs, "-DBUILD_EXAMPLES=OFF")
        table.insert(configs, "-DSRAL_DISABLE_UIA=ON")
        table.insert(configs, "-DBUILD_SRAL_TEST=OFF")
        table.insert(configs, "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL")
        import("package.tools.cmake").install(package, configs, {generator = "NMake Makefiles"})
    end)
package_end()

add_requires("sral", {configs = {shared = false}})
add_requires("zydis")

-- Project-specific configuration
-- Mod Organizer 2 installation path (override with XSE_FO4_MODS_PATH env var)
local MO2_MODS_PATH = os.getenv("XSE_FO4_MODS_PATH") or "E:\\Games\\ModOrganizer\\Fallout 4\\mods"
local MOD_FOLDER_NAME = "fallout4access"

-- Fallout 4 installation path for Papyrus compiler (override with XSE_FO4_GAME_PATH env var)
local FO4_PATH = os.getenv("XSE_FO4_GAME_PATH") or "E:\\SteamLibrary\\steamapps\\common\\Fallout 4"

-- targets
target("fallout4access")
    -- compile Papyrus scripts before building
    before_build(function(target)
        local compiler = path.join(FO4_PATH, "Papyrus Compiler", "PapyrusCompiler.exe")
        local flags_file = path.join(FO4_PATH, "Data", "Scripts", "Source", "Base", "Institute_Papyrus_Flags.flg")
        local base_import = path.join(FO4_PATH, "Data", "Scripts", "Source", "Base")
        local user_import = path.join(FO4_PATH, "Data", "Scripts", "Source", "User")
        local project_source = path.join(os.projectdir(), "Data", "Scripts", "Source", "User")
        local output_dir = path.join(os.projectdir(), "Data", "Scripts")

        if not os.isfile(compiler) then
            print("⚠️ Papyrus compiler not found at: " .. compiler)
            print("   Skipping script compilation")
            return
        end

        -- Find all .psc files
        local psc_files = os.files(path.join(project_source, "**.psc"))
        if #psc_files == 0 then
            print("📜 No Papyrus scripts to compile")
            return
        end

        print("📜 Compiling " .. #psc_files .. " Papyrus script(s)...")

        -- Build import path string
        local imports = base_import .. ";" .. user_import .. ";" .. project_source

        for _, psc_file in ipairs(psc_files) do
            local rel_path = path.relative(psc_file, project_source)
            print("   Compiling: " .. rel_path)

            -- Run compiler with relative path (compiler derives script name from path)
            local args = {
                rel_path,
                "-f=" .. flags_file,
                "-i=" .. imports,
                "-o=" .. output_dir
            }

            local outdata, errdata = os.iorunv(compiler, args, {curdir = project_source})
            if errdata and #errdata > 0 then
                print("   ❌ Error: " .. errdata)
                return
            end
        end

        print("✓ Papyrus scripts compiled successfully")
    end)

    -- add dependencies to target
    add_deps("commonlibf4")
    add_packages("sral", "zydis")
    add_defines("SRAL_STATIC")
    add_includedirs("lib/SRAL/Include")


    -- add commonlibsse plugin
    add_rules("commonlibf4.plugin", {
        name = "fallout4access",
        author = "alex19EP",
        description = "F4SE plugin providing access to Fallout 4 for blind users",
    })

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    -- install Data directory contents (scripts, etc.)
    add_installfiles("Data/(**)", {prefixdir = ""})

    -- show installation path after install
    after_install(function(target)
        local install_path = target:installdir()
        if install_path then
            print("=== PLUGIN SUCCESSFULLY INSTALLED ===")
            print("Installation directory: " .. install_path)
            print("Plugin installed to: " .. path.join(install_path, "F4SE", "Plugins", target:name() .. ".dll"))
            print("PDB file installed to: " .. path.join(install_path, "F4SE", "Plugins", target:name() .. ".pdb"))
            print("=====================================")
        else
            print("=== PLUGIN INSTALLATION COMPLETED ===")

            -- Check for environment variables and show actual installation
            local fo4_mods_path = os.getenv("XSE_FO4_MODS_PATH")
            local fo4_game_path = os.getenv("XSE_FO4_GAME_PATH")

            print("Built plugin: " .. target:targetfile())
            print("")

            if fo4_mods_path then
                local actual_install_path = path.join(fo4_mods_path, target:name(), "F4SE", "Plugins", target:name() .. ".dll")
                print("✓ XSE_FO4_MODS_PATH detected: " .. fo4_mods_path)
                print("✓ Plugin should be installed to:")
                print("  " .. actual_install_path)
                if os.isfile(actual_install_path) then
                    print("✓ Installation verified - file exists")
                    print("  File size: " .. os.filesize(actual_install_path) .. " bytes")
                else
                    print("❌ Warning: Installation file not found at expected location")
                end
            elseif fo4_game_path then
                local actual_install_path = path.join(fo4_game_path, "Data", "F4SE", "Plugins", target:name() .. ".dll")
                print("✓ XSE_FO4_GAME_PATH detected: " .. fo4_game_path)
                print("✓ Plugin should be installed to:")
                print("  " .. actual_install_path)
                if os.isfile(actual_install_path) then
                    print("✓ Installation verified - file exists")
                    print("  File size: " .. os.filesize(actual_install_path) .. " bytes")
                else
                    print("❌ Warning: Installation file not found at expected location")
                end
            else
                print("❌ No auto-installation performed")
                print("   Reason: No Fallout 4 environment variables configured")
                print("")
                print("To enable automatic installation to Fallout 4, set one of:")
                print("  XSE_FO4_GAME_PATH=<your_fallout4_directory>")
                print("    Example: XSE_FO4_GAME_PATH=C:\\Program Files (x86)\\Steam\\steamapps\\common\\Fallout 4")
                print("  OR")
                print("  XSE_FO4_MODS_PATH=<your_mods_directory>")
                print("    Example: XSE_FO4_MODS_PATH=C:\\Games\\Fallout4Mods")
                print("")
                print("📁 Manual installation: Copy the built plugin to your F4SE\\Plugins directory")
            end
            print("======================================")
        end
    end)
