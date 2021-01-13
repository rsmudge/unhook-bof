@echo off
set PLAT="x86"
IF "%Platform%"=="x64" set PLAT="x64"

cl.exe /GS- /nologo /Od /Oi /c /Isrc src\unhook.c /Founhook.%PLAT%.o
