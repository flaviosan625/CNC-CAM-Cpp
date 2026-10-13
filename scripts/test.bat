@echo off
rem Roda os testes (doctest). Argumentos vao direto para o executavel:
rem   scripts\test.bat -tc="*entidades*"     (filtra por nome)
rem   scripts\test.bat -ltc                  (lista os casos)
setlocal
call "%~dp0env.bat" || exit /b 1
if "%CFG%"=="" set "CFG=RelWithDebInfo"
"%CNC_BUILD%\cnccam-%CFG%\bin\cnc_tests.exe" %*
