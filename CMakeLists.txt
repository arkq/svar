# SVAR - CMakeLists.txt
# SPDX-FileCopyrightText: 2014-2025 Arkadiusz Bokowy and contributors
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.11)

project(svar
	VERSION 1.3.1
	DESCRIPTION "Simple Voice Activated Recorder"
	LANGUAGES C)

if(CMAKE_BUILD_TYPE MATCHES Debug)
	set(DEBUG TRUE)
endif()

if(CMAKE_SYSTEM_NAME MATCHES Linux)
	option(ENABLE_PORTAUDIO "Use PortAudio as an audio back-end.")
else()
	set(ENABLE_PORTAUDIO ON)
endif()

option(ENABLE_MP3LAME "Enable MP3 support.")
option(ENABLE_SNDFILE "Enable WAV support.")
option(ENABLE_VORBIS "Enable OGG support.")

configure_file(
	${PROJECT_SOURCE_DIR}/config.h.in
	${PROJECT_BINARY_DIR}/config.h)

add_definitions(-DHAVE_CONFIG_H)
include_directories(${PROJECT_BINARY_DIR})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
find_package(PkgConfig REQUIRED)

add_executable(svar
	src/writer.c
	src/main.c)

target_link_libraries(svar m)

find_package(Threads REQUIRED)
target_link_libraries(svar Threads::Threads)

if(ENABLE_PORTAUDIO)
	pkg_check_modules(PortAudio REQUIRED IMPORTED_TARGET portaudio-2.0)
	target_link_libraries(svar PkgConfig::PortAudio)
else()
	pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
	target_link_libraries(svar PkgConfig::ALSA)
endif()

if(ENABLE_SNDFILE)
	pkg_check_modules(SNDFile REQUIRED IMPORTED_TARGET sndfile)
	target_sources(svar PRIVATE src/writer-wav.c)
	target_link_libraries(svar PkgConfig::SNDFile)
endif()

if(ENABLE_MP3LAME)
	find_package(Mp3Lame REQUIRED)
	target_sources(svar PRIVATE src/writer-mp3.c)
	target_link_libraries(svar Mp3Lame::Mp3Lame)
endif()

if(ENABLE_VORBIS)
	pkg_check_modules(VorbisOgg REQUIRED IMPORTED_TARGET vorbis vorbisenc ogg)
	target_sources(svar PRIVATE src/writer-ogg.c)
	target_link_libraries(svar PkgConfig::VorbisOgg)
endif()

install(TARGETS svar
	RUNTIME DESTINATION bin)
