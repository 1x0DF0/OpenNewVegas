@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem ===========================================================================
rem  Open New Vegas - Windows launcher
rem
rem  Double-click this file to play. It:
rem    1. Finds walker.exe sitting next to this script.
rem    2. Auto-detects your Fallout: New Vegas install (Steam) and sets the
rem       ONV_FNV_PATH environment variable the engine reads.
rem    3. Launches the walker.
rem
rem  ONV_FNV_PATH must point at the install ROOT - the folder that CONTAINS the
rem  "Data" subfolder (e.g. ...\Fallout New Vegas, not ...\Fallout New Vegas\Data).
rem
rem  If the game isn't found we still launch: the walker falls back to
rem  procedural Mojave terrain. We print how to set ONV_FNV_PATH by hand.
rem ===========================================================================

rem --- Locate walker.exe next to this script -------------------------------
rem  %~dp0 expands to this script's directory, with a trailing backslash.
set "HERE=%~dp0"
set "WALKER=%HERE%walker.exe"

if not exist "%WALKER%" (
  echo [ERROR] walker.exe was not found next to this launcher.
  echo         Expected: "%WALKER%"
  echo         Make sure you extracted the whole zip and run play.bat from inside it.
  echo.
  pause
  exit /b 1
)

rem --- If ONV_FNV_PATH is already set and valid, respect it ----------------
if defined ONV_FNV_PATH (
  if exist "%ONV_FNV_PATH%\Data" (
    echo [info] Using ONV_FNV_PATH from your environment:
    echo        "%ONV_FNV_PATH%"
    goto :launch
  ) else (
    echo [warn] ONV_FNV_PATH is set but has no "Data" subfolder; re-detecting...
  )
)

rem --- Auto-detect: check a list of common install locations ----------------
rem  Each candidate is the install ROOT (the folder containing "Data").
set "GAME="

rem  Standard Steam library on the system drive, plus common extra-drive roots.
call :try_path "%ProgramFiles(x86)%\Steam\steamapps\common\Fallout New Vegas"
if defined GAME goto :found
call :try_path "%ProgramFiles%\Steam\steamapps\common\Fallout New Vegas"
if defined GAME goto :found

rem  Walk every drive letter for a couple of common library folder names.
for %%D in (C D E F G H I J K L) do (
  call :try_path "%%D:\SteamLibrary\steamapps\common\Fallout New Vegas"
  if defined GAME goto :found
  call :try_path "%%D:\Steam\steamapps\common\Fallout New Vegas"
  if defined GAME goto :found
  call :try_path "%%D:\Program Files (x86)\Steam\steamapps\common\Fallout New Vegas"
  if defined GAME goto :found
  call :try_path "%%D:\Games\Steam\steamapps\common\Fallout New Vegas"
  if defined GAME goto :found
  rem  GOG / standalone common locations.
  call :try_path "%%D:\GOG Games\Fallout New Vegas"
  if defined GAME goto :found
)

rem  Parse Steam's libraryfolders.vdf to discover non-default library roots.
rem  The default Steam config lives under the (x86) Steam install.
set "VDF=%ProgramFiles(x86)%\Steam\steamapps\libraryfolders.vdf"
if not exist "%VDF%" set "VDF=%ProgramFiles%\Steam\steamapps\libraryfolders.vdf"
if exist "%VDF%" (
  rem  Lines look like:  "path"   "D:\\SteamLibrary"
  for /f "tokens=2 delims=	 " %%L in ('findstr /i /c:"\"path\"" "%VDF%"') do (
    rem  Strip surrounding quotes and unescape doubled backslashes.
    set "LIB=%%~L"
    set "LIB=!LIB:\\=\!"
    call :try_path "!LIB!\steamapps\common\Fallout New Vegas"
    if defined GAME goto :found
  )
)

rem --- Not found: explain and fall back to procedural terrain ---------------
echo.
echo [info] Could not auto-detect your Fallout: New Vegas install.
echo        Launching with PROCEDURAL Mojave terrain (no game files needed).
echo.
echo        To walk the REAL terrain, set ONV_FNV_PATH to your install root -
echo        the folder that contains the "Data" subfolder. For example:
echo.
echo          set "ONV_FNV_PATH=D:\SteamLibrary\steamapps\common\Fallout New Vegas"
echo.
echo        Then run play.bat again. (Use setx to make it permanent.)
echo.
goto :launch

:found
set "ONV_FNV_PATH=%GAME%"
echo [info] Found Fallout: New Vegas:
echo        "%ONV_FNV_PATH%"

:launch
echo.
echo [info] Starting Open New Vegas...
echo.
rem  Quote the exe path; it may contain spaces. Run in its own dir so it finds
rem  any sibling resource files.
pushd "%HERE%"
"%WALKER%"
set "RC=%ERRORLEVEL%"
popd

if not "%RC%"=="0" (
  echo.
  echo [warn] walker.exe exited with code %RC%.
  pause
)
endlocal
exit /b %RC%

rem ===========================================================================
rem  :try_path  <candidate-root>
rem  Sets GAME if the candidate exists and has a Data subfolder.
rem ===========================================================================
:try_path
if defined GAME goto :eof
if exist "%~1\Data" set "GAME=%~1"
goto :eof
