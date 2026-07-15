@echo off
setlocal

where glslc >nul 2>nul
if errorlevel 1 (
    echo glslc was not found. Install the Vulkan SDK and add its Bin folder to PATH.
    exit /b 1
)

set "SHADER_DIR=%~dp0..\assets\shaders"
pushd "%SHADER_DIR%"

call :compile shader.vert vert.spv || goto :failed
call :compile shader.frag frag.spv || goto :failed
call :compile skybox.vert skybox.vert.spv || goto :failed
call :compile skybox.frag skybox.frag.spv || goto :failed
call :compile irradiance.vert irradiance.vert.spv || goto :failed
call :compile irradiance.frag irradiance.frag.spv || goto :failed
call :compile prefilter.vert prefilter.vert.spv || goto :failed
call :compile prefilter.frag prefilter.frag.spv || goto :failed

popd
echo Shader compilation complete.
exit /b 0

:compile
glslc "%~1" -o "%~2"
exit /b %errorlevel%

:failed
popd
echo Shader compilation failed.
exit /b 1
