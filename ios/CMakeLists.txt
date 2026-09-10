# pIRCIS for iOS. The same program as the board and the desktop emulator: the
# UI, the interpreter and storage are the shared sources, SDL2 stands in for
# the panel exactly as it does on the desktop, and the phone's screen is the
# window. Generate an Xcode project with ios/setup.sh, or by hand:
#
#   cmake -S ios -B ios/build -G Xcode -DCMAKE_SYSTEM_NAME=iOS
#
# This build carries no packed program (PackAbsent.cpp stands in for Pack.cpp).
cmake_minimum_required(VERSION 3.20)
project(pIRCIS LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "" FORCE)
endif()

# The signing team, so a regenerated project is ready to run on a phone
# without choosing it again in Xcode. Override with -DPIRCIS_TEAM=... to
# sign as somebody else; a team id is public, it is in every app's bundle.
set(PIRCIS_TEAM "3G8P5Y2L25" CACHE STRING "Apple developer team id for signing")
set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${PIRCIS_TEAM}")
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic")

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
# The version the whole program carries, read from the one place it is kept.
file(STRINGS "${ROOT}/src/Version.h" PIRCIS_VERSION_LINE REGEX "define PIRCIS_VERSION")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" PIRCIS_VERSION "${PIRCIS_VERSION_LINE}")
set(THIRD "${ROOT}/third_party")
if(NOT EXISTS "${THIRD}/SDL/include/SDL.h" OR NOT EXISTS "${THIRD}/LovyanGFX/src/LovyanGFX.hpp")
  message(FATAL_ERROR "SDL and LovyanGFX are fetched by ios/setup.sh; run that first")
endif()

# SDL2, static, with its iOS entry point (SDL2main).
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory("${THIRD}/SDL" sdl EXCLUDE_FROM_ALL)
# Neither library is ours to tidy: keep their warnings out of the issue list.
foreach(t SDL2-static SDL2main)
  if(TARGET ${t})
    target_compile_options(${t} PRIVATE -w)
    target_compile_definitions(${t} PRIVATE GLES_SILENCE_DEPRECATION=1)
    set_target_properties(${t} PROPERTIES
      XCODE_ATTRIBUTE_GCC_WARN_INHIBIT_ALL_WARNINGS YES
      XCODE_ATTRIBUTE_OTHER_LIBTOOLFLAGS "-no_warning_for_no_symbols")
  endif()
endforeach()

# LovyanGFX picks its SDL platform when SDL.h is on the include path.
file(GLOB_RECURSE LGFX_SRC CONFIGURE_DEPENDS
     "${THIRD}/LovyanGFX/src/*.cpp" "${THIRD}/LovyanGFX/src/*.c")
add_library(lgfx STATIC ${LGFX_SRC})
target_include_directories(lgfx PUBLIC "${THIRD}/LovyanGFX/src")
target_compile_definitions(lgfx PUBLIC LGFX_USE_V1)
target_compile_options(lgfx PRIVATE -w)
# Most of LovyanGFX is for other hardware and compiles to nothing here, which
# the archiver would otherwise mention once per file.
set_target_properties(lgfx PROPERTIES
  XCODE_ATTRIBUTE_GCC_WARN_INHIBIT_ALL_WARNINGS YES
  XCODE_ATTRIBUTE_OTHER_LIBTOOLFLAGS "-no_warning_for_no_symbols")
target_link_libraries(lgfx PUBLIC SDL2::SDL2-static)

# The program. Everything here is the same source the board runs.
file(GLOB APP_SRC CONFIGURE_DEPENDS
     "${ROOT}/src/*.cpp" "${ROOT}/src/platform/*.cpp"
     "${ROOT}/src/platform/*.mm"
     "${ROOT}/lib/ircis/src/*.cpp" "${ROOT}/lib/program/src/*.cpp")
list(APPEND APP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/PackAbsent.cpp")
# One Objective-C++ file, compiled by the C++ toolchain told what it is
# looking at; enabling CMake's own Objective-C support pushed C++ flags at
# SDL's plain Objective-C sources.
set_source_files_properties("${ROOT}/src/platform/Platform_ios.mm" PROPERTIES
  LANGUAGE CXX COMPILE_OPTIONS "-x;objective-c++;-fobjc-arc")

add_executable(pircis MACOSX_BUNDLE ${APP_SRC})
target_compile_definitions(pircis PRIVATE SK_HOST=1 LGFX_USE_V1 IRCIS_TRAIL_LEN=24)
target_compile_options(pircis PRIVATE -Wall -Wno-unused-parameter -Wno-unused-function)
target_include_directories(pircis PRIVATE
  "${ROOT}/src" "${ROOT}/lib/ircis/src" "${ROOT}/lib/program/src" "${ROOT}/lib/pack/src")
target_link_libraries(pircis PRIVATE lgfx SDL2::SDL2main SDL2::SDL2-static "-framework UniformTypeIdentifiers")

# The icon: the tile from the IRCIS banner, by the language's author. Only
# the Xcode build compiles an asset catalogue.
if(CMAKE_GENERATOR STREQUAL "Xcode")
  # ...and the privacy manifest the App Store asks for.
  target_sources(pircis PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Assets.xcassets"
                                "${CMAKE_CURRENT_SOURCE_DIR}/PrivacyInfo.xcprivacy")
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Assets.xcassets"
                              "${CMAKE_CURRENT_SOURCE_DIR}/PrivacyInfo.xcprivacy" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(pircis PROPERTIES XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
endif()

set_target_properties(pircis PROPERTIES
  OUTPUT_NAME "pIRCIS"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  MACOSX_BUNDLE_BUNDLE_NAME "pIRCIS"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.jamesleaver.pircis"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PIRCIS_VERSION}"
  MACOSX_BUNDLE_BUNDLE_VERSION "3"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.jamesleaver.pircis"
  XCODE_ATTRIBUTE_MARKETING_VERSION "${PIRCIS_VERSION}"
  XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "3"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
  # An archive is only an app archive, the kind the Organizer will upload,
  # when the app is installed into it at /Applications. CMake skips the
  # install step by default, which left a "generic" archive with nothing
  # to distribute but its contents.
  XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
  XCODE_ATTRIBUTE_INSTALL_PATH "/Applications"
  # Crash reports are symbolicated from a dSYM in the archive. CMake's
  # default keeps the debug information inside the binary, and the upload
  # warns that the archive has none.
  XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
  XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES"
  # CMake pins every target's products folder, and an archive collects
  # dSYMs from its own products folder under SYMROOT, so ours was never
  # found. Writing the dSYM under SYMROOT puts it beside the product for an
  # ordinary build and inside the archive's intermediates for an archive.
  XCODE_ATTRIBUTE_DWARF_DSYM_FOLDER_PATH "$(SYMROOT)/$(CONFIGURATION)$(EFFECTIVE_PLATFORM_NAME)"
  XCODE_ATTRIBUTE_GCC_OPTIMIZATION_LEVEL[variant=Release] "2"
  XCODE_GENERATE_SCHEME ON)
