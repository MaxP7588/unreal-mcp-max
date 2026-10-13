@echo off
rem Instalador en modo completo: todas las herramientas, compila el plugin con el proyecto.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Instalador\instalar.ps1" -Modo completo
pause
