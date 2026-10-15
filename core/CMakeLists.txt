# ToneForgeCore: pure C++20 DSP + signal chain + preset model.
# Deliberately zero third-party dependencies and zero dependency on JUCE -
# see docs/ARCHITECTURE.md for the rationale. This is what lets `core` be
# built and unit tested in any C++20 environment, including one with no
# JUCE, no VST3 SDK, and no audio host installed.

add_library(tfcore STATIC
    src/util/Json.cpp
    src/preset/PresetSerializer.cpp
    src/dsp/modules/NoiseGate.cpp
    src/dsp/modules/Drive.cpp
)

target_include_directories(tfcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(tfcore PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(tfcore PRIVATE /W4)
else()
    target_compile_options(tfcore PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Threads (std::atomic / pthreads on Linux) needed for the atomic
# shared_ptr swap used by SignalChain.
find_package(Threads REQUIRED)
target_link_libraries(tfcore PUBLIC Threads::Threads)

if (TONEFORGE_BUILD_TESTS)
    add_executable(tfcore_tests
        tests/main.cpp
        tests/test_parameter_store.cpp
        tests/test_noise_gate.cpp
        tests/test_drive.cpp
        tests/test_signal_chain.cpp
        tests/test_json.cpp
        tests/test_preset_serializer.cpp
        tests/test_biquad.cpp
        tests/test_amp.cpp
        tests/test_cab.cpp
        tests/test_eq.cpp
        tests/test_reverb.cpp
        tests/test_compressor.cpp
        tests/test_distortion.cpp
        tests/test_delay.cpp
        tests/test_modulation.cpp
    )
    target_link_libraries(tfcore_tests PRIVATE tfcore)
    target_compile_features(tfcore_tests PRIVATE cxx_std_20)

    include(CTest)
    add_test(NAME tfcore_tests COMMAND tfcore_tests)
endif()
