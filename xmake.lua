-- set minimum xmake version
set_xmakever("2.8.2")

-- includes
includes("lib/commonlibf4")

-- set project
set_project("fallout4access")
set_version("0.0.0")
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
-- Mod Organizer 2 installation path
local MO2_MODS_PATH = "E:\\Games\\ModOrganizer\\Fallout 4\\mods"
local MOD_FOLDER_NAME = "fallout4access"

-- Fallout 4 installation path (for Papyrus compiler)
local FO4_PATH = "E:\\SteamLibrary\\steamapps\\common\\Fallout 4"

-- custom task to show installation info
task("show-install-path")
    set_menu({
        usage = "xmake show-install-path",
        description = "Show plugin installation path information",
        options = {}
    })
    on_run(function ()
        print("=== FALLOUT 4 PLUGIN INSTALLATION INFO ===")

        local fo4_mods_path = os.getenv("XSE_FO4_MODS_PATH")
        local fo4_game_path = os.getenv("XSE_FO4_GAME_PATH")

        print("Project: " .. (get_config("project") or "fallout4access"))
        print("Plugin name: fallout4access")
        print("")

        -- Show current build status
        local build_dir = path.join(os.projectdir(), "build")
        local target_dir = path.join(build_dir, "windows", "x64", "release")
        local plugin_file = path.join(target_dir, "fallout4access.dll")

        if os.isfile(plugin_file) then
            print("📦 Build Status:")
            print("✓ Plugin built: " .. plugin_file)
            print("  File size: " .. os.filesize(plugin_file) .. " bytes")
            print("  Last modified: " .. os.date("%Y-%m-%d %H:%M:%S", os.mtime(plugin_file)))
        else
            print("📦 Build Status:")
            print("❌ Plugin not built yet. Run 'xmake build' first.")
        end
        print("")

        -- Show installation configuration
        print("🎯 Installation Configuration:")

        -- Show project-configured MO2 path first
        print("📁 Project MO2 Configuration:")
        local mo2_install_path = path.join(MO2_MODS_PATH, MOD_FOLDER_NAME, "F4SE", "Plugins", "fallout4access.dll")
        print("  MO2 Mods Path: " .. MO2_MODS_PATH)
        print("  Target install: " .. mo2_install_path)
        if os.isfile(mo2_install_path) then
            print("  ✓ Currently installed in MO2 (size: " .. os.filesize(mo2_install_path) .. " bytes)")
        else
            print("  ❌ Not currently installed in MO2")
        end
        print("")

        if fo4_mods_path then
            print("✓ XSE_FO4_MODS_PATH is configured: " .. fo4_mods_path)
            local install_path = path.join(fo4_mods_path, "fallout4access", "F4SE", "Plugins", "fallout4access.dll")
            print("  Auto-install target: " .. install_path)
            if os.isfile(install_path) then
                print("  ✓ Currently installed (size: " .. os.filesize(install_path) .. " bytes)")
            else
                print("  ❌ Not currently installed")
            end
        elseif fo4_game_path then
            print("✓ XSE_FO4_GAME_PATH is configured: " .. fo4_game_path)
            local install_path = path.join(fo4_game_path, "Data", "F4SE", "Plugins", "fallout4access.dll")
            print("  Auto-install target: " .. install_path)
            if os.isfile(install_path) then
                print("  ✓ Currently installed (size: " .. os.filesize(install_path) .. " bytes)")
            else
                print("  ❌ Not currently installed")
            end
        else
            print("❌ No environment variables configured")
            print("  Use project tasks for MO2 installation")
        end

        print("")
        print("💡 Commands:")
        print("  xmake build              - Build the plugin")
        print("  xmake install             - Install using environment variables")
        print("  xmake install-mo2         - Install to Mod Organizer 2 (project-configured)")
        print("  xmake build-and-install-mo2 - Build and install to MO2 in one step")
        print("  xmake clean               - Clean build files")
        print("==========================================")
    end)

-- Task to install to MO2 without building
task("install-mo2")
    set_menu({
        usage = "xmake install-mo2",
        description = "Install plugin to Mod Organizer 2 mods folder",
        options = {}
    })
    on_run(function ()
        local build_dir = path.join(os.projectdir(), "build")
        local target_dir = path.join(build_dir, "windows", "x64", "release")
        local plugin_file = path.join(target_dir, "fallout4access.dll")
        local pdb_file = path.join(target_dir, "fallout4access.pdb")

        if not os.isfile(plugin_file) then
            print("❌ Plugin not built yet. Run 'xmake build' first.")
            return
        end

        local mo2_plugin_dir = path.join(MO2_MODS_PATH, MOD_FOLDER_NAME, "F4SE", "Plugins")
        local mo2_plugin_file = path.join(mo2_plugin_dir, "fallout4access.dll")
        local mo2_pdb_file = path.join(mo2_plugin_dir, "fallout4access.pdb")

        print("=== INSTALLING TO MOD ORGANIZER 2 ===")
        print("Source: " .. plugin_file)
        print("Target: " .. mo2_plugin_file)

        -- Create directory if it doesn't exist
        os.mkdir(mo2_plugin_dir)

        -- Copy files
        if os.cp(plugin_file, mo2_plugin_file) then
            print("✓ Plugin installed successfully")
            print("  File size: " .. os.filesize(mo2_plugin_file) .. " bytes")
        else
            print("❌ Failed to install plugin")
            return
        end

        -- Copy PDB if exists
        if os.isfile(pdb_file) then
            if os.cp(pdb_file, mo2_pdb_file) then
                print("✓ Debug symbols (PDB) installed")
            else
                print("⚠️ Warning: Failed to install debug symbols")
            end
        end

        -- Copy Data directory contents (preserving directory structure)
        local data_dir = path.join(os.projectdir(), "Data")
        local mo2_mod_dir = path.join(MO2_MODS_PATH, MOD_FOLDER_NAME)
        if os.isdir(data_dir) then
            print("📂 Installing Data files...")
            os.cp(path.join(data_dir, "*"), mo2_mod_dir)
            print("✓ Data files installed")
        end

        print("✓ Installation to MO2 completed!")
        print("📁 Mod folder: " .. path.join(MO2_MODS_PATH, MOD_FOLDER_NAME))
        print("🎮 Enable the mod in Mod Organizer 2 to use it")
        print("=====================================")
    end)

-- Task to build and install to MO2 in one step
task("build-and-install-mo2")
    set_menu({
        usage = "xmake build-and-install-mo2",
        description = "Build and install plugin to Mod Organizer 2 in one step",
        options = {}
    })
    on_run(function ()
        -- First build
        import("core.project.task")
        print("🔨 Building plugin...")
        local ok = try { function()
            task.run("build", {target = "fallout4access"})
            return true
        end, catch { function(errors)
            print("❌ Build failed: " .. tostring(errors))
            return false
        end }}

        if not ok then
            return
        end

        print("✓ Build completed successfully")
        print("")

        -- Then install to MO2
        task.run("install-mo2")
    end)

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
