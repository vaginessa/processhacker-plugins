@echo off
@setlocal enableextensions

call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" amd64_arm64

echo;

rc.exe /v /c 65001 /i "%INCLUDE%" /i "%~dp0..\..\processhacker" /fo version.res version.rc

pause
