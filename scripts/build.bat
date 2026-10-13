@echo off
rem Configura (se preciso) e compila.
rem Uso:  scripts\build.bat [Config] [argumentos extras do cmake --build]
rem Ex.:  scripts\build.bat RelWithDebInfo --target cnc_tests
setlocal
rem a raiz tem de ser guardada ANTES do shift: ele tambem desloca o %0
set "ROOT=%~dp0.."
call "%~dp0env.bat" || exit /b 1
set "CFG=%~1"
if "%CFG%"=="" set "CFG=RelWithDebInfo"
shift /1
set "BDIR=%CNC_BUILD%\cnccam-%CFG%"
if not exist "%BDIR%\build.ninja" (
    cmake -S "%ROOT%" -B "%BDIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% || exit /b 1
)
cmake --build "%BDIR%" %1 %2 %3 %4 %5 %6 %7 %8 %9
