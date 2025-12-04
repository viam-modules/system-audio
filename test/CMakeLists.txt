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
        ${CMAKE_SOURCE_DIR}/src/audio_stream.cpp
        ${CMAKE_SOURCE_DIR}/src/speaker.cpp
        ${CMAKE_SOURCE_DIR}/src/mp3_encoder.cpp
    )
    target_link_libraries(${TEST_EXECUTABLE_NAME}
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        viam-cpp-sdk::viamsdk
        ${PORTAUDIO_LIBRARIES}
        libmp3lame::libmp3lame
    )

    target_include_directories(${TEST_EXECUTABLE_NAME} PRIVATE
        ${PORTAUDIO_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/src
    )

    gtest_discover_tests(${TEST_EXECUTABLE_NAME})
endfunction()

audio_add_gtest(microphone_test.cpp)
audio_add_gtest(audio_stream_test.cpp)
audio_add_gtest(speaker_test.cpp)
audio_add_gtest(audio_utils_test.cpp)
audio_add_gtest(mp3_encoder_test.cpp)
