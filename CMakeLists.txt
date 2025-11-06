# SVAR - CMakeLists.txt
# SPDX-FileCopyrightText: 2014-2025 Arkadiusz Bokowy and contributors
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.11)

project(svar
	VERSION 2.1.0
	DESCRIPTION "Simple Voice Activated Recorder"
	LANGUAGES C)

option(BUILD_TESTING "Build the testing tree." OFF)
include(CTest)

if(CMAKE_BUILD_TYPE MATCHES Debug)
	set(DEBUG TRUE)
endif()

option(ENABLE_ALSA "Use ALSA as an audio back-end." ON)
option(ENABLE_PIPEWIRE "Use PipeWire as an audio back-end.")
option(ENABLE_PORTAUDIO "Use PortAudio as an audio back-end.")

# Make sure at least one audio back-end is enabled.
if(NOT (ENABLE_ALSA OR ENABLE_PIPEWIRE OR ENABLE_PORTAUDIO))
	message(FATAL_ERROR "At least one audio back-end must be enabled.")
endif()

option(ENABLE_MP3LAME "Enable MP3 support.")
option(ENABLE_SNDFILE "Enable WAV support.")
option(ENABLE_OPUS "Enable OPUS support.")
option(ENABLE_VORBIS "Enable VORBIS support.")

configure_file(
	${PROJECT_SOURCE_DIR}/config.h.in
	${PROJECT_BINARY_DIR}/config.h)

add_definitions(-DHAVE_CONFIG_H)
include_directories(${PROJECT_BINARY_DIR})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
find_package(PkgConfig REQUIRED)

add_library(svarcore STATIC src/pcm.c src/rbuf.c src/recorder.c src/writer.c)
add_executable(svar src/main.c)

target_link_libraries(svar svarcore m)

find_package(Threads REQUIRED)
target_link_libraries(svar Threads::Threads)

if(ENABLE_ALSA)
	pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
	target_sources(svar PRIVATE src/recorder-alsa.c)
	target_link_libraries(svar PkgConfig::ALSA)
endif()

if(ENABLE_PIPEWIRE)
	pkg_check_modules(PipeWire REQUIRED IMPORTED_TARGET libpipewire-0.3)
	target_sources(svar PRIVATE src/recorder-pipewire.c)
	target_link_libraries(svar PkgConfig::PipeWire)
endif()

if(ENABLE_PORTAUDIO)
	pkg_check_modules(PortAudio REQUIRED IMPORTED_TARGET portaudio-2.0)
	target_sources(svar PRIVATE src/recorder-portaudio.c)
	target_link_libraries(svar PkgConfig::PortAudio)
endif()

if(ENABLE_SNDFILE)
	pkg_check_modules(SNDFile REQUIRED IMPORTED_TARGET sndfile)
	target_sources(svarcore PRIVATE src/writer-wav.c)
	target_link_libraries(svarcore PkgConfig::SNDFile)
endif()

if(ENABLE_MP3LAME)
	find_package(Mp3Lame REQUIRED)
	target_sources(svarcore PRIVATE src/writer-mp3.c)
	target_link_libraries(svarcore Mp3Lame::Mp3Lame)
endif()

if(ENABLE_OPUS)
	pkg_check_modules(OggOpus REQUIRED IMPORTED_TARGET libopusenc)
	target_sources(svarcore PRIVATE src/writer-opus.c)
	target_link_libraries(svarcore PkgConfig::OggOpus)
endif()

if(ENABLE_VORBIS)
	pkg_check_modules(OggVorbis REQUIRED IMPORTED_TARGET vorbis vorbisenc ogg)
	target_sources(svarcore PRIVATE src/writer-vorbis.c)
	target_link_libraries(svarcore PkgConfig::OggVorbis)
endif()

if(BUILD_TESTING)

	file(GLOB_RECURSE TEST_SOURCES test/tc-*.c)
	pkg_check_modules(Check REQUIRED IMPORTED_TARGET check)

	foreach(TEST_SOURCE ${TEST_SOURCES})
		get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
		add_executable(${TEST_NAME} ${TEST_SOURCE} test/test-runner.c)
		target_include_directories(${TEST_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)
		target_link_libraries(${TEST_NAME} svarcore PkgConfig::Check)
		add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
	endforeach()

endif()

install(TARGETS svar
	RUNTIME DESTINATION bin)
