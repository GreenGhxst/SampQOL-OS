cmake_minimum_required(VERSION 3.16)
project(diskmode_webconsole CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(DM_WITH_TLS "Enable HTTPS through OpenSSL" ON)
option(DM_BUILD_TESTS "Build the test binaries" ON)
option(DM_WITH_ALSA "Enable guarded ALSA raw-MIDI control-surface input" OFF)
option(DM_STATIC_LIBSTDCXX "Link libstdc++/libgcc statically (useful for the device)" OFF)
option(DM_FULLY_STATIC "Link the whole binary statically (no glibc version dependency on the device)" OFF)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
if(DM_FULLY_STATIC)
  add_link_options(-static)
elseif(DM_STATIC_LIBSTDCXX)
  add_link_options(-static-libstdc++ -static-libgcc)
endif()

add_library(dm_core STATIC
  src/util/common.cpp src/util/sha256.cpp
  src/core/volume_registry.cpp src/core/path_auth.cpp src/core/journal.cpp src/core/file_ops.cpp
  src/core/job_queue.cpp src/core/transfer_manager.cpp src/core/wav.cpp src/core/intelligence.cpp
  src/core/inbox.cpp src/core/screen.cpp src/core/format_guard.cpp src/core/network.cpp)
target_include_directories(dm_core PUBLIC src third_party)
find_package(Threads REQUIRED)
target_link_libraries(dm_core PUBLIC Threads::Threads)

add_library(dm_server STATIC src/server/auth.cpp src/server/config.cpp src/server/web_console.cpp src/server/routes_extra.cpp)
target_link_libraries(dm_server PUBLIC dm_core)
if(DM_WITH_TLS)
  find_package(OpenSSL REQUIRED)
  target_compile_definitions(dm_server PUBLIC DM_WITH_TLS)
  target_link_libraries(dm_server PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# Native controller is platform-independent. It never reads raw device inputs or
# bypasses the existing server-side filesystem authority.
add_library(dm_native STATIC src/native/native_model.cpp src/native/native_core_service.cpp src/native/hardware_bridge.cpp src/native/device_session.cpp)
target_include_directories(dm_native PUBLIC src)
target_link_libraries(dm_native PUBLIC dm_core)
if(DM_WITH_ALSA)
  find_package(ALSA REQUIRED)
  target_compile_definitions(dm_native PUBLIC DM_WITH_ALSA)
  target_link_libraries(dm_native PUBLIC ALSA::ALSA)
endif()

add_executable(diskmode-native-desktop src/native/native_desktop.cpp)
target_link_libraries(diskmode-native-desktop PRIVATE dm_native)

add_executable(diskmode-webconsole src/main.cpp)
target_link_libraries(diskmode-webconsole PRIVATE dm_server)

install(TARGETS diskmode-webconsole RUNTIME DESTINATION bin)
install(DIRECTORY web/ DESTINATION share/diskmode-webconsole/web)

if(DM_BUILD_TESTS)
  enable_testing()
  add_executable(core_tests tests/core/test_main.cpp tests/core/test_paths.cpp tests/core/test_volumes.cpp
    tests/core/test_fileops.cpp tests/core/test_journal_jobs.cpp tests/core/test_auth.cpp tests/core/test_format.cpp
    tests/core/test_misc.cpp tests/core/test_native.cpp tests/core/test_native_hardware.cpp
    tests/core/test_device_session.cpp)
  target_link_libraries(core_tests PRIVATE dm_server dm_native)
  add_test(NAME core_tests COMMAND core_tests)
  add_test(NAME native_desktop_smoke COMMAND diskmode-native-desktop --smoke)
  find_program(PYTHON3 python3)
  if(PYTHON3)
    add_test(NAME http_tests COMMAND ${PYTHON3} -I ${CMAKE_SOURCE_DIR}/tests/http/test_api.py $<TARGET_FILE:diskmode-webconsole> ${CMAKE_SOURCE_DIR}/web)
    set_tests_properties(http_tests PROPERTIES TIMEOUT 300)
  endif()
endif()
