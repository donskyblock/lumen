cmake_minimum_required(VERSION 3.21)
project(lumen VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Quick Concurrent Network)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
if(WIN32)
  # vcpkg supplies these packages when using scripts/setup.ps1.
  find_package(unofficial-sqlite3 CONFIG REQUIRED)
  find_package(FFMPEG REQUIRED)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil)
endif()

add_executable(lumen
  src/main.cpp
  src/Track.h
  src/Probe.cpp            src/Probe.h
  src/Covers.cpp           src/Covers.h
  src/Palette.cpp          src/Palette.h
  src/Library.cpp          src/Library.h          src/Discovery.cpp
  src/PlayStats.h
  src/TextUtil.h
  src/Playlists.cpp        src/Playlists.h
  src/Profiles.cpp         src/Profiles.h
  src/AudioEngine.cpp      src/AudioEngine.h
  src/Db.cpp               src/Db.h
  src/Catalog.cpp          src/Catalog.h
  src/Resolver.cpp         src/Resolver.h
  src/Metadata.cpp         src/Metadata.h
  src/Presence.cpp         src/Presence.h
  src/SongIndex.cpp        src/SongIndex.h
  src/Taste.cpp            src/Taste.h
  src/SpotifyImport.cpp    src/SpotifyImport.h
  src/PluginManager.cpp    src/PluginManager.h
  src/Lyrics.cpp           src/Lyrics.h
  src/Marketplace.cpp      src/Marketplace.h
  src/UiState.h
  src/Bus.h
  src/Player.cpp           src/Player.h
  src/Icons.cpp            src/Icons.h
  src/WavyProgress.cpp     src/WavyProgress.h
  src/miniaudio_impl.cpp
)

file(GLOB QML_FILES RELATIVE ${CMAKE_SOURCE_DIR} CONFIGURE_DEPENDS qml/*.qml qml/*.js)
qt_add_resources(lumen "qml" PREFIX "/" FILES ${QML_FILES})

target_include_directories(lumen PRIVATE third_party)
if(WIN32)
  target_include_directories(lumen PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(lumen PRIVATE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(lumen PRIVATE Qt6::Quick Qt6::Concurrent Qt6::Network ${FFMPEG_LIBRARIES} unofficial::sqlite3::sqlite3 Threads::Threads ZLIB::ZLIB)
  target_compile_options(lumen PRIVATE /W4 /wd4100)
  set_source_files_properties(src/miniaudio_impl.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
  target_link_libraries(lumen PRIVATE Qt6::Quick Qt6::Concurrent Qt6::Network PkgConfig::FFMPEG PkgConfig::SQLITE Threads::Threads ZLIB::ZLIB ${CMAKE_DL_LIBS} m)
  target_compile_options(lumen PRIVATE -Wall -Wextra -Wno-unused-parameter)
  set_source_files_properties(src/miniaudio_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
if(ipo_ok AND CMAKE_BUILD_TYPE STREQUAL "Release")
  set_property(TARGET lumen PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# Bundled (official) extensions: next to the binary for dev builds, share/ when installed.
add_custom_command(TARGET lumen POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:lumen>/plugins)
install(TARGETS lumen RUNTIME DESTINATION bin)
install(DIRECTORY plugins/ DESTINATION share/lumen/plugins USE_SOURCE_PERMISSIONS)
