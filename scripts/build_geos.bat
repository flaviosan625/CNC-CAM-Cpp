@echo off
rem Compila e instala o GEOS (motor do shapely) como biblioteca estatica.
rem Uso:  scripts\build_geos.bat <pasta-do-fonte-geos-3.14.1>
setlocal
call "%~dp0env.bat" || exit /b 1
set "SRC=%~1"
if "%SRC%"=="" ( echo uso: build_geos.bat ^<pasta do fonte^> & exit /b 2 )
cmake -S "%SRC%" -B "%CNC_BUILD%\geos" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF ^
  -DBUILD_TESTING=OFF -DBUILD_BENCHMARKS=OFF -DBUILD_DOCUMENTATION=OFF ^
  -DCMAKE_INSTALL_PREFIX="%CNC_DEPS%" || exit /b 1
cmake --build "%CNC_BUILD%\geos" || exit /b 1
cmake --install "%CNC_BUILD%\geos" || exit /b 1
echo [geos] instalado em %CNC_DEPS%
