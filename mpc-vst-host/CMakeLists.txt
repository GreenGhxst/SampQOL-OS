# SPDX-License-Identifier: MIT
# MPC Sample VST Host Expansion: the standalone VST2 host, its tools, the
# mock plug-ins and the tests. Desktop build:
#   cmake -S . -B build && cmake --build build && ctest --test-dir build
# Device build (aarch64, see cmake/aarch64-linux-gnu.cmake):
#   cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
cmake_minimum_required(VERSION 3.16)
project(mpcvst VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(MPCVST_BUILD_TESTS "Build the tests and the faulty mock plug-ins" ON)
option(MPCVST_WERROR "Treat warnings as errors" ON)

add_compile_options(-Wall -Wextra -Wshadow -fno-strict-aliasing -fvisibility=hidden)
if(MPCVST_WERROR)
  add_compile_options(-Werror)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
  # Rockchip RK3566, Cortex-A55 (sample-base HARDWARE.md, mpc-fm Makefile).
  add_compile_options(-mcpu=cortex-a55)
endif()

# ---- the host library ----
add_library(mpcvst STATIC
  src/host_core/vst2_plugin.cpp
  src/host_core/state_file.cpp
  src/plugin_manager/elf_inspect.cpp
  src/plugin_manager/plugin_registry.cpp
  src/plugin_manager/plugin_scanner.cpp
  src/audio_engine/plugin_chain.cpp
  src/audio_engine/offline_render.cpp
  src/sampqol_compat/sampqol_compat.cpp)
target_include_directories(mpcvst PUBLIC include)
target_link_libraries(mpcvst PUBLIC dl pthread m)

# ---- tools ----
add_executable(mpcvst-probe src/tools/mpcvst_probe.cpp)
target_link_libraries(mpcvst-probe mpcvst)
add_executable(mpcvst-cli src/tools/mpcvst_cli.cpp)
target_link_libraries(mpcvst-cli mpcvst)
set_target_properties(mpcvst-cli PROPERTIES OUTPUT_NAME mpcvst)

# ---- mock plug-ins ----
set(PLUGIN_DIR ${CMAKE_BINARY_DIR}/plugins)
function(mock_plugin name)
  add_library(${name} MODULE ${ARGN})
  target_include_directories(${name} PRIVATE include plugins)
  target_link_libraries(${name} m)
  set_target_properties(${name} PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_DIR}
    # Plain C exports named VSTPluginMain; nothing else leaves the library.
    C_VISIBILITY_PRESET hidden)
endfunction()
mock_plugin(mock_instrument plugins/mock_instrument.c)
mock_plugin(mock_effect plugins/mock_effect.c)

if(MPCVST_BUILD_TESTS)
  enable_testing()
  set(BAD_KINDS no_entry wrong_magic crash_open hang crash_process nan_output huge_io no_replacing missing_dep)
  set(idx 1)
  set(bad_defs "")
  foreach(k ${BAD_KINDS})
    string(TOUPPER ${k} K)
    list(APPEND bad_defs "BAD_${K}=${idx}")
    math(EXPR idx "${idx} + 1")
  endforeach()
  foreach(k ${BAD_KINDS})
    string(TOUPPER ${k} K)
    mock_plugin(bad_${k} plugins/bad_plugins.c)
    target_compile_definitions(bad_${k} PRIVATE ${bad_defs} BAD_KIND=BAD_${K})
  endforeach()
  # The missing-dependency plug-in links a tiny library that is deleted right
  # after linking, so its DT_NEEDED entry points at nothing.
  add_library(mpcvst_fakedep SHARED plugins/fakedep.c)
  set_target_properties(mpcvst_fakedep PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_DIR}/fakedep)
  target_link_libraries(bad_missing_dep mpcvst_fakedep)
  target_link_options(bad_missing_dep PRIVATE -Wl,--no-as-needed)
  add_custom_command(TARGET bad_missing_dep POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f $<TARGET_FILE:mpcvst_fakedep>
    COMMENT "Deleting the fake dependency so bad_missing_dep cannot load")

  function(mpcvst_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} mpcvst)
    target_compile_definitions(${name} PRIVATE
      MPCVST_PLUGIN_DIR="${PLUGIN_DIR}" MPCVST_TOOL_DIR="${CMAKE_BINARY_DIR}")
    add_test(NAME ${name} COMMAND ${MPCVST_TEST_LAUNCHER} $<TARGET_FILE:${name}>)
    string(REPLACE ";" " " launcher_words "${MPCVST_TEST_LAUNCHER}")
    set_tests_properties(${name} PROPERTIES
      ENVIRONMENT "MPCVST_TEST_TMP=${CMAKE_BINARY_DIR}/test-tmp;MPCVST_WORKER_LAUNCHER=${launcher_words}" TIMEOUT 120)
  endfunction()
  mpcvst_test(test_host_core)
  mpcvst_test(test_plugin_manager)
  mpcvst_test(test_engine)
  mpcvst_test(test_compat)
  add_dependencies(test_host_core mock_instrument mock_effect mpcvst-probe)
  add_dependencies(test_plugin_manager mock_instrument mock_effect mpcvst-probe)

  # End-to-end through the command line: render the mock instrument to a WAV.
  add_test(NAME cli_render COMMAND ${MPCVST_TEST_LAUNCHER} $<TARGET_FILE:mpcvst-cli> render ${PLUGIN_DIR}/mock_instrument.so
    --midi ${CMAKE_SOURCE_DIR}/tests/data/chord.midi.txt --seconds 1 --out ${CMAKE_BINARY_DIR}/test-tmp/chord.wav --effect ${PLUGIN_DIR}/mock_effect.so)
  add_test(NAME cli_info COMMAND ${MPCVST_TEST_LAUNCHER} $<TARGET_FILE:mpcvst-cli> info ${PLUGIN_DIR}/mock_effect.so)
  add_test(NAME cli_compat_not_in_app COMMAND ${MPCVST_TEST_LAUNCHER} $<TARGET_FILE:mpcvst-cli> compat)
  set_tests_properties(cli_compat_not_in_app PROPERTIES WILL_FAIL TRUE) # exit 3: integration disabled, as it must be here
endif()

install(TARGETS mpcvst-cli mpcvst-probe RUNTIME DESTINATION bin)
