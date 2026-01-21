include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

function(audio_add_gtest SOURCE_FILE_NAME)
    get_filename_component(TEST_EXECUTABLE_NAME ${SOURCE_FILE_NAME} NAME_WE)

    add_executable(${TEST_EXECUTABLE_NAME}
        ${SOURCE_FILE_NAME}
        ${CMAKE_SOURCE_DIR}/src/microphone.cpp
        ${CMAKE_SOURCE_DIR}/src/audio_codec.cpp
        ${CMAKE_SOURCE_DIR}/src/audio_stream.cpp
        ${CMAKE_SOURCE_DIR}/src/discovery.cpp
        ${CMAKE_SOURCE_DIR}/src/audio_buffer.cpp
        ${CMAKE_SOURCE_DIR}/src/speaker.cpp
        ${CMAKE_SOURCE_DIR}/src/mp3_encoder.cpp
        ${CMAKE_SOURCE_DIR}/src/mp3_decoder.cpp
    )
    target_link_libraries(${TEST_EXECUTABLE_NAME}
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        viam-cpp-sdk::viamsdk
        ${PORTAUDIO_STATIC_LIB}
        libmp3lame::libmp3lame
        soxr::soxr
    )

    # Link platform-specific dependencies for portaudio
    if(APPLE)
        target_link_libraries(${TEST_EXECUTABLE_NAME}
            ${COREAUDIO_LIBRARY}
            ${AUDIOTOOLBOX_LIBRARY}
            ${AUDIOUNIT_LIBRARY}
            ${COREFOUNDATION_LIBRARY}
            ${CORESERVICES_LIBRARY}
        )
    endif()

    if(LINUX)
        target_link_libraries(${TEST_EXECUTABLE_NAME} ALSA::ALSA)
        if(JACK_FOUND)
            target_link_libraries(${TEST_EXECUTABLE_NAME} ${JACK_LIBRARIES})
            target_link_directories(${TEST_EXECUTABLE_NAME} PRIVATE ${JACK_LIBRARY_DIRS})
        endif()
    endif()

    target_include_directories(${TEST_EXECUTABLE_NAME} PRIVATE
        ${PORTAUDIO_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/src
    )

    # Allow discovery timeout to be configured via environment variable
    # Defaults to 15, set to 60 for ASAN builds in Makefile
    if(DEFINED ENV{GTEST_DISCOVERY_TIMEOUT})
        set(DISCOVERY_TIMEOUT_VALUE $ENV{GTEST_DISCOVERY_TIMEOUT})
    else()
        set(DISCOVERY_TIMEOUT_VALUE 15)
    endif()

    gtest_discover_tests(${TEST_EXECUTABLE_NAME}
        DISCOVERY_TIMEOUT ${DISCOVERY_TIMEOUT_VALUE}
    )
endfunction()

audio_add_gtest(microphone_test.cpp)
audio_add_gtest(audio_stream_test.cpp)
audio_add_gtest(mp3_encoder_test.cpp)
audio_add_gtest(discovery_test.cpp)
audio_add_gtest(speaker_test.cpp)
audio_add_gtest(audio_utils_test.cpp)
audio_add_gtest(mp3_decoder_test.cpp)
audio_add_gtest(audio_buffer_test.cpp)
