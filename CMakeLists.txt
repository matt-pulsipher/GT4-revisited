cmake_minimum_required(VERSION 3.21)
project(GT4Revisited VERSION 0.1.0 LANGUAGES C CXX)

# GT4 Revisited: a reimplementation of the Gran Turismo 4 engine on GT3 Revisited's engine.
# No game data is part of this project; everything is read at runtime from the user's own
# disc image.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GT4R_BUILD_TOOLS "Build the gt4tool command line utility" ON)
option(GT4R_BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------------
# The shared engine: GT3 Revisited, a git submodule (docs/ARCHITECTURE.md).
# ---------------------------------------------------------------------------
# (-DGT3R_DIR=<a GT3 Revisited checkout> builds against another engine tree, to try an engine change first.)
if(NOT DEFINED GT3R_DIR)
  set(GT3R_DIR ${CMAKE_SOURCE_DIR}/engine/gt3r)
endif()
if(NOT EXISTS ${GT3R_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "engine/gt3r is missing. Run: git submodule update --init")
endif()

if(IOS)
  # iPhone and iPad (docs/BUILDING.md "iOS"), as GT3 Revisited builds them: the warnings are the flags below, not Xcode's iOS
  # defaults on top (-Wshorten-64-to-32); libtool's note on every file that compiles to nothing on iOS (src/xr without GT3R_XR,
  # SDL's other platforms) is turned off for the static libraries (gt3r_engine, gt4r_engine, SDL3-static).
  set(CMAKE_XCODE_ATTRIBUTE_GCC_WARN_64_TO_32_BIT_CONVERSION NO)
  set(GT3R_LIBTOOL_QUIET -no_warning_for_no_symbols)
endif()

# SDL3, zlib and libchdr, found or fetched the way GT3 Revisited does (iOS: always SDL built from source).
include(${GT3R_DIR}/cmake/Dependencies.cmake)

if(MSVC)
  # /MP: Visual Studio's MSBuild compiles a project's files one at a time without it (CI's Windows build
  # took about 3 times as long as the others).
  add_compile_options(/W4 /permissive- /utf-8 /MP)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# Its engine library, built from the submodule's sources as its own CMakeLists.txt builds it
# (that file is written for the top of a tree, so it is not added as a subdirectory).
file(GLOB_RECURSE GT3R_ENGINE_SOURCES CONFIGURE_DEPENDS ${GT3R_DIR}/src/*.cpp)
list(FILTER GT3R_ENGINE_SOURCES EXCLUDE REGEX "/src/app/")
add_library(gt3r_engine STATIC ${GT3R_ENGINE_SOURCES})
target_include_directories(gt3r_engine PUBLIC ${GT3R_DIR}/src)
target_link_libraries(gt3r_engine PUBLIC SDL3::SDL3 ZLIB::ZLIB chdr-static)
if(APPLE)
  target_link_libraries(gt3r_engine PUBLIC objc)
endif()
if(IOS)
  target_link_libraries(gt3r_engine PUBLIC "-framework UniformTypeIdentifiers") # the engine's platform/ios_files.cpp (UTType)
  set_target_properties(gt3r_engine PROPERTIES STATIC_LIBRARY_OPTIONS ${GT3R_LIBTOOL_QUIET})
endif()
if(WIN32)
  target_link_libraries(gt3r_engine PUBLIC ws2_32)
endif()
if(WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID))
  set(_gt3r_gles_default ON)
else()
  set(_gt3r_gles_default OFF)
endif()
option(GT3R_GLES "OpenGL ES 3.0 fallback renderer" ${_gt3r_gles_default})
if(GT3R_GLES)
  target_compile_definitions(gt3r_engine PUBLIC GT3R_GLES=1)
  target_include_directories(gt3r_engine SYSTEM PRIVATE ${GT3R_DIR}/third_party/khronos)
endif()
# VR headsets through OpenXR (the engine's src/xr; docs/VR.md): GT3R_XR (its option in the engine's Dependencies.cmake, which then
# takes SDL's development branch with SDL_GPU's OpenXR support). Without it none of the VR code is compiled.
if(GT3R_XR)
  target_compile_definitions(gt3r_engine PUBLIC GT3R_XR=1)
  target_include_directories(gt3r_engine SYSTEM PUBLIC ${GT3R_OPENXR_INCLUDE_DIR})
endif()

# gt3r_version.h, which the engine includes: the submodule's own version.
file(STRINGS ${GT3R_DIR}/CMakeLists.txt _gt3r_project REGEX "^project\\(GT3Revisited VERSION ")
if(NOT _gt3r_project MATCHES "VERSION ([0-9]+)\\.([0-9]+)\\.([0-9]+)")
  message(FATAL_ERROR "Could not read GT3 Revisited's version from ${GT3R_DIR}/CMakeLists.txt")
endif()
function(gt4r_write_gt3r_version)
  set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
  set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
  set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_3})
  set(GT3R_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
  configure_file(${GT3R_DIR}/cmake/gt3r_version.h.in ${CMAKE_BINARY_DIR}/generated/gt3r_version.h @ONLY)
endfunction()
gt4r_write_gt3r_version()
target_include_directories(gt3r_engine PUBLIC ${CMAKE_BINARY_DIR}/generated)

# The UI fonts its Ui draws with, embedded as in GT3 Revisited.
set(GT3R_UI_FONTS
  ${GT3R_DIR}/third_party/fonts/barlow/BarlowSemiCondensed-Medium.ttf
  ${GT3R_DIR}/third_party/fonts/barlow/BarlowCondensed-SemiBold.ttf
  ${GT3R_DIR}/third_party/fonts/promptfont/promptfont.ttf)
set(GT3R_UI_FONT_SRC ${CMAKE_BINARY_DIR}/generated/ui_font_data.cpp)
list(JOIN GT3R_UI_FONTS "|" _ui_font_files)
add_custom_command(OUTPUT ${GT3R_UI_FONT_SRC}
  COMMAND ${CMAKE_COMMAND} -DOUT=${GT3R_UI_FONT_SRC} -DNAMESPACE=gt3r::font_data
          -DNAMES=kBody|kTitle|kPrompt -DFILES=${_ui_font_files}
          -P ${GT3R_DIR}/cmake/EmbedFiles.cmake
  DEPENDS ${GT3R_UI_FONTS} ${GT3R_DIR}/cmake/EmbedFiles.cmake
  COMMENT "Embedding UI fonts"
  VERBATIM)
add_library(gt3r_ui_fonts STATIC ${GT3R_UI_FONT_SRC})
# The application icon (packaging/icon.png itself, so the window's icon and the packages' can never differ), embedded for
# SDL_SetWindowIcon: a development build's bare binary has no bundle to take one from.
set(GT4R_APP_ICON_PNG ${CMAKE_SOURCE_DIR}/packaging/icon.png)
set(GT4R_APP_ICON_SRC ${CMAKE_BINARY_DIR}/generated/app_icon_data.cpp)
add_custom_command(OUTPUT ${GT4R_APP_ICON_SRC}
  COMMAND ${CMAKE_COMMAND} -DOUT=${GT4R_APP_ICON_SRC} -DNAMESPACE=gt4r::icon_data -DNAMES=kAppIconPng
          -DFILES=${GT4R_APP_ICON_PNG} -P ${GT3R_DIR}/cmake/EmbedFiles.cmake
  DEPENDS ${GT4R_APP_ICON_PNG} ${GT3R_DIR}/cmake/EmbedFiles.cmake
  COMMENT "Embedding the application icon"
  VERBATIM)
add_library(gt4r_app_icon STATIC ${GT4R_APP_ICON_SRC})
# Its UI toolkit (src/app there is the GT3 game's executable; these files are the
# game-independent part).
set(GT3R_UI_SOURCES ${GT3R_DIR}/src/app/ui.cpp ${GT3R_DIR}/src/app/ui_font.cpp ${GT3R_DIR}/src/app/button_prompts.cpp ${GT3R_DIR}/src/app/text_keyboard.cpp
    ${GT3R_DIR}/src/app/screen_touch_overlay.cpp) # (and the on-screen controls of phones and tablets)

# ---------------------------------------------------------------------------
# GT4 Revisited: everything under src/gt4r except the executable (src/gt4r/app).
# ---------------------------------------------------------------------------
configure_file(${CMAKE_SOURCE_DIR}/cmake/gt4r_version.h.in ${CMAKE_BINARY_DIR}/generated/gt4r_version.h @ONLY)
file(GLOB_RECURSE GT4R_ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/gt4r/*.cpp)
list(FILTER GT4R_ENGINE_SOURCES EXCLUDE REGEX "/src/gt4r/app/")
add_library(gt4r_engine STATIC ${GT4R_ENGINE_SOURCES})
target_include_directories(gt4r_engine PUBLIC src)
target_link_libraries(gt4r_engine PUBLIC gt3r_engine)
if(IOS)
  set_target_properties(gt4r_engine PROPERTIES STATIC_LIBRARY_OPTIONS ${GT3R_LIBTOOL_QUIET})
endif()

file(GLOB GT4R_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/gt4r/app/*.cpp)
if(ANDROID)
  # Android (android/README.md): the game is libmain.so, which SDL's Java glue loads.
  add_library(gt4r SHARED ${GT4R_APP_SOURCES} ${GT3R_UI_SOURCES})
  set_target_properties(gt4r PROPERTIES OUTPUT_NAME main)
else()
  add_executable(gt4r ${GT4R_APP_SOURCES} ${GT3R_UI_SOURCES})
endif()
target_link_libraries(gt4r PRIVATE gt4r_engine gt3r_ui_fonts gt4r_app_icon)
target_include_directories(gt4r SYSTEM PRIVATE ${GT3R_DIR}/third_party/stb)
# Windows VR builds: the OpenXR loader next to gt4r.exe (the engine's Dependencies.cmake downloads it).
if(GT3R_OPENXR_LOADER)
  add_custom_command(TARGET gt4r POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GT3R_OPENXR_LOADER} $<TARGET_FILE_DIR:gt4r>)
endif()

if(GT4R_BUILD_TOOLS AND NOT IOS) # (no command-line tools or tests on a phone)
  file(GLOB GT4TOOL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/gt4tool/*.cpp)
  add_executable(gt4tool ${GT4TOOL_SOURCES})
  target_link_libraries(gt4tool PRIVATE gt4r_engine)
  # A developer's check: routines of the user's own program run in the shared engine's interpreter, against ours.
  file(GLOB GT4DIFF_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/gt4diff/*.cpp)
  set(GT3R_EE ${GT3R_DIR}/tools/diffharness)
  add_executable(gt4r_diff ${GT4DIFF_SOURCES} ${GT3R_EE}/ee.cpp ${GT3R_EE}/ee_fpu.cpp ${GT3R_EE}/ee_mmi.cpp ${GT3R_EE}/ee_vu0.cpp)
  target_include_directories(gt4r_diff PRIVATE ${GT3R_EE})
  target_link_libraries(gt4r_diff PRIVATE gt4r_engine ${CMAKE_DL_LIBS})
endif()

if(GT4R_BUILD_TESTS AND NOT IOS)
  enable_testing()
  file(GLOB GT4R_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
  # (with the checks against the user's own program: tools/gt4diff/diff.cpp on the shared engine's interpreter)
  set(GT3R_EE_SOURCES ${GT3R_DIR}/tools/diffharness/ee.cpp ${GT3R_DIR}/tools/diffharness/ee_fpu.cpp ${GT3R_DIR}/tools/diffharness/ee_mmi.cpp
      ${GT3R_DIR}/tools/diffharness/ee_vu0.cpp)
  file(GLOB GT4DIFF_CHECKS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/gt4diff/*.cpp)
  list(FILTER GT4DIFF_CHECKS EXCLUDE REGEX "/main\\.cpp$")
  add_executable(gt4r_tests ${GT4R_TEST_SOURCES} ${GT4DIFF_CHECKS} ${GT3R_EE_SOURCES})
  target_include_directories(gt4r_tests PRIVATE ${GT3R_DIR}/tools/diffharness ${CMAKE_SOURCE_DIR}/tools/gt4diff)
  target_link_libraries(gt4r_tests PRIVATE gt4r_engine ${CMAKE_DL_LIBS})
  add_test(NAME unit COMMAND gt4r_tests)
endif()

# Icon, app bundle and install rules for the release packages.
include(cmake/Packaging.cmake)
