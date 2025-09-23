@echo off
REM build.bat - Configure and build the CMake project on Windows
REM Usage:
REM   build.bat               # configure + build Release
REM   build.bat Debug         # configure + build Debug
REM   build.bat clean         # remove the build directory
REM   build.bat Release "Visual Studio 17 2022"  # force generator

setlocal EnableDelayedExpansion

set "BUILD_DIR=build"
set "CONFIG=%~1"
set "GENERATOR=%~2"

if "%CONFIG%"=="" set "CONFIG=Release"

if /I "%CONFIG%"=="clean" (
    echo Cleaning "%CD%\%BUILD_DIR%" ...
    if exist "%BUILD_DIR%" (
        rd /s /q "%BUILD_DIR%"
        if errorlevel 1 (
            echo Failed to remove build directory.
            exit /b 1
        ) else (
            echo Clean complete.
            exit /b 0
        )
    ) else (
        echo No build directory to remove.
        exit /b 0
    )
)

echo === CMake configure (build dir: %BUILD_DIR%) ===
if "%GENERATOR%"=="" (
    cmake -S . -B "%BUILD_DIR%" -D CMAKE_BUILD_TYPE=%CONFIG%
) else (
    echo Using generator: %GENERATOR%
    cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -D CMAKE_BUILD_TYPE=%CONFIG%
)
if errorlevel 1 (
    echo CMake configuration failed.
    exit /b %errorlevel%
)

echo.
echo === Build (%CONFIG%) ===
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 (
    echo Build failed.
    exit /b %errorlevel%
)

echo.
echo Build completed successfully.
endlocal
exit /b 0
