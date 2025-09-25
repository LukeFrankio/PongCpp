@echo off
REM build.bat - Configure and build the CMake project on Windows
REM Usage:
REM   build.bat               # configure + build Release + generate docs
REM   build.bat Debug         # configure + build Debug
REM   build.bat clean         # remove the build directory
REM   build.bat docs          # generate documentation only
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

if /I "%CONFIG%"=="docs" (
    echo === Generating Documentation ===
    call :generate_docs
    exit /b %errorlevel%
)

echo === CMake configure (build dir: %BUILD_DIR%) ===
if "%GENERATOR%"=="" (
    cmake -S . -B "%BUILD_DIR%" -D CMAKE_BUILD_TYPE=%CONFIG% -D BUILD_DOCUMENTATION=ON
) else (
    echo Using generator: %GENERATOR%
    cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -D CMAKE_BUILD_TYPE=%CONFIG% -D BUILD_DOCUMENTATION=ON
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

REM Generate documentation for Release builds
if /I "%CONFIG%"=="Release" (
    echo.
    echo === Generating Documentation ===
    call :generate_docs
    if errorlevel 1 (
        echo Warning: Documentation generation failed, but build succeeded.
    )
)

echo.
echo Build completed successfully.
endlocal
exit /b 0

REM Function to generate documentation
:generate_docs
echo Checking for Doxygen...
where doxygen >nul 2>&1
if errorlevel 1 (
    echo Doxygen not found in PATH. Please install Doxygen to generate documentation.
    echo Download from: https://www.doxygen.nl/download.html
    exit /b 1
)

echo Generating Doxygen documentation...
if not exist "%BUILD_DIR%" (
    echo Build directory does not exist. Running cmake configure first...
    cmake -S . -B "%BUILD_DIR%" -D BUILD_DOCUMENTATION=ON
    if errorlevel 1 (
        echo CMake configuration failed.
        exit /b %errorlevel%
    )
)

REM Try CMake docs target first, fall back to direct doxygen call
cmake --build "%BUILD_DIR%" --target docs 2>nul
if errorlevel 1 (
    echo CMake docs target not available, running doxygen directly...
    if not exist "docs\doxygen" mkdir "docs\doxygen"
    doxygen Doxyfile
    if errorlevel 1 (
        echo Documentation generation failed.
        exit /b %errorlevel%
    )
)

if exist "docs\doxygen\html\index.html" (
    echo Documentation generated successfully in docs\doxygen\html\
    echo You can view the documentation by opening: docs\doxygen\html\index.html
) else (
    echo Warning: Documentation may not have generated correctly.
)
exit /b 0
