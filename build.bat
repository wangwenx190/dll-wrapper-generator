@echo off
setlocal
cls
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake --version
echo ninja build
ninja --version
cd /d "%~dp0"
if exist build rd /s /q build
md build
cd build
md cmake
cd cmake
cmake -DCMAKE_INSTALL_PREFIX="%~dp0build\install" -DCMAKE_BUILD_TYPE=Release -GNinja -DDWG_ENABLE_VCLTL=ON -DDWG_ENABLE_YYTHUNKS=ON "%~dp0"
cmake --build . --target all --config Release --parallel
cmake --install . --config Release
endlocal
cd /d "%~dp0"
pause
exit /b 0
