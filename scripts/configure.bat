@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: trace-replay build wrapper (modeled on Cubium).
:: Automatically injects the VS development environment, then invokes CMake
:: (vcpkg manifest mode auto-installs dependencies).
:: Usage:
::   configure.bat            - configure (Release, clang, Ninja)
::   configure.bat build      - configure + build
:: ============================================================

set "VSBASE=D:\Program Files\Microsoft Visual Studio\18\Community"
set "VSDEVCMD=%VSBASE%\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
    echo ERROR: VsDevCmd.bat not found: %VSDEVCMD%
    exit /b 1
)
call "%VSDEVCMD%" -arch=amd64 -host_arch=amd64 -no_logo >nul 2>&1
if errorlevel 1 (
    echo ERROR: VsDevCmd failed.
    exit /b 1
)

set "VCPKG_ROOT=E:\vcpkg"
set "VCPKG_DEFAULT_BINARY_CACHE=E:\vcpkg\binary-cache"
set "NINJA=D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set "CLANG=D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin"

if /i "%~1"=="build" goto :build

:configure
cmake -S . -B build -G "Ninja Multi-Config" ^
  -DCMAKE_C_COMPILER="%CLANG%\clang.exe" ^
  -DCMAKE_CXX_COMPILER="%CLANG%\clang++.exe" ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DVCPKG_TARGET_TRIPLET=x64-windows
exit /b %errorlevel%

:build
cmake -S . -B build -G "Ninja Multi-Config" ^
  -DCMAKE_C_COMPILER="%CLANG%\clang.exe" ^
  -DCMAKE_CXX_COMPILER="%CLANG%\clang++.exe" ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DVCPKG_TARGET_TRIPLET=x64-windows
if errorlevel 1 exit /b 1
echo Building...
"%NINJA%" -C build trace_replay
exit /b %errorlevel%
