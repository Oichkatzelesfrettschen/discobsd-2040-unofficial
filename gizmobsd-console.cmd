@echo off
rem Run the GizmoBSD console tools from this checkout on Windows without
rem installing them: gizmobsd-console up | down | status. The package
rem lives at distrib\rp2040\host\src; the only dependency is pyserial
rem (python -m pip install --user pyserial). Set PYTHON to pick an
rem interpreter; the default is the python on PATH. The POSIX sh script
rem beside this file serves Linux, macOS and Git Bash.
setlocal
set "here=%~dp0"
if defined PYTHONPATH (
	set "PYTHONPATH=%here%distrib\rp2040\host\src;%PYTHONPATH%"
) else (
	set "PYTHONPATH=%here%distrib\rp2040\host\src"
)
if not defined PYTHON set "PYTHON=python"
"%PYTHON%" -m gizmobsd_host.console %*
endlocal & exit /b %ERRORLEVEL%
