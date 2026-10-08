set_xmakever("3.0.0")

includes("lib/commonlibsse-ng")

set_config("rex_ini", true)
add_requires("doctest 2.5.3")

set_project("SaferSaving")
set_version("1.0.0")
set_license("GPL-3.0-only")

set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")
add_rules("plugin.vsxmake.autoupdate")

target("SaferSaving")
    add_rules("commonlibsse-ng.plugin", {
        name = "SaferSaving",
        author = "Ultimersiv",
        description = "Blocks saving while the player is in an unsafe state."
    })

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    add_syslinks("shell32")
    set_pcxxheader("src/pch.h")

    add_installfiles("release/SaferSaving.ini", {prefixdir = "SKSE/Plugins"})
    add_installfiles("LICENSE")

target("tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/*.cpp", "src/ini-overrides.cpp")
    add_includedirs("src")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_packages("simpleini", "doctest")
    add_tests("default")
