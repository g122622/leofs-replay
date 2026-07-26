@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: trace-replay 构建 wrapper（仿 Cubium）
:: 自动注入 VS 开发环境，再调用 CMake（vcpkg manifest 模式自动装依赖）。
:: 用法:
::   configure.bat            - 配置 (Release, clang, Ninja)
::   configure.bat build      - 配置 + 构建
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
