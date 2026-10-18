"""Abrir, cerrar y reiniciar el editor de Unreal.

Corre del lado del servidor MCP (no del plugin), así que funciona aunque el editor esté cerrado.
El proyecto se busca en este orden: parámetro `project`, variable UNREAL_MCP_PROJECT, el editor
abierto, el último proyecto usado (logs/editor_state.json) y los .uproject de las carpetas padre.
El ejecutable sale de UNREAL_EDITOR_PATH o de la instalación del motor según EngineAssociation.
"""

import asyncio
import ctypes
import json
import logging
import os
import socket
import subprocess
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

from mcp.server.fastmcp import Context, FastMCP

from tools.common import DESTRUCTIVE, EDIT, command

logger = logging.getLogger("UnrealMCP")

SERVER_DIR = Path(__file__).resolve().parent.parent
STATE_FILE = SERVER_DIR / "logs" / "editor_state.json"
EDITOR_EXE = "UnrealEditor.exe"

# Script que corre dentro del editor para saber quién es y qué tiene sin guardar.
_INFO_SCRIPT = """
import json, os, sys, unreal
dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
dirty += [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
print(json.dumps({
    "pid": os.getpid(),
    "exe": sys.executable,
    "project": unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path()),
    "dirty": dirty,
}))
"""

_SAVE_SCRIPT = "import unreal\nprint(unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))"
_QUIT_SCRIPT = "import unreal\nunreal.SystemLibrary.quit_editor()\nprint('ok')"

_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


# ---------------------------------------------------------------- estado y rutas

def _load_state() -> Dict[str, Any]:
    try:
        return json.loads(STATE_FILE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def _save_state(**values: Any):
    state = _load_state()
    state.update({key: value for key, value in values.items() if value})
    try:
        STATE_FILE.parent.mkdir(exist_ok=True)
        STATE_FILE.write_text(json.dumps(state, indent=2), encoding="utf-8")
    except OSError as exc:
        logger.warning("No se pudo guardar %s: %s", STATE_FILE, exc)


def _norm(path: str) -> str:
    return os.path.normcase(os.path.normpath(path.strip().strip('"')))


def _find_uproject_upwards() -> Optional[str]:
    for folder in [SERVER_DIR, *SERVER_DIR.parents]:
        found = sorted(folder.glob("*.uproject"))
        if found:
            return str(found[0])
    return None


def _resolve_project(project: Optional[str], running: Optional[Dict[str, Any]] = None) -> Optional[str]:
    candidates = [
        project,
        os.environ.get("UNREAL_MCP_PROJECT"),
        (running or {}).get("project"),
        _load_state().get("project"),
        _find_uproject_upwards(),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file() and candidate.lower().endswith(".uproject"):
            return str(Path(candidate).resolve())
    return None


def _engine_dir_for(association: str) -> Optional[Path]:
    """Carpeta raíz del motor para un EngineAssociation ("5.5" o el GUID de un build de código)."""
    candidates: List[str] = []
    try:
        launcher = Path(os.environ.get("PROGRAMDATA", r"C:\ProgramData")) / "Epic" / "UnrealEngineLauncher" / "LauncherInstalled.dat"
        for item in json.loads(launcher.read_text(encoding="utf-8")).get("InstallationList", []):
            if item.get("AppName") == f"UE_{association}" or item.get("ArtifactId") == f"UE_{association}":
                candidates.append(item.get("InstallLocation", ""))
    except (OSError, ValueError):
        pass

    try:
        import winreg

        for hive, key, value in [
            (winreg.HKEY_LOCAL_MACHINE, rf"SOFTWARE\EpicGames\Unreal Engine\{association}", "InstalledDirectory"),
            (winreg.HKEY_CURRENT_USER, r"Software\Epic Games\Unreal Engine\Builds", association),
        ]:
            try:
                with winreg.OpenKey(hive, key) as handle:
                    candidates.append(winreg.QueryValueEx(handle, value)[0])
            except OSError:
                pass
    except ImportError:
        pass

    candidates.append(rf"C:\Program Files\Epic Games\UE_{association}")
    for candidate in candidates:
        if candidate and (Path(candidate) / "Engine" / "Binaries" / "Win64" / EDITOR_EXE).is_file():
            return Path(candidate)
    return None


def _resolve_editor_exe(project: str) -> Optional[str]:
    env_exe = os.environ.get("UNREAL_EDITOR_PATH")
    if env_exe and Path(env_exe).is_file():
        return env_exe
    try:
        association = json.loads(Path(project).read_text(encoding="utf-8-sig")).get("EngineAssociation", "")
    except (OSError, ValueError):
        association = ""
    if association:
        engine = _engine_dir_for(association)
        if engine:
            return str(engine / "Engine" / "Binaries" / "Win64" / EDITOR_EXE)
    cached = _load_state().get("exe")
    if cached and Path(cached).is_file():
        return cached
    return None


# ---------------------------------------------------------------- procesos

def _pid_alive(pid: int) -> bool:
    """True si el proceso sigue vivo (OpenProcess + GetExitCodeProcess, sin lanzar nada)."""
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        if not kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
            return False
        return code.value == 259  # STILL_ACTIVE
    finally:
        kernel32.CloseHandle(handle)


def _list_editor_processes() -> List[Dict[str, Any]]:
    script = (
        f"Get-CimInstance Win32_Process -Filter \"Name='{EDITOR_EXE}'\" | "
        "Select-Object ProcessId,ExecutablePath,CommandLine | ConvertTo-Json -Compress"
    )
    try:
        out = subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
            capture_output=True, text=True, timeout=20, creationflags=_NO_WINDOW,
        ).stdout.strip()
        data = json.loads(out) if out else []
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        logger.warning("No se pudo listar procesos del editor: %s", exc)
        return []
    return data if isinstance(data, list) else [data]


def _find_editor_process(project: Optional[str]) -> Optional[Dict[str, Any]]:
    """Proceso UnrealEditor.exe de este proyecto (por la línea de comandos)."""
    processes = _list_editor_processes()
    if project:
        target = _norm(project)
        stem = Path(project).stem.lower()
        for proc in processes:
            cmd = proc.get("CommandLine") or ""
            if target in _norm(cmd) or f"{stem}.uproject" in cmd.lower():
                return proc
    return None


def _port_open() -> bool:
    from unreal_mcp_server import UNREAL_HOST, UNREAL_PORT

    try:
        with socket.create_connection((UNREAL_HOST, UNREAL_PORT), timeout=2):
            return True
    except OSError:
        return False


def _run_in_editor(code: str) -> Dict[str, Any]:
    return command("execute_python", {"code": code, "mode": "file"})


def _editor_info() -> Optional[Dict[str, Any]]:
    """Pregunta al editor conectado su PID, proyecto y paquetes sin guardar. None si no responde."""
    response = _run_in_editor(_INFO_SCRIPT)
    if not response.get("success"):
        return None
    for line in reversed((response.get("output") or "").splitlines()):
        try:
            return json.loads(line)
        except ValueError:
            continue
    return None


def _drop_connection():
    from unreal_mcp_server import get_unreal_connection

    get_unreal_connection().disconnect()


async def _wait_exit(pid: int, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not _pid_alive(pid):
            return True
        await asyncio.sleep(1)
    return not _pid_alive(pid)


def _kill(pid: int, force: bool) -> bool:
    """Sin force manda WM_CLOSE (cierre normal); con force termina el proceso."""
    args = ["taskkill", "/PID", str(pid)] + (["/F"] if force else [])
    result = subprocess.run(args, capture_output=True, text=True, creationflags=_NO_WINDOW)
    return result.returncode == 0


# ---------------------------------------------------------------- lógica de las herramientas

async def close_editor_impl(unsaved: str, force: bool, timeout: float, project: Optional[str]) -> Dict[str, Any]:
    if unsaved not in ("abort", "save", "discard"):
        return {"success": False, "message": 'unsaved debe ser "abort", "save" o "discard"'}

    info = _editor_info()
    if info:
        _save_state(project=info.get("project"), exe=info.get("exe"))
        pid = int(info["pid"])
    else:
        proc = _find_editor_process(_resolve_project(project))
        if not proc:
            return {"success": True, "closed": True, "message": "El editor ya estaba cerrado."}
        pid = int(proc["ProcessId"])
        _save_state(exe=proc.get("ExecutablePath"))
        if not force:
            return {
                "success": False,
                "pid": pid,
                "message": "El editor está abierto pero no responde por MCP, así que no se puede revisar si hay "
                           "cambios sin guardar. Pide confirmación al usuario y usa force=true para cerrarlo igual "
                           "(se pierden los cambios sin guardar).",
            }

    dirty = (info or {}).get("dirty") or []
    saved: List[str] = []
    if dirty and unsaved == "abort":
        return {
            "success": False,
            "pid": pid,
            "dirty": dirty,
            "message": "Hay paquetes sin guardar. Pregunta al usuario y vuelve a llamar con unsaved=\"save\" "
                       "o unsaved=\"discard\".",
        }
    if dirty and unsaved == "save":
        result = _run_in_editor(_SAVE_SCRIPT)
        after = _editor_info()
        still_dirty = (after or {}).get("dirty") or []
        if not result.get("success") or still_dirty:
            return {
                "success": False,
                "pid": pid,
                "dirty": still_dirty or dirty,
                "message": "No se pudo guardar todo; el editor sigue abierto. " + (result.get("message") or ""),
            }
        saved = dirty

    if info is None or (dirty and unsaved == "discard"):
        # Sin respuesta del editor, o descartando cambios: cerrar sin diálogos.
        method = "terminado (force)"
        _kill(pid, force=True)
    else:
        method = "cierre normal"
        if not _run_in_editor(_QUIT_SCRIPT).get("success"):
            _kill(pid, force=False)

    _drop_connection()
    closed = await _wait_exit(pid, timeout)
    if not closed and force:
        _kill(pid, force=True)
        method = "terminado (force) tras esperar"
        closed = await _wait_exit(pid, 15)

    payload: Dict[str, Any] = {"success": closed, "closed": closed, "pid": pid, "method": method}
    if saved:
        payload["saved"] = saved
    if dirty and unsaved == "discard":
        payload["discarded"] = dirty
    if not closed:
        payload["message"] = (
            f"El editor sigue abierto tras {timeout:.0f}s; probablemente hay un diálogo esperando al usuario. "
            "Usa force=true para terminarlo."
        )
    return payload


async def open_editor_impl(project: Optional[str], wait: bool, timeout: float) -> Dict[str, Any]:
    project_path = _resolve_project(project)
    if not project_path:
        return {
            "success": False,
            "message": "No encontré el .uproject. Pásalo en `project` o define UNREAL_MCP_PROJECT.",
        }

    running = _find_editor_process(project_path)
    if running:
        ready = _port_open()
        return {
            "success": True,
            "already_running": True,
            "pid": running["ProcessId"],
            "mcp_ready": ready,
            "project": project_path,
            "message": "El editor ya estaba abierto." + ("" if ready else " Aún no responde por MCP (¿cargando?)."),
        }

    exe = _resolve_editor_exe(project_path)
    if not exe:
        return {
            "success": False,
            "project": project_path,
            "message": "No encontré UnrealEditor.exe para este proyecto. Define UNREAL_EDITOR_PATH.",
        }

    flags = getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    proc = subprocess.Popen(
        [exe, project_path],
        cwd=str(Path(project_path).parent),
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        creationflags=flags, close_fds=True,
    )
    _save_state(project=project_path, exe=exe)
    logger.info("Editor lanzado (pid %s): %s %s", proc.pid, exe, project_path)

    payload: Dict[str, Any] = {"success": True, "pid": proc.pid, "project": project_path, "exe": exe}
    if not wait:
        payload["message"] = "Editor lanzando; tarda en cargar. Las herramientas fallarán hasta que termine."
        return payload

    started = time.monotonic()
    while time.monotonic() - started < timeout:
        if proc.poll() is not None:
            return {
                **payload,
                "success": False,
                "exit_code": proc.returncode,
                "message": "El editor se cerró durante el arranque. Revisa Saved/Logs del proyecto.",
            }
        if _port_open() and command("list_commands").get("success"):
            payload.update(mcp_ready=True, seconds=round(time.monotonic() - started))
            payload["message"] = "Editor abierto y listo."
            return payload
        await asyncio.sleep(3)

    payload.update(mcp_ready=False, seconds=round(time.monotonic() - started))
    payload["message"] = (
        f"El editor sigue cargando tras {timeout:.0f}s o espera un diálogo (p. ej. recompilar módulos o "
        "restaurar paquetes). Pide al usuario que lo revise."
    )
    return payload


def register_editor_process_tools(mcp: FastMCP):
    @mcp.tool(annotations=DESTRUCTIVE)
    async def close_editor(
        ctx: Context,
        unsaved: str = "abort",
        force: bool = False,
        timeout: float = 60,
        project: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Cierra el editor de Unreal (p. ej. para compilar C++ con Build.bat).

        Si hay cambios sin guardar y unsaved="abort", no cierra y devuelve la lista `dirty`:
        pregunta al usuario antes de volver a llamar con "save" o "discard".

        Args:
            unsaved: "abort" (por defecto), "save" (guarda todo y cierra) o "discard" (cierra sin guardar).
            force: Termina el proceso si no responde por MCP o no se cierra en `timeout` segundos.
            timeout: Segundos a esperar a que el proceso termine.
            project: Ruta al .uproject si no se puede deducir.
        """
        return await close_editor_impl(unsaved, force, timeout, project)

    @mcp.tool(annotations=EDIT)
    async def open_editor(
        ctx: Context,
        project: Optional[str] = None,
        wait: bool = True,
        timeout: float = 600,
    ) -> Dict[str, Any]:
        """Abre el editor de Unreal con el proyecto y, con wait=true, espera a que el MCP responda.

        Args:
            project: Ruta al .uproject. Por defecto el del último editor usado o el de las carpetas padre.
            wait: Esperar a que el editor termine de cargar y el plugin acepte comandos.
            timeout: Segundos máximos de espera (la primera carga con shaders puede tardar varios minutos).
        """
        return await open_editor_impl(project, wait, timeout)

    @mcp.tool(annotations=DESTRUCTIVE)
    async def restart_editor(
        ctx: Context,
        unsaved: str = "abort",
        force: bool = False,
        timeout: float = 600,
        project: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Cierra y vuelve a abrir el editor, esperando a que el MCP responda.

        Args:
            unsaved: Igual que en close_editor: "abort", "save" o "discard".
            force: Igual que en close_editor.
            timeout: Segundos máximos de espera para que vuelva a cargar.
            project: Ruta al .uproject si no se puede deducir.
        """
        closed = await close_editor_impl(unsaved, force, 60, project)
        if not closed.get("success"):
            return {**closed, "reopened": False}
        opened = await open_editor_impl(project, True, timeout)
        return {**opened, "close": closed}

    logger.info("Editor process tools registered")
