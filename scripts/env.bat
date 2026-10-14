@echo off
rem ------------------------------------------------------------------
rem Ambiente de build do CNC CAM C++ (Windows: MSVC + Qt + dependencias).
rem Chame com:  call scripts\env.bat
rem Cada caminho pode ser trocado por variavel de ambiente ANTES da chamada.
rem ------------------------------------------------------------------
if "%CNC_VS%"==""    set "CNC_VS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
if "%CNC_QT%"==""    set "CNC_QT=%USERPROFILE%\toolchains\Qt\6.12.0\msvc2022_64"
if "%CNC_DEPS%"==""  set "CNC_DEPS=%USERPROFILE%\toolchains\deps"
if "%CNC_BUILD%"=="" set "CNC_BUILD=%USERPROFILE%\toolchains\build"
if "%CNC_VTK%"==""  set "CNC_VTK=%USERPROFILE%\toolchains\vtk-9.7.1\vtk-wheel-sdk-9.7.1-cp314-cp314-win_amd64\vtk-9.7.1.data\headers\cmake"

rem Compila fora da pasta do projeto: sem espacos nem "+" no caminho do build.
rem Ninja precisa reconhecer /showIncludes para recompilar dependencias de headers.
rem Idioma fixo evita prefixos localizados/codificados de forma diferente pelo CMake.
if not defined VSCMD_VER (
    rem o vcvars procura o vswhere no PATH; ele mora na pasta do instalador do VS
    set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
    call "%CNC_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 ( echo [env] vcvars64 falhou & exit /b 1 )
)
set "VSLANG=1033"
set "PATH=%CNC_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%CNC_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%CNC_QT%\bin;%CNC_DEPS%\bin;%PATH%"
set "CMAKE_PREFIX_PATH=%CNC_QT%;%CNC_DEPS%;%CNC_VTK%"
exit /b 0
