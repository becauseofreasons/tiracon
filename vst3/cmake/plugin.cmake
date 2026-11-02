# VST3 plugin "Tiracon" on top of the Steinberg VST3 SDK (included from CMakeLists.txt)

if(NOT EXISTS "${VST3_SDK_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "VST3 SDK not found: ${VST3_SDK_ROOT} (set -DVST3_SDK_ROOT=...)")
endif()

# only what the plugin needs from the SDK
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
option(TIRACON_HOSTING_TOOLS "Also build the SDK's editorhost (for tests)" OFF)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ${TIRACON_HOSTING_TOOLS} CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)      # VSTGUI is added below (without its standalone part)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
if(CMAKE_CROSSCOMPILING)
    # Windows binaries cannot run here: no validator run, no moduleinfo.json
    set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
endif()

set(vst3sdk_SOURCE_DIR "${VST3_SDK_ROOT}")
add_subdirectory("${VST3_SDK_ROOT}" "${PROJECT_BINARY_DIR}/vst3sdk" EXCLUDE_FROM_ALL)
smtg_enable_vst3_sdk()

if(MINGW)
    # MinGW has no std::aligned_alloc; dataexchange.cpp is the only user and the plugin does not need it
    get_target_property(_sdk_sources sdk SOURCES)
    list(FILTER _sdk_sources EXCLUDE REGEX "utility/dataexchange\\.cpp$")
    set_property(TARGET sdk PROPERTY SOURCES ${_sdk_sources})
    # validator.exe for tests under Wine: console program, no runtime DLLs
    if(TARGET validator)
        target_link_options(validator PRIVATE -mconsole -municode -static)
    endif()
endif()
# VSTGUI like smtg_enable_vstgui_support() of the SDK, but without the standalone library
# (the SDK switches it on, and it has no sources for MinGW)
set(VSTGUI_DISABLE_UNITTESTS 1)
set(VSTGUI_STANDALONE OFF)
set(VSTGUI_STANDALONE_EXAMPLES OFF)
set(VSTGUI_TOOLS OFF)
set(VSTGUI_UISCRIPTING OFF)
set(VSTGUI_ENABLE_DEPRECATED_METHODS OFF CACHE BOOL "" FORCE)
set(VSTGUI_ENABLE_OPENGL_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_VSTGUI_SOURCE_DIR "${VST3_SDK_ROOT}/vstgui4")
add_subdirectory("${SMTG_VSTGUI_SOURCE_DIR}/vstgui" "${PROJECT_BINARY_DIR}/vstgui" EXCLUDE_FROM_ALL)
if(MINGW)
    # compatibility shims (cmake/mingw-compat), only for VSTGUI itself
    set(_compat "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mingw-compat")
    target_include_directories(vstgui PRIVATE "${_compat}")
    target_compile_options(vstgui PRIVATE -include "${_compat}/vstgui_mingw_compat.h")
    target_compile_definitions(vstgui PRIVATE VSTGUI_WIN32_CUSTOMFONT_SUPPORT=0)
    # MSVC pulls these in with #pragma comment(lib); MinGW needs them listed
    target_link_libraries(vstgui PUBLIC d2d1 dwrite windowscodecs shlwapi imm32 d3d11 dxgi dcomp dwmapi
                          uuid ole32 oleaut32 comdlg32 shell32 gdi32 user32 winmm)
    set_source_files_properties("${SMTG_VSTGUI_SOURCE_DIR}/vstgui/lib/platform/win32/win32directcomposition.cpp"
        TARGET_DIRECTORY vstgui PROPERTIES COMPILE_DEFINITIONS VSTGUI_MINGW_DCOMP3=1)
endif()
add_library(tiracon_vstgui STATIC "${VST3_SDK_ROOT}/public.sdk/source/vst/vstguieditor.cpp")
target_link_libraries(tiracon_vstgui PUBLIC base vstgui)
target_include_directories(tiracon_vstgui PUBLIC "${SMTG_VSTGUI_SOURCE_DIR}")
# the same switches as VSTGUI itself for everything that uses it (they change the class layout)
set(_vstgui_defs ${VSTGUI_COMPILE_DEFINITIONS})
list(TRANSFORM _vstgui_defs REPLACE "^PRIVATE$" "PUBLIC")
target_compile_definitions(tiracon_vstgui ${_vstgui_defs})
if(SMTG_WIN)
    target_compile_definitions(tiracon_vstgui PUBLIC SMTG_MODULE_IS_BUNDLE=1)
    target_sources(tiracon_vstgui PRIVATE "${VST3_SDK_ROOT}/public.sdk/source/vst/vstgui_win32_bundle_support.cpp")
endif()
if(SMTG_LINUX)
    target_sources(tiracon_vstgui PRIVATE "${VST3_SDK_ROOT}/public.sdk/source/vst/vstgui_linux_runloop_support.cpp")
endif()

if(CMAKE_CROSSCOMPILING AND NOT CMAKE_HOST_WIN32)
    # the SDK sets the folder icon with the Windows tool "attrib"; not available here (only cosmetic)
    function(smtg_target_add_folder_icon target icon)
    endfunction()
endif()

# editor content (VSTGUI, no SDK plugin classes): also used by the screenshot tool
add_library(tiracon_gui STATIC
    src/gui/theme.h
    src/gui/views.h
    src/gui/views.cpp
    src/gui/overlays.h
    src/gui/overlays.cpp
    src/gui/mainview.h
    src/gui/mainview.cpp
)
target_link_libraries(tiracon_gui PUBLIC tiracon_core tiracon_vstgui)

smtg_add_vst3plugin(Tiracon
    src/plugin/ids.h
    src/plugin/state.h
    src/plugin/bridge.h
    src/plugin/snapshot.h
    src/plugin/settings.h
    src/plugin/settings.cpp
    src/plugin/processor.h
    src/plugin/processor.cpp
    src/plugin/controller.h
    src/plugin/controller.cpp
    src/plugin/editor.h
    src/plugin/editor.cpp
    src/plugin/entry.cpp
)
target_compile_features(Tiracon PUBLIC cxx_std_17)
target_link_libraries(Tiracon PRIVATE sdk tiracon_core tiracon_gui)
if(MINGW)
    # no runtime DLLs needed on the Windows machine
    target_link_options(Tiracon PRIVATE -static -static-libgcc -static-libstdc++)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        # 32 bit: export GetPluginFactory / InitDll / ExitDll without the stdcall "@n" suffix
        target_link_options(Tiracon PRIVATE -Wl,--kill-at)
    endif()
endif()

# end-to-end test: loads the built plugin like a DAW (Linux only, not when cross compiling)
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(tiracon_hosttest
        test/hosttest.cpp
        "${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/plugprovider.cpp"
        "${VST3_SDK_ROOT}/public.sdk/source/common/memorystream.cpp"
        "${VST3_SDK_ROOT}/public.sdk/source/vst/hosting/module_linux.cpp"
    )
    target_link_libraries(tiracon_hosttest PRIVATE sdk_hosting ${CMAKE_DL_LIBS})
    add_dependencies(tiracon_hosttest Tiracon)

endif()

# renders the editor into PNG files without a window (for checking the layout; on Windows under Wine)
add_executable(tiracon_uishot test/uishot.cpp)
target_link_libraries(tiracon_uishot PRIVATE tiracon_gui ${CMAKE_DL_LIBS})
if(MINGW)
    target_link_options(tiracon_uishot PRIVATE -static -mconsole)
endif()
