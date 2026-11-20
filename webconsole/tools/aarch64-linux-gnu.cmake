# Cross toolchain for the MPC Sample (Rockchip RK3566, Cortex-A55, AArch64).
# Usage: cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=tools/aarch64-linux-gnu.cmake \
#              -DDM_WITH_TLS=OFF -DDM_STATIC_LIBSTDCXX=ON
# TLS needs an OpenSSL built for the target sysroot (set OPENSSL_ROOT_DIR), see docs/BUILD.md.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
