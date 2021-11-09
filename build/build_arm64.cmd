@echo off
@setlocal enableextensions
@cd /d "%~dp0\..\"

call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64_arm64

msbuild KProcessHacker\KProcessHacker.sln -property:Configuration=Debug -property:Platform=ARM64 -verbosity:minimal
if %ERRORLEVEL% neq 0 goto end

msbuild ProcessHacker.sln -property:Configuration=Debug -property:Platform=ARM64 -verbosity:minimal
if %ERRORLEVEL% neq 0 goto end

mkdir sdk\lib\arm64
copy bin\DebugARM64\ProcessHacker.lib sdk\lib\arm64

msbuild plugins\Plugins.sln -property:Configuration=Debug -property:Platform=ARM64 -verbosity:minimal

:end
pause
