# SPDX-License-Identifier: MIT
# Cross build for the MPC Sample (Rockchip RK3566, Cortex-A55, aarch64 Linux,
# glibc). Needs the Debian/Ubuntu packages gcc-aarch64-linux-gnu and
# g++-aarch64-linux-gnu, or any aarch64-linux-gnu toolchain in PATH.
#   cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Run the tests under qemu-user when it is installed (make check-arm).
find_program(QEMU_AARCH64 NAMES qemu-aarch64-static qemu-aarch64)
if(QEMU_AARCH64)
  set(MPCVST_TEST_LAUNCHER ${QEMU_AARCH64} -L /usr/aarch64-linux-gnu CACHE STRING "test launcher")
endif()
