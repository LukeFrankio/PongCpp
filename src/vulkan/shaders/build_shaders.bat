@echo off
REM build_shaders.bat - Compile Slang shaders to SPIR-V
REM This script compiles all .slang files in the shaders directory

setlocal EnableDelayedExpansion

REM Check for Slang compiler
if not defined SLANG_DIR (
    echo Error: SLANG_DIR environment variable not set
    echo Please set SLANG_DIR to your Slang SDK installation directory
    exit /b 1
)

set "SLANGC=%SLANG_DIR%\bin\slangc.exe"
if not exist "%SLANGC%" (
    echo Error: Slang compiler not found at %SLANGC%
    echo Please check your SLANG_DIR environment variable
    exit /b 1
)

REM Create output directory
set "OUTPUT_DIR=..\..\..\build\shaders"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo Compiling Slang shaders...
echo Output directory: %OUTPUT_DIR%
echo.

REM Compile vertex shaders
echo Compiling vertex shaders...
for %%f in (*.slang) do (
    echo   Compiling %%f (vertex stage)...
    "%SLANGC%" %%f ^  
        -target spirv ^  
        -stage vertex ^  
        -entry vertexMain ^  
        -o "%OUTPUT_DIR%\%%~nf_vert.spv"
    if errorlevel 1 (
        echo     Error compiling %%f vertex stage
        set "HAS_ERRORS=1"
    ) else (
        echo     Success: %%~nf_vert.spv
    )
)

echo.
echo Compiling fragment shaders...
for %%f in (*.slang) do (
    echo   Compiling %%f (fragment stage)...
    "%SLANGC%" %%f ^  
        -target spirv ^  
        -stage fragment ^  
        -entry fragmentMain ^  
        -o "%OUTPUT_DIR%\%%~nf_frag.spv"
    if errorlevel 1 (
        echo     Error compiling %%f fragment stage
        set "HAS_ERRORS=1"
    ) else (
        echo     Success: %%~nf_frag.spv
    )
)

echo.
if defined HAS_ERRORS (
    echo Shader compilation completed with errors.
    exit /b 1
) else (
    echo All shaders compiled successfully.
    echo Compiled shaders are available in: %OUTPUT_DIR%
    exit /b 0
)
