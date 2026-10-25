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
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil)

add_executable(lumen
  src/main.cpp
  src/Track.h
  src/Probe.cpp            src/Probe.h
  src/Covers.cpp           src/Covers.h
  src/Palette.cpp          src/Palette.h
  src/Library.cpp          src/Library.h
  src/AudioEngine.cpp      src/AudioEngine.h
  src/Db.cpp               src/Db.h
  src/PluginManager.cpp    src/PluginManager.h
  src/Lyrics.cpp           src/Lyrics.h
  src/Marketplace.cpp      src/Marketplace.h
  src/UiState.h
  src/Player.cpp           src/Player.h
  src/Icons.cpp            src/Icons.h
  src/WavyProgress.cpp     src/WavyProgress.h
  src/miniaudio_impl.cpp
)

qt_add_resources(lumen "qml" PREFIX "/" FILES
  qml/Main.qml
  qml/Icon.qml
  qml/Cover.qml
  qml/Chip.qml
  qml/Switch.qml
  qml/Backdrop.qml
  qml/SearchPage.qml
  qml/LyricsPage.qml
  qml/SettingsPage.qml
  qml/Pill.qml
  qml/NavItem.qml
  qml/IconButton.qml
  qml/SearchField.qml
  qml/Scrubber.qml
  qml/ScrollIndicator.qml
  qml/Util.js
  qml/PlayerBar.qml
  qml/Sidebar.qml
  qml/AlbumsPage.qml
  qml/SongsPage.qml
  qml/AlbumDetail.qml
  qml/NowPlayingPanel.qml
  qml/EmptyState.qml
)

target_include_directories(lumen PRIVATE third_party)
target_link_libraries(lumen PRIVATE Qt6::Quick Qt6::Concurrent Qt6::Network PkgConfig::FFMPEG PkgConfig::SQLITE Threads::Threads ${CMAKE_DL_LIBS} m)
target_compile_options(lumen PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_source_files_properties(src/miniaudio_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
if(ipo_ok AND CMAKE_BUILD_TYPE STREQUAL "Release")
  set_property(TARGET lumen PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# Bundled (official) extensions: next to the binary for dev builds, share/ when installed.
add_custom_command(TARGET lumen POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins ${CMAKE_BINARY_DIR}/plugins)
install(TARGETS lumen RUNTIME DESTINATION bin)
install(DIRECTORY plugins/ DESTINATION share/lumen/plugins USE_SOURCE_PERMISSIONS)
