-- More Informative Console SF
set_xmakever("3.0.0")

set_project("MoreInformativeConsoleSF")
set_version("1.6.2")
set_license("GPL-3.0")

set_arch("x64")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")  -- optimized, with a .pdb for crash loggers such as Trainwreck

-- CommonLibSF (OSF's fork, commit 4242565) is expected next to this project, in ../commonlibsf,
-- see README: "Build".
includes("../commonlibsf")

target("MoreInformativeConsoleSF", function()
    add_rules("commonlibsf.plugin", {
        name = "MoreInformativeConsoleSF",
        author = "drvictorvs",
        description = "Shows detailed information about the reference selected in the console"
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/PCH.h")
    add_syslinks("user32")  -- GetAsyncKeyState, for the scroll keys

    add_installfiles("dist/Data/SFSE/Plugins/MoreInformativeConsoleSF.ini", { prefixdir = "SFSE/Plugins" })
    add_installfiles("dist/Data/SFSE/Plugins/OSF/Settings/schemas/moreinformativeconsole-sf.json", { prefixdir = "SFSE/Plugins/OSF/Settings/schemas" })
    
    -- After each build, copy the plugin next to its INI in dist/, so dist/Data is the complete mod.
    after_build(function(target)
    local dir = path.join(os.projectdir(), "dist", "Data", "SFSE", "Plugins")
    os.mkdir(dir)
    os.cp(target:targetfile(), dir)
    local pdb = target:symbolfile()
    if pdb and os.isfile(pdb) then
        os.cp(pdb, dir)
    end
    cprint("${dim}copied %s to %s", path.filename(target:targetfile()), path.relative(dir, os.projectdir()))
end)
end)
