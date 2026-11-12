@echo off
setlocal

where glslc >nul 2>nul
if errorlevel 1 (
    echo glslc was not found. Install the Vulkan SDK and add its Bin folder to PATH.
    exit /b 1
)

set "SHADER_DIR=%~dp0..\assets\shaders"

for /r "%SHADER_DIR%" %%F in (*.vert *.frag *.comp) do (
    call :compile "%%~fF" || goto :failed
)

echo Shader compilation complete.
exit /b 0

:compile
glslc --target-env=vulkan1.0 -I "%SHADER_DIR%" "%~1" -o "%~1.spv"
exit /b %errorlevel%

:failed
echo Shader compilation failed.
exit /b 1
