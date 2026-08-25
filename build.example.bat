@echo off
setlocal enabledelayedexpansion

rem Build C&C Generals Zero Hour (Win32 x86 only -- x64 is rejected by design).
rem
rem   build.bat                 configure (if needed) + build Release
rem   build.bat Debug           build another config (Release|RelWithDebInfo|Debug)
rem   build.bat Release test    build, then run ctest
rem   build.bat Release generals   build a single target
rem   build.bat clean           delete build\ and reconfigure from scratch
rem
rem This is the committed template. Copy it to build.bat (git-ignored) and edit
rem the MACHINE SETTINGS block below to match this PC:
rem
rem   copy build.example.bat build.bat

rem ------------------------------ MACHINE SETTINGS -----------------------------
rem Leave a value empty to let the script work it out on its own.

rem Full path to cmake.exe. Empty = PATH first, then the copy shipped with VS2022.
set "CMAKE="

rem Visual Studio editions to probe for that shipped cmake, in order.
set "VS_EDITIONS=Community Professional Enterprise BuildTools"

rem CMake generator. Win32 x86 only -- x64 is rejected by the CMakeLists on purpose.
set "GENERATOR=Visual Studio 17 2022"
set "PLATFORM=Win32"

rem Config used when the command line does not name one.
set "DEFAULT_CONFIG=Release"

rem Build tree. Empty = <repo>\build. Point it at a fast local disk if this
rem checkout lives on a network share or a slow drive.
set "BUILD="
rem --------------------------- END MACHINE SETTINGS ----------------------------

set "ROOT=%~dp0"
set "SRC=%ROOT%GeneralsMD\Code"
if not defined BUILD set "BUILD=%ROOT%build"
set "CONFIG=%~1"
set "ARG2=%~2"

if /i "%CONFIG%"=="clean" (
    echo [build] removing %BUILD%
    if exist "%BUILD%" rmdir /s /q "%BUILD%"
    set "CONFIG=%ARG2%"
    set "ARG2="
)
if "%CONFIG%"=="" set "CONFIG=%DEFAULT_CONFIG%"

set "RUNTESTS="
set "TARGET="
if /i "%ARG2%"=="test" (set "RUNTESTS=1") else (if not "%ARG2%"=="" set "TARGET=%ARG2%")

rem --- locate cmake: MACHINE SETTINGS, then PATH, then the copy shipped with VS2022 ---
if defined CMAKE if not exist "%CMAKE%" (
    echo [build] ERROR: CMAKE is set to "%CMAKE%" in the MACHINE SETTINGS block, but that file does not exist.
    exit /b 1
)
for /f "delims=" %%C in ('where cmake 2^>nul') do if not defined CMAKE set "CMAKE=%%C"
if not defined CMAKE (
    for %%E in (%VS_EDITIONS%) do (
        set "TRY=C:\Program Files\Microsoft Visual Studio\2022\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if not defined CMAKE if exist "!TRY!" set "CMAKE=!TRY!"
    )
)
if not defined CMAKE (
    echo [build] ERROR: cmake.exe not found on PATH or under Visual Studio 2022.
    exit /b 1
)
echo [build] cmake:  %CMAKE%
echo [build] config: %CONFIG%

rem --- configure (only when the cache is missing) ---
if not exist "%BUILD%\CMakeCache.txt" (
    echo [build] configuring %SRC% -^> %BUILD%
    "%CMAKE%" -S "%SRC%" -B "%BUILD%" -G "%GENERATOR%" -A %PLATFORM%
    if !errorlevel! neq 0 (
        echo [build] ERROR: configure failed.
        exit /b 1
    )
)

rem --- build ---
if defined TARGET (
    echo [build] building target %TARGET%
    "%CMAKE%" --build "%BUILD%" --config %CONFIG% --target %TARGET%
) else (
    "%CMAKE%" --build "%BUILD%" --config %CONFIG%
)
if !errorlevel! neq 0 (
    echo [build] ERROR: build failed.
    exit /b 1
)

rem --- tests ---
if defined RUNTESTS (
    echo [build] running ctest
    "%CMAKE%" -E chdir "%BUILD%" ctest -C %CONFIG% --output-on-failure
    if !errorlevel! neq 0 (
        echo [build] ERROR: tests failed.
        exit /b 1
    )
)

echo [build] done. output: %BUILD%\%CONFIG%
endlocal
exit /b 0
