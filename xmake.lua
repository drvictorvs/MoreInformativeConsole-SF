-- More Informative Console SF
set_xmakever("3.0.0")

set_project("MoreInformativeConsoleSF")
set_version("1.4.0")
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
end)
