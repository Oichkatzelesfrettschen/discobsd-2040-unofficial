@echo off
rem discobsd-console is the former name of gizmobsd-console. For one release
rem this prints one line on stderr and runs the new command with the same
rem arguments.
echo discobsd-console is renamed gizmobsd-console; use the new name 1>&2
"%~dp0gizmobsd-console.cmd" %*
exit /b %ERRORLEVEL%
