@echo off
set "ARM_TOOLCHAIN_PATH=C:\Tools\xpack-arm-none-eabi-gcc-14.2.1-1.1\bin"
set "JLINK_PATH=C:\Program Files\SEGGER\JLink_V912\JLink.exe"

if not exist "%ARM_TOOLCHAIN_PATH%\arm-none-eabi-gcc.exe" (
    echo ERROR: Toolchain not found
    pause
    exit /b 1
)

if exist build rmdir /s /q build
mkdir build
cd build

cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/gcc.cmake -DCMAKE_BUILD_TYPE=Release -DARM_TOOLCHAIN_PATH="%ARM_TOOLCHAIN_PATH%" ..
if errorlevel 1 (
    cd ..
    pause
    exit /b 1
)

ninja
if errorlevel 1 (
    cd ..
    pause
    exit /b 1
)

cd ..

:: Create J-Link Script
(
echo device R7FA6M5BH
echo si SWD
echo speed 1000
echo connect
echo erase
echo loadfile build\TinyML_RA6M5.srec
echo r
echo g
echo exit
) > flash.jlink

:: Flash
"%JLINK_PATH%" -CommandFile flash.jlink

if errorlevel 1 (
    echo Flash failed
    pause
    exit /b 1
)

del flash.jlink
echo Build and Flash completed!
pause