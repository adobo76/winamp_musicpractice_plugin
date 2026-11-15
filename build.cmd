@echo off
rem Builds dsp_musicpractice.dll (32-bit, like Winamp) and the installer.
rem Needs Visual Studio 2019/2022 with the C++ desktop workload.
setlocal
set ROOT=%~dp0
set OUT=%ROOT%build
set ST=%ROOT%third_party\soundtouch

if not exist "%ST%\source\SoundTouch\SoundTouch.cpp" (
  echo SoundTouch source missing. Run: git submodule update --init
  exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if "%VSDIR%"=="" ( echo Visual Studio C++ tools not found. & exit /b 1 )
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1

if not exist "%OUT%\obj" mkdir "%OUT%\obj"

set CFLAGS=/nologo /O2 /MT /EHsc /std:c++17 /W3 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /arch:SSE2 /I"%ST%\include" /I"%ROOT%src"
set STSRC=AAFilter.cpp FIFOSampleBuffer.cpp FIRFilter.cpp cpu_detect_x86.cpp mmx_optimized.cpp sse_optimized.cpp RateTransposer.cpp SoundTouch.cpp TDStretch.cpp InterpolateCubic.cpp InterpolateLinear.cpp InterpolateShannon.cpp
set STFILES=
for %%f in (%STSRC%) do call set STFILES=%%STFILES%% "%ST%\source\SoundTouch\%%f"

echo == Building dsp_musicpractice.dll
cl %CFLAGS% /Fo"%OUT%\obj\\" /LD "%ROOT%src\plugin.cpp" "%ROOT%src\dsp.cpp" "%ROOT%src\ui.cpp" %STFILES% ^
   /Fe"%OUT%\dsp_musicpractice.dll" /link /DLL /MAP:"%OUT%\dsp_musicpractice.map" user32.lib gdi32.lib comctl32.lib || exit /b 1

echo == Building MusicPractice-Setup.exe
rc /nologo /I"%OUT%" /fo"%OUT%\obj\installer.res" "%ROOT%installer\installer.rc" || exit /b 1
cl %CFLAGS% /Fo"%OUT%\obj\\" "%ROOT%installer\installer.cpp" "%OUT%\obj\installer.res" ^
   /Fe"%OUT%\MusicPractice-Setup.exe" /link /SUBSYSTEM:WINDOWS /MANIFEST:NO user32.lib shell32.lib comctl32.lib ole32.lib advapi32.lib || exit /b 1

echo.
echo Done: %OUT%\dsp_musicpractice.dll and %OUT%\MusicPractice-Setup.exe
