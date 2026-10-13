@echo off
rem Instalador para estudiantes (modo simple): doble clic y elegir el proyecto.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Instalador\instalar.ps1" -Modo simple
pause
