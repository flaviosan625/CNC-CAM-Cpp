@echo off
rem Roda o aplicativo (ou outro executavel do build) com o ambiente do Qt.
rem   scripts\run.bat                       abre o CNC-CAM
rem   scripts\run.bat --grab foto.png       tira foto da janela e sai
setlocal
call "%~dp0env.bat" || exit /b 1
if "%CFG%"=="" set "CFG=RelWithDebInfo"
"%CNC_BUILD%\cnccam-%CFG%\bin\CNC-CAM.exe" %*
