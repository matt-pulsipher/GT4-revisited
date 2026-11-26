@echo off
rem A picture of one frame of the game started on a disc, muted and with its own data folder (run
rem through desktop-run.ps1, in the desktop session):
rem   title-shot.cmd <gt4r.exe> <disc> <data folder> <out.png> <frame> [keys]
rem The run is scripted (--shot): no window shows, nothing is heard, no real input is read.
setlocal
set SDL_AUDIODRIVER=dummy
set GT4R_DATA_DIR=%~3
if not exist "%~3" mkdir "%~3"
if "%~6"=="" (
  "%~1" "%~2" --shot "%~4" --shot-frame %5 > "%~dpn4.log" 2>&1
) else (
  "%~1" "%~2" --keys "%~6" --shot "%~4" --shot-frame %5 > "%~dpn4.log" 2>&1
)
echo exit %ERRORLEVEL% >> "%~dpn4.log"
