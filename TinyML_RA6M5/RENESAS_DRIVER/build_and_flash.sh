#!/usr/bin/env bash
# -------------------------------------------------------
# TESTING_2 - RA6M5 Baremetal CMake/Ninja Build Script
# -------------------------------------------------------

set -e

# ---- Toolchain paths (edit for your system) ----
ARM_TOOLCHAIN_PATH="${ARM_TOOLCHAIN_PATH:-/opt/arm-gnu-toolchain/arm-none-eabi/bin}"
JLINK_PATH="${JLINK_PATH:-/usr/bin/JLinkExe}"

# ---- Build type: Debug (default) or Release ----
BUILD_TYPE="${1:-Debug}"

# ---- Validate toolchain ----
if [[ ! -f "${ARM_TOOLCHAIN_PATH}/arm-none-eabi-gcc" ]]; then
    echo "[ERROR] ARM GCC toolchain not found at: ${ARM_TOOLCHAIN_PATH}" >&2
    echo "Install it or set ARM_TOOLCHAIN_PATH env variable." >&2
    exit 1
fi

# ---- Clean and create build directory ----
rm -rf build
mkdir build
cd build

# ---- Configure ----
echo "[INFO] Configuring with CMake (BUILD_TYPE=${BUILD_TYPE})..."
cmake -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=../cmake/gcc.cmake \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DARM_TOOLCHAIN_PATH="${ARM_TOOLCHAIN_PATH}" \
    ..

# ---- Build ----
echo "[INFO] Building with Ninja..."
ninja

cd ..
echo "[INFO] Build succeeded: build/TESTING_2.elf / build/TESTING_2.srec"

# ---- (Optional) Flash via J-Link ----
if [[ "${2}" == "flash" ]]; then
    if [[ ! -f "${JLINK_PATH}" ]]; then
        echo "[ERROR] J-Link not found at: ${JLINK_PATH}" >&2
        exit 1
    fi

    echo "[INFO] Flashing via J-Link..."
    cat > flash.jlink <<EOF
device R7FA6M5BH
si SWD
speed 4000
connect
erase
loadfile build/TESTING_2.srec
r
g
exit
EOF

    "${JLINK_PATH}" -CommandFile flash.jlink
    rm -f flash.jlink
    echo "[INFO] Flash complete."
fi

echo "Done!"
