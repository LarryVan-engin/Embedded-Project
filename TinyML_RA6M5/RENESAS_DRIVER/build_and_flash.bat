@echo off
setlocal enabledelayedexpansion

:: -------------------------------------------------------
:: TESTING_2 - RA6M5 Baremetal CMake/Ninja Build Script
:: -------------------------------------------------------

:: ---- Toolchain paths (edit if different on your machine) ----
set "ARM_TOOLCHAIN_PATH=C:\Tools\xpack-arm-none-eabi-gcc-14.2.1-1.1\bin"
set "JLINK_PATH=C:\Program Files\SEGGER\JLink_V912\JLink.exe"

:: Convert to short path to avoid (x86) parsing issues in cmd.exe
for %%A in ("!ARM_TOOLCHAIN_PATH!") do set "ARM_TOOLCHAIN_PATH=%%~sA"
for %%B in ("!JLINK_PATH!") do set "JLINK_PATH=%%~sB"

:: ---- Build type: Debug or Release ----
if "%1"=="" (
    set BUILD_TYPE=Debug
) else (
    set BUILD_TYPE=%1
)

:: ---- Validate toolchain ----
if not exist "!ARM_TOOLCHAIN_PATH!\arm-none-eabi-gcc.exe" (
    echo [ERROR] ARM GCC toolchain not found at:
    echo   !ARM_TOOLCHAIN_PATH!
    echo Please install it or update ARM_TOOLCHAIN_PATH in this script.
    pause
    exit /b 1
)

:: ---- Clean and create build directory ----
if exist build rmdir /s /q build
mkdir build
cd build

:: ---- Configure ----
echo [INFO] Configuring with CMake (BUILD_TYPE=!BUILD_TYPE!)...
cmake -G Ninja ^
    "-DCMAKE_TOOLCHAIN_FILE=%~dp0cmake\gcc.cmake" ^
    "-DCMAKE_BUILD_TYPE=!BUILD_TYPE!" ^
    "-DARM_TOOLCHAIN_PATH=!ARM_TOOLCHAIN_PATH!" ^
    "%~dp0."

if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    cd ..
    pause
    exit /b 1
)

:: ---- Build ----
echo [INFO] Building with Ninja...
ninja

if errorlevel 1 (
    echo [ERROR] Build failed.
    cd ..
    pause
    exit /b 1
)

cd ..
echo [INFO] Build succeeded: build\TESTING_2.elf / build\TESTING_2.srec

:: ---- Flash via J-Link ----
if not exist "!JLINK_PATH!" (
    echo [ERROR] J-Link not found at: !JLINK_PATH!
    echo Please install J-Link or update JLINK_PATH in this script.
    pause
    exit /b 1
)

echo [INFO] Generating J-Link flash script...
(
    echo device R7FA6M5BH
    echo si SWD
    echo speed 4000
    echo connect
    echo erase
    echo loadfile build\TESTING_2.srec
    echo r
    echo g
    echo exit
) > flash.jlink

echo [INFO] Flashing device...
"!JLINK_PATH!" -CommandFile flash.jlink

del /f /q flash.jlink

if errorlevel 1 (
    echo [ERROR] Flash failed.
    pause
    exit /b 1
)
echo [INFO] Flash complete.

echo.
echo Done!
pause
