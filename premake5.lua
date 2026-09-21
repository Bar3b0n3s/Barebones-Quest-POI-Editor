workspace "BarebonesQuestPoiEditor"
    architecture "x86_64"
    configurations { "Debug", "Release" }
    startproject "QuestPoiEditor"
    location "build"

project "Glfw"
    kind "StaticLib"
    language "C"
    staticruntime "off"
    targetdir "build/lib/%{cfg.system}/%{cfg.buildcfg}"
    objdir "build/obj-glfw/%{cfg.system}/%{cfg.buildcfg}"
    includedirs { "vendor/glfw/include", "vendor/glfw/src" }
    defines { "_CRT_SECURE_NO_WARNINGS" }
    files {
        "vendor/glfw/src/context.c",
        "vendor/glfw/src/egl_context.c",
        "vendor/glfw/src/init.c",
        "vendor/glfw/src/input.c",
        "vendor/glfw/src/monitor.c",
        "vendor/glfw/src/null_init.c",
        "vendor/glfw/src/null_joystick.c",
        "vendor/glfw/src/null_monitor.c",
        "vendor/glfw/src/null_window.c",
        "vendor/glfw/src/osmesa_context.c",
        "vendor/glfw/src/platform.c",
        "vendor/glfw/src/vulkan.c",
        "vendor/glfw/src/window.c"
    }

    filter "system:windows"
        systemversion "latest"
        defines { "_GLFW_WIN32" }
        files {
            "vendor/glfw/src/wgl_context.c",
            "vendor/glfw/src/win32_init.c",
            "vendor/glfw/src/win32_joystick.c",
            "vendor/glfw/src/win32_module.c",
            "vendor/glfw/src/win32_monitor.c",
            "vendor/glfw/src/win32_thread.c",
            "vendor/glfw/src/win32_time.c",
            "vendor/glfw/src/win32_window.c"
        }

    filter "system:linux"
        pic "On"
        defines { "_GLFW_X11" }
        files {
            "vendor/glfw/src/glx_context.c",
            "vendor/glfw/src/linux_joystick.c",
            "vendor/glfw/src/posix_module.c",
            "vendor/glfw/src/posix_poll.c",
            "vendor/glfw/src/posix_thread.c",
            "vendor/glfw/src/posix_time.c",
            "vendor/glfw/src/x11_init.c",
            "vendor/glfw/src/x11_monitor.c",
            "vendor/glfw/src/x11_window.c",
            "vendor/glfw/src/xkb_unicode.c"
        }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "Speed"
        symbols "Off"

project "QuestPoiEditor"
    kind "WindowedApp"
    language "C++"
    cppdialect "C++23"
    staticruntime "off"
    targetdir "bin/%{cfg.buildcfg}"
    objdir "build/obj/%{cfg.system}/%{cfg.buildcfg}"
    files {
        "src/**.h", "src/**.hpp", "src/**.cpp",
        "vendor/imgui/imgui.cpp",
        "vendor/imgui/imgui_draw.cpp",
        "vendor/imgui/imgui_tables.cpp",
        "vendor/imgui/imgui_widgets.cpp",
        "vendor/imgui/backends/imgui_impl_glfw.cpp",
        "vendor/imgui/backends/imgui_impl_opengl3.cpp"
    }
    includedirs {
        "src", "vendor", "vendor/glfw/include",
        "vendor/imgui", "vendor/imgui/backends"
    }
    links { "Glfw" }

    filter "system:windows"
        systemversion "latest"
        files { "src/**.rc" }
        defines { "UNICODE", "_UNICODE", "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS" }
        links { "opengl32", "gdi32", "user32", "shell32", "ole32" }

    filter "system:linux"
        kind "ConsoleApp"
        links { "GL", "X11", "Xrandr", "Xi", "Xcursor", "Xinerama", "dl", "pthread", "m" }

    filter "configurations:Debug"
        symbols "On"
        defines { "_DEBUG" }

    filter "configurations:Release"
        optimize "Speed"
        symbols "Off"
        defines { "NDEBUG" }

project "QuestPoiCoreTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++23"
    staticruntime "off"
    targetdir "bin-tools/%{cfg.buildcfg}"
    objdir "build/obj-tests/%{cfg.system}/%{cfg.buildcfg}"
    files {
        "tests/CoreTests.cpp",
        "src/wow/BlpDecoder.cpp",
        "src/wow/BlpDecoder.h",
        "src/wow/WorldMap.cpp",
        "src/wow/WorldMap.h",
        "src/wow/MpqArchive.cpp",
        "src/wow/MpqArchive.h",
        "src/Models.h",
        "src/Platform.h"
    }
    includedirs { "src" }
    defines { "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS" }

    filter "system:linux"
        links { "dl" }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "Speed"
        symbols "Off"

project "QuestPoiDatabaseIntegrationTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++23"
    staticruntime "off"
    targetdir "bin-tools/%{cfg.buildcfg}"
    objdir "build/obj-db-integration-tests/%{cfg.system}/%{cfg.buildcfg}"
    files {
        "tests/DatabaseIntegrationTests.cpp",
        "src/db/Database.cpp",
        "src/db/Database.h",
        "src/Models.h",
        "src/Platform.h"
    }
    includedirs { "src" }
    defines { "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS" }

    filter "system:linux"
        links { "dl" }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "Speed"
        symbols "Off"
