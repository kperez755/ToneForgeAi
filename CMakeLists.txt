cmake_minimum_required(VERSION 3.20)
project(ToneForgeAI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TONEFORGE_BUILD_TESTS "Build the tfcore unit test suite" ON)
option(TONEFORGE_BUILD_PLUGIN "Build the JUCE VST3/Standalone plugin (requires JUCE + a full desktop toolchain)" OFF)

# Must be called here, in the TOP-LEVEL CMakeLists.txt, for `ctest` run
# from the top build directory to discover tests registered by
# add_test() in subdirectories (core/CMakeLists.txt's `include(CTest)` +
# add_test() alone only generates a CTestTestfile.cmake inside
# build/core/ - it does NOT automatically relay into the top-level
# build/CTestTestfile.cmake as a `subdirs("core")` entry unless
# enable_testing() has ALSO run in this top-level scope). Without this,
# `ctest --test-dir build` prints "No tests were found!!!" even though
# `ctest --test-dir build/core` would work - a real bug caught by an
# actual `ctest --test-dir build` run on Windows (SETUP_WINDOWS.md
# recommends the top-level form), reproduced and confirmed in the Linux
# sandbox before this fix, not guessed at from CMake documentation alone.
enable_testing()

# ToneForgeCore: pure C++20 DSP library, no JUCE dependency. Always
# buildable, including in headless/CI/sandbox environments.
add_subdirectory(core)

# The JUCE plugin wrapper. OFF by default because it requires JUCE (fetched
# via FetchContent, ~500MB+) and a real desktop/DAW environment to link and
# smoke-test - see docs/ROADMAP.md "Environment reality check". Turn this on
# with -DTONEFORGE_BUILD_PLUGIN=ON on a machine that has those things.
if (TONEFORGE_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
