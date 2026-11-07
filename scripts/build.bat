@echo off
setlocal enabledelayedexpansion

set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

if exist %VCVARS% (
    call %VCVARS%
)

if "%1"=="clean" (
    echo Cleaning build directory...
    if exist build rmdir /s /q build
)

echo Configuring project with CMake and Ninja...
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Compiling CapApp...
cmake --build build --config Release
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Running Google Test suite via CTest...
ctest --test-dir build -C Release --output-on-failure
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo Build and test execution completed successfully!
