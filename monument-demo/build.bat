@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "%~dp0"
cl /nologo /EHsc /O2 /utf-8 monument.cpp /Fe:MonumentDemo.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib gdiplus.lib
