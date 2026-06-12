@echo off
setlocal enabledelayedexpansion
set "ROOT_DIR=%~dp0"
set "ROOT_DIR=%ROOT_DIR:~0,-1%"
set "NO_PAUSE="
if /I "%~1"=="--no-pause" set "NO_PAUSE=1"

echo ============================================
echo  ETS2 Chat Translator - Build
echo ============================================
echo.

set ELECTRON_RUN_AS_NODE=

set "FOUND_VS="

if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_BAT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    set "FOUND_VS=1"
)

if not defined FOUND_VS if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    set "FOUND_VS=1"
)

if not defined FOUND_VS if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_BAT=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    set "FOUND_VS=1"
)

if not defined FOUND_VS if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_BAT=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    set "FOUND_VS=1"
)

if not defined FOUND_VS if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VS_BAT=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    set "FOUND_VS=1"
)

if not defined FOUND_VS (
    echo [ERROR] Visual Studio 2019/2022 x64 tools were not found.
    if not defined NO_PAUSE pause
    exit /b 1
)

call "%VS_BAT%" >nul 2>&1
if not exist build mkdir build

echo [INFO] Building plugin DLL...
cl.exe /nologo /EHsc /O2 /std:c++17 /utf-8 /W3 ^
    /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /D "_WIN64" ^
    /I "include" /I "src" ^
    /Fe"build\ets2_chat_translator.dll" ^
    /Fo"build\\" ^
    src\dllmain.cpp src\app_runtime.cpp src\chat_panel.cpp src\chat_tailer.cpp ^
    src\translate_engine.cpp src\http_agent.cpp src\settings_store.cpp ^
    src\text_codec.cpp src\win_paths.cpp ^
    /link /DLL /DEF:exports.def /MACHINE:X64 ^
    user32.lib gdi32.lib winhttp.lib shell32.lib advapi32.lib shlwapi.lib crypt32.lib

if %ERRORLEVEL% neq 0 (
    echo [ERROR] Plugin build failed.
    if not defined NO_PAUSE pause
    exit /b 1
)

where npm >nul 2>&1
if not "!ERRORLEVEL!"=="0" (
    echo.
    echo [WARN] Node.js/npm was not found, manager app was not built.
    echo        Install Node.js and run build.bat again.
    echo.
) else (
    echo [INFO] Building JavaScript/Electron manager app...
    pushd manager
    if not exist "node_modules" call npm install
    if not "!ERRORLEVEL!"=="0" (
        popd
        echo [ERROR] npm install failed.
        if not defined NO_PAUSE pause
        exit /b 1
    )
    call npm run dist
    if not "!ERRORLEVEL!"=="0" (
        popd
        echo [ERROR] Electron manager build failed.
        if not defined NO_PAUSE pause
        exit /b 1
    )
    popd
    if exist "%ROOT_DIR%\build\ets2_chat_translator_app" rmdir /S /Q "%ROOT_DIR%\build\ets2_chat_translator_app"
    xcopy "%ROOT_DIR%\manager\dist\win-unpacked" "%ROOT_DIR%\build\ets2_chat_translator_app" /E /I /Y >nul
    if not "!ERRORLEVEL!"=="0" (
        echo [ERROR] Failed to copy Electron manager app folder.
        if not defined NO_PAUSE pause
        exit /b 1
    )
    if not exist "%ROOT_DIR%\build\installer" mkdir "%ROOT_DIR%\build\installer"
    del /q "%ROOT_DIR%\build\installer\*.exe" >nul 2>&1
    for %%F in ("%ROOT_DIR%\manager\dist\ETS2-Chat-Translator-Manager-Setup-*.exe") do copy /Y "%%~fF" "%ROOT_DIR%\build\installer\" >nul
    if not exist "%ROOT_DIR%\build\installer\ETS2-Chat-Translator-Manager-Setup-*.exe" (
        echo [ERROR] Failed to copy manager installer.
        if not defined NO_PAUSE pause
        exit /b 1
    )
)

del /q build\*.obj build\*.exp 2>nul

echo.
echo Build successful:
echo   build\ets2_chat_translator.dll
if exist "build\ets2_chat_translator_app\ETS2 Chat Translator Manager.exe" echo   build\ets2_chat_translator_app\ETS2 Chat Translator Manager.exe
for %%F in ("build\installer\ETS2-Chat-Translator-Manager-Setup-*.exe") do if exist "%%~fF" echo   %%~fF
echo.
if not defined NO_PAUSE pause
