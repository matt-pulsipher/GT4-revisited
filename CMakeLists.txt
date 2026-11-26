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
set(GT3R_DIR ${CMAKE_SOURCE_DIR}/engine/gt3r)
if(NOT EXISTS ${GT3R_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "engine/gt3r is missing. Run: git submodule update --init")
endif()

# SDL3, zlib and libchdr, found or fetched the way GT3 Revisited does.
include(${GT3R_DIR}/cmake/Dependencies.cmake)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
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
# Its UI toolkit (src/app there is the GT3 game's executable; these files are the
# game-independent part).
set(GT3R_UI_SOURCES ${GT3R_DIR}/src/app/ui.cpp ${GT3R_DIR}/src/app/ui_font.cpp ${GT3R_DIR}/src/app/button_prompts.cpp ${GT3R_DIR}/src/app/text_keyboard.cpp)

# ---------------------------------------------------------------------------
# GT4 Revisited: everything under src/gt4r except the executable (src/gt4r/app).
# ---------------------------------------------------------------------------
configure_file(${CMAKE_SOURCE_DIR}/cmake/gt4r_version.h.in ${CMAKE_BINARY_DIR}/generated/gt4r_version.h @ONLY)
file(GLOB_RECURSE GT4R_ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/gt4r/*.cpp)
list(FILTER GT4R_ENGINE_SOURCES EXCLUDE REGEX "/src/gt4r/app/")
add_library(gt4r_engine STATIC ${GT4R_ENGINE_SOURCES})
target_include_directories(gt4r_engine PUBLIC src)
target_link_libraries(gt4r_engine PUBLIC gt3r_engine)

file(GLOB GT4R_APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/gt4r/app/*.cpp)
add_executable(gt4r ${GT4R_APP_SOURCES} ${GT3R_UI_SOURCES})
target_link_libraries(gt4r PRIVATE gt4r_engine gt3r_ui_fonts)
target_include_directories(gt4r SYSTEM PRIVATE ${GT3R_DIR}/third_party/stb)

if(GT4R_BUILD_TOOLS)
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

if(GT4R_BUILD_TESTS)
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
