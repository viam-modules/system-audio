cmake_minimum_required(VERSION 3.10)
set(CMAKE_PROJECT_VERSION 0.0.1)
set(CMAKE_CXX_STANDARD 17)

project(audio
    DESCRIPTION "Viam module for audio input and output"
    HOMEPAGE_URL https://github.com/viam-modules/audio
    LANGUAGES CXX
)

add_executable(audio-module
    src/main.cpp
    src/microphone.cpp
    src/audio_stream.cpp
    src/audio_buffer.cpp
    src/audio_codec.cpp
    src/speaker.cpp
    src/mp3_encoder.cpp
)

find_package(viam-cpp-sdk REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(libmp3lame REQUIRED)

pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)

# Find the static library
find_library(PORTAUDIO_STATIC_LIB
    NAMES libportaudio.a
    PATHS ${PORTAUDIO_LIBRARY_DIRS} /opt/homebrew/opt/portaudio/lib /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu
    NO_DEFAULT_PATH
)

# On macOS, PortAudio needs these frameworks when statically linked
# These are automatically avaliable on every mac
if(APPLE)
    find_library(COREAUDIO_LIBRARY CoreAudio REQUIRED)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox REQUIRED)
    find_library(AUDIOUNIT_LIBRARY AudioUnit REQUIRED)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
    find_library(CORESERVICES_LIBRARY CoreServices REQUIRED)
endif()

# On Linux, PortAudio needs ALSA and JACK when statically linked
if(LINUX)
    find_package(ALSA REQUIRED)
    pkg_check_modules(JACK jack)
endif()

target_link_libraries(audio-module
PRIVATE Threads::Threads
PRIVATE viam-cpp-sdk::viamsdk
PRIVATE ${PORTAUDIO_STATIC_LIB}
PRIVATE libmp3lame::libmp3lame
)

# Link platform-specific dependencies
if(APPLE)
    target_link_libraries(audio-module
        PRIVATE ${COREAUDIO_LIBRARY}
        PRIVATE ${AUDIOTOOLBOX_LIBRARY}
        PRIVATE ${AUDIOUNIT_LIBRARY}
        PRIVATE ${COREFOUNDATION_LIBRARY}
        PRIVATE ${CORESERVICES_LIBRARY}
    )
endif()

if(LINUX)
    target_link_libraries(audio-module PRIVATE ALSA::ALSA)
    if(JACK_FOUND)
        target_link_libraries(audio-module PRIVATE ${JACK_LIBRARIES})
        target_link_directories(audio-module PRIVATE ${JACK_LIBRARY_DIRS})
    endif()
endif()

target_include_directories(audio-module
    PRIVATE ${PORTAUDIO_INCLUDE_DIRS}
)
target_compile_options(audio-module PRIVATE ${PORTAUDIO_CFLAGS_OTHER})

install(TARGETS audio-module DESTINATION .)
install(FILES meta.json DESTINATION .)

enable_testing()
add_subdirectory(test)
