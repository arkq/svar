# CMakeLists.txt
# Copyright (c) 2014-2020 Arkadiusz Bokowy
#
# This file is a part of SVAR.
#
# This project is licensed under the terms of the MIT license.

cmake_minimum_required(VERSION 3.1)

project(svar
	VERSION 1.2.0
	DESCRIPTION "Simple Voice Activated Recorder"
	LANGUAGES C)

if(CMAKE_BUILD_TYPE MATCHES Debug)
	set(DEBUG TRUE)
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

set(SRCS
	src/main.c)

add_executable(svar ${SRCS})

target_link_libraries(svar m)

find_package(Threads REQUIRED)
target_link_libraries(svar Threads::Threads)

pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
target_link_libraries(svar PkgConfig::ALSA)

if(ENABLE_SNDFILE)
	pkg_check_modules(SNDFile REQUIRED IMPORTED_TARGET sndfile)
	target_sources(svar PRIVATE src/writer_sndfile.c)
	target_link_libraries(svar PkgConfig::SNDFile)
endif()

if(ENABLE_MP3LAME)
	find_package(Mp3Lame REQUIRED)
	target_sources(svar PRIVATE src/writer_mp3lame.c)
	target_link_libraries(svar Mp3Lame::Mp3Lame)
endif()

if(ENABLE_VORBIS)
	pkg_check_modules(VorbisOgg REQUIRED IMPORTED_TARGET vorbis vorbisenc ogg)
	target_sources(svar PRIVATE src/writer_vorbis.c)
	target_link_libraries(svar PkgConfig::VorbisOgg)
endif()

install(TARGETS svar
	RUNTIME DESTINATION bin)
