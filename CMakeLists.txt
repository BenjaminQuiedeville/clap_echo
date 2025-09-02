cmake_minimum_required(VERSION 3.22)

project(clap_echo VERSION 0.1 LANGUAGES C CXX)

if (MSVC)
    add_compile_options(/MD)
else(CLANG)
    add_compile_options(-D_DLL -Wall -Wextra -Wno-pragma-pack -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers) # -ftime-trace=clang_logs.json)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES FALSE CACHE BOOL "Download Dependencies")
set(VST3_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/vst3sdk)
set(CLAP_SDK_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../clap)
set(CLAP_VST3_TUID_STRING "cech")


add_library(${PROJECT_NAME}_static STATIC source/plugin.cpp source/imgui_code.cpp)

target_include_directories(${PROJECT_NAME}_static PRIVATE imgui ${CLAP_SDK_ROOT}/include)

target_link_libraries(${PROJECT_NAME}_static PRIVATE opengl32.lib)


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/clap-wrapper)
set(VST3_TARGET ${PROJECT_NAME}_vst3)

make_clapfirst_plugins(
    TARGET_NAME         ${PROJECT_NAME}           # name of the output target. It will be postpended _clap, _vst3, _all
    IMPL_TARGET         ${PROJECT_NAME}_static    # name of the target you set up with your static library
    OUTPUT_NAME         ${PROJECT_NAME}           # output name of your CLAP, VST3, etc...
    ENTRY_SOURCE        source/plugin_entry.cpp   # source file of the small cpp with the export directives and link to init
    COPY_AFTER_BUILD    FALSE                     # If true, on mac and lin the clap/vst3/etc... will install locally
    WINDOWS_FOLDER_VST3 FALSE                     # True for a filder, false for single file (default)
    PLUGIN_FORMATS      CLAP VST3                 # A list of plugin formats, "CLAP" "VST3" "AUV2"
)

