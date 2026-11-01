@echo off
setlocal
cd /d "%~dp0"

if not exist index.html (
  echo Please keep this file in the project web folder, next to index.html.
  pause
  exit /b 1
)

rem --- find a working Python ---
set PY=
python -c "print(1)" >nul 2>&1 && set PY=python
if not defined PY py -3 -c "print(1)" >nul 2>&1 && set PY=py -3
if not defined PY if exist "C:\msys64\usr\bin\python.exe" set PY=C:\msys64\usr\bin\python.exe
if not defined PY if exist "C:\msys64\ucrt64\bin\python3.exe" set PY=C:\msys64\ucrt64\bin\python3.exe

if not defined PY (
  echo No Python was found on this computer.
  echo Install it from https://www.python.org and tick "Add python.exe to PATH",
  echo or with MSYS2 run:  pacman -S python
  pause
  exit /b 1
)

echo Starting the TamaPoke installer at http://localhost:8000 ...
if not exist serve_nocache.py (
  echo serve_nocache.py is missing - keep it next to this file.
  pause
  exit /b 1
)
start "TamaPoke server" cmd /k %PY% serve_nocache.py 8000
timeout /t 2 /nobreak >nul
start http://localhost:8000/?v=%RANDOM%%RANDOM%
echo.
echo A "TamaPoke server" window opened - leave it running while you install.
echo Close that window when you are done.
