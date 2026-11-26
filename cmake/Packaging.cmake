# Release packaging: the application icon, the macOS app bundle, Windows resources and install
# rules for the archives tools/release/package.sh and CI build (README "Installing"). No game data
# is ever installed.
#
#   cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DGT3R_FETCH_SDL=ON \
#         -DGT4R_MAC_BUNDLE=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0      # (macOS)
#   cmake --build build-release --target gt4r gt4tool
#   cmake --install build-release --prefix dist
#
# What a package holds: gt4r (gt4r.app on macOS), gt4tool (PLAYING.md: texture names, saves for
# checks), README.md, docs/PLAYING.md and docs/SAVE_EDITOR.md, LICENSE, THIRD-PARTY-NOTICES.md and
# the font licenses; on Windows SDL3.dll
# and ANGLE's DLLs; on Linux the .desktop file and the icon. Never the tests or gt4r_diff.

set(GT4R_PACKAGING_DIR ${CMAKE_SOURCE_DIR}/packaging)

# macOS: gt4r as gt4r.app. Off by default so that a development build stays build/gt4r, which
# the docs, the scripted runs and the tests' recipes name; the release builds turn it on.
option(GT4R_MAC_BUNDLE "macOS: build gt4r as the app bundle gt4r.app (release packages)" OFF)

if(APPLE AND GT4R_MAC_BUNDLE)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "GT4R_MAC_BUNDLE: set CMAKE_OSX_DEPLOYMENT_TARGET (the release uses 12.0); "
                        "Info.plist's LSMinimumSystemVersion comes from it")
  endif()
  # .icns made from packaging/icon.png with the macOS tools.
  set(_iconset ${CMAKE_BINARY_DIR}/gt4r.iconset)
  set(_icns ${CMAKE_BINARY_DIR}/gt4r.icns)
  set(_icon_cmds)
  foreach(sz 16 32 128 256 512)
    math(EXPR sz2 "${sz} * 2")
    list(APPEND _icon_cmds
      COMMAND sips -z ${sz} ${sz} ${GT4R_PACKAGING_DIR}/icon.png --out ${_iconset}/icon_${sz}x${sz}.png > /dev/null
      COMMAND sips -z ${sz2} ${sz2} ${GT4R_PACKAGING_DIR}/icon.png --out ${_iconset}/icon_${sz}x${sz}@2x.png > /dev/null)
  endforeach()
  add_custom_command(OUTPUT ${_icns}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${_iconset}
    ${_icon_cmds}
    COMMAND iconutil -c icns ${_iconset} -o ${_icns}
    DEPENDS ${GT4R_PACKAGING_DIR}/icon.png
    COMMENT "Building gt4r.icns")
  target_sources(gt4r PRIVATE ${_icns})
  set_source_files_properties(${_icns} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  # The identifier and name are the ones the app gives SDL (SDL_SetAppMetadata in app.cpp).
  set_target_properties(gt4r PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${GT4R_PACKAGING_DIR}/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER org.gt4revisited.gt4r
    MACOSX_BUNDLE_BUNDLE_NAME "GT4 Revisited"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE gt4r.icns)
elseif(WIN32)
  # The icon, and a Windows program (no console window beside it).
  target_sources(gt4r PRIVATE ${GT4R_PACKAGING_DIR}/gt4r.rc)
  set_target_properties(gt4r PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Windows: ANGLE (OpenGL ES on Direct3D 11) for the OpenGL ES renderer on GPUs without Vulkan,
# copied next to gt4r.exe and packaged with it: libEGL.dll, libGLESv2.dll and LICENSE-ANGLE.txt,
# built with vcpkg by packaging/angle.ps1 as CI's Windows package does. Without it the renderer
# uses the graphics driver's OpenGL ES.
set(GT3R_ANGLE_DIR "" CACHE PATH "Windows: folder with ANGLE's libEGL.dll, libGLESv2.dll and LICENSE-ANGLE.txt")
if(WIN32 AND GT3R_ANGLE_DIR)
  set(GT4R_ANGLE_FILES ${GT3R_ANGLE_DIR}/libEGL.dll ${GT3R_ANGLE_DIR}/libGLESv2.dll)
  foreach(f ${GT4R_ANGLE_FILES} ${GT3R_ANGLE_DIR}/LICENSE-ANGLE.txt)
    if(NOT EXISTS ${f})
      message(FATAL_ERROR "GT3R_ANGLE_DIR: ${f} is missing")
    endif()
  endforeach()
  add_custom_command(TARGET gt4r POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GT4R_ANGLE_FILES} $<TARGET_FILE_DIR:gt4r>)
endif()

set(_tools)
if(TARGET gt4tool)
  set(_tools gt4tool)
endif()
set(_docs ${CMAKE_SOURCE_DIR}/README.md ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md)
# The players' guide, in docs/ beside README.md as README links it.
set(_player_docs ${CMAKE_SOURCE_DIR}/docs/PLAYING.md ${CMAKE_SOURCE_DIR}/docs/SAVE_EDITOR.md)
set(_fonts ${GT3R_DIR}/third_party/fonts)

if(APPLE)
  if(NOT GT4R_MAC_BUNDLE)
    install(CODE "message(FATAL_ERROR \"A macOS package needs gt4r.app: configure with -DGT4R_MAC_BUNDLE=ON\")")
    return()
  endif()
  install(TARGETS gt4r BUNDLE DESTINATION .)
  if(_tools)
    install(TARGETS ${_tools} RUNTIME DESTINATION .)
  endif()
  install(FILES ${_docs} DESTINATION .)
  install(FILES ${_player_docs} DESTINATION docs)
  install(FILES ${_fonts}/barlow/OFL.txt DESTINATION . RENAME Barlow-OFL.txt)
  install(FILES ${_fonts}/promptfont/OFL.txt DESTINATION . RENAME PromptFont-OFL.txt)
  # Seal the finished bundle with an ad-hoc signature (no Apple account needed). The linker signs
  # only the executable; without sealing the bundle's resources the signature is invalid and
  # macOS calls a downloaded copy "damaged" instead of offering Open Anyway (README "Installing").
  install(CODE "execute_process(COMMAND codesign --force --deep --sign - \"\${CMAKE_INSTALL_PREFIX}/gt4r.app\" RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR \"codesign (ad-hoc) of gt4r.app failed\")
    endif()")
elseif(WIN32)
  install(TARGETS gt4r ${_tools} RUNTIME DESTINATION .)
  # SDL3.dll (GT3R_SDL_SHARED) and any other DLLs the app links.
  install(FILES $<TARGET_RUNTIME_DLLS:gt4r> DESTINATION .)
  if(GT3R_ANGLE_DIR)
    install(FILES ${GT4R_ANGLE_FILES} ${GT3R_ANGLE_DIR}/LICENSE-ANGLE.txt DESTINATION .)
  endif()
  install(FILES ${_docs} DESTINATION .)
  install(FILES ${_player_docs} DESTINATION docs)
  install(FILES ${_fonts}/barlow/OFL.txt DESTINATION . RENAME Barlow-OFL.txt)
  install(FILES ${_fonts}/promptfont/OFL.txt DESTINATION . RENAME PromptFont-OFL.txt)
else()
  include(GNUInstallDirs)
  install(TARGETS gt4r ${_tools} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES ${GT4R_PACKAGING_DIR}/gt4r.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES ${GT4R_PACKAGING_DIR}/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/1024x1024/apps
          RENAME gt4r.png)
  install(FILES ${_docs} DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/gt4r)
  install(FILES ${_player_docs} DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/gt4r/docs)
  install(FILES ${_fonts}/barlow/OFL.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/gt4r RENAME Barlow-OFL.txt)
  install(FILES ${_fonts}/promptfont/OFL.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/gt4r RENAME PromptFont-OFL.txt)
endif()
