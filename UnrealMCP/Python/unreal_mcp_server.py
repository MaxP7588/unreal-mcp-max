"""
Unreal Engine MCP Server

Servidor MCP (stdio) que reenvía cada herramienta al plugin UnrealMCP del editor por TCP.

Protocolo con el editor: un JSON {"type": ..., "params": {...}} por línea; cada respuesta es
un JSON por línea. La conexión se mantiene abierta y se reabre sola si el editor se reinicia.

Variables de entorno opcionales: UNREAL_MCP_HOST, UNREAL_MCP_PORT, UNREAL_MCP_TIMEOUT (segundos),
UNREAL_MCP_MODE ("simple" o "completo"; por defecto "completo").
"""

import json
import logging
import os
import socket
import threading
from contextlib import asynccontextmanager
from logging.handlers import RotatingFileHandler
from pathlib import Path
from typing import Any, AsyncIterator, Dict, Optional

from mcp.server.fastmcp import FastMCP

# Log junto al script (no en el directorio de trabajo) y con rotación.
LOG_DIR = Path(__file__).resolve().parent / "logs"
LOG_DIR.mkdir(exist_ok=True)
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    handlers=[RotatingFileHandler(LOG_DIR / "unreal_mcp.log", maxBytes=2_000_000, backupCount=2, encoding="utf-8")],
)
logger = logging.getLogger("UnrealMCP")

UNREAL_HOST = os.environ.get("UNREAL_MCP_HOST", "127.0.0.1")
UNREAL_PORT = int(os.environ.get("UNREAL_MCP_PORT", "55557"))
# Un poco más que el timeout del editor (60 s por defecto) para recibir su propio error de timeout.
RESPONSE_TIMEOUT = float(os.environ.get("UNREAL_MCP_TIMEOUT", "75"))

# Modo simple (estudiantes): sin herramientas que ejecutan código arbitrario o que no se deshacen con Ctrl+Z.
SIMPLE_MODE = os.environ.get("UNREAL_MCP_MODE", "completo").strip().lower() in ("simple", "estudiante", "student")
SIMPLE_MODE_BLOCKED_TOOLS = {"execute_python", "console_command", "delete_asset"}


def _preview(text: str, limit: int = 1500) -> str:
    return text if len(text) <= limit else f"{text[:limit]}... <{len(text)} chars>"


class UnrealConnection:
    """Conexión persistente con el editor. Thread-safe: un comando a la vez."""

    def __init__(self):
        self.socket: Optional[socket.socket] = None
        self._buffer = b""
        self._lock = threading.Lock()

    @property
    def connected(self) -> bool:
        return self.socket is not None

    def connect(self) -> bool:
        self.disconnect()
        try:
            sock = socket.create_connection((UNREAL_HOST, UNREAL_PORT), timeout=5)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            self.socket = sock
            self._buffer = b""
            logger.info("Conectado a Unreal en %s:%s", UNREAL_HOST, UNREAL_PORT)
            return True
        except OSError as exc:
            logger.warning("No se pudo conectar a Unreal en %s:%s: %s", UNREAL_HOST, UNREAL_PORT, exc)
            self.socket = None
            return False

    def disconnect(self):
        if self.socket:
            try:
                self.socket.close()
            except OSError:
                pass
        self.socket = None
        self._buffer = b""

    def _read_line(self) -> bytes:
        assert self.socket is not None
        self.socket.settimeout(RESPONSE_TIMEOUT)
        while b"\n" not in self._buffer:
            chunk = self.socket.recv(1024 * 1024)
            if not chunk:
                raise ConnectionError("El editor cerró la conexión")
            self._buffer += chunk
        line, _, self._buffer = self._buffer.partition(b"\n")
        return line

    def _send_once(self, payload: bytes) -> Dict[str, Any]:
        if not self.socket and not self.connect():
            raise ConnectionError(
                f"No hay conexión con Unreal en {UNREAL_HOST}:{UNREAL_PORT}. "
                "Abre el editor con el proyecto y revisa que el plugin UnrealMCP esté activo."
            )
        assert self.socket is not None
        self.socket.sendall(payload)
        return json.loads(self._read_line().decode("utf-8"))

    def send_command(self, command: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Envía un comando y devuelve {"status": "success", "result": ...} o {"status": "error", "error": ...}."""
        message = json.dumps({"type": command, "params": params or {}}, ensure_ascii=False)
        payload = (message + "\n").encode("utf-8")
        logger.info("-> %s", _preview(message))

        with self._lock:
            response = None
            for attempt in range(2):
                try:
                    response = self._send_once(payload)
                    break
                except socket.timeout:
                    # No se reintenta: el comando puede seguir corriendo en el editor.
                    self.disconnect()
                    return {
                        "status": "error",
                        "error": f"Sin respuesta de Unreal en {RESPONSE_TIMEOUT:.0f}s (¿editor bloqueado por un diálogo?)",
                    }
                except (ConnectionError, OSError) as exc:
                    # Conexión vieja (editor reiniciado): reconectar una vez.
                    self.disconnect()
                    if attempt == 1:
                        return {"status": "error", "error": str(exc)}
                except json.JSONDecodeError as exc:
                    self.disconnect()
                    return {"status": "error", "error": f"Respuesta inválida de Unreal: {exc}"}

        if not isinstance(response, dict):
            return {"status": "error", "error": "Respuesta vacía de Unreal"}

        logger.info("<- %s", _preview(json.dumps(response, ensure_ascii=False)))
        if response.get("status") == "error" and "error" not in response:
            response["error"] = response.get("message", "Error desconocido de Unreal")
        elif response.get("success") is False:
            response = {"status": "error", "error": response.get("error") or response.get("message", "Error desconocido")}
        return response


_unreal_connection: Optional[UnrealConnection] = None
_connection_lock = threading.Lock()


def get_unreal_connection() -> UnrealConnection:
    """Devuelve la conexión compartida. Conecta de forma perezosa en el primer comando."""
    global _unreal_connection
    with _connection_lock:
        if _unreal_connection is None:
            _unreal_connection = UnrealConnection()
        return _unreal_connection


@asynccontextmanager
async def server_lifespan(server: FastMCP) -> AsyncIterator[Dict[str, Any]]:
    logger.info("UnrealMCP server iniciando")
    connection = get_unreal_connection()
    if not connection.connect():
        logger.warning("Unreal no está disponible todavía; se reintentará en cada comando")
    try:
        yield {}
    finally:
        connection.disconnect()
        logger.info("UnrealMCP server detenido")


mcp = FastMCP(
    "UnrealMCP",
    instructions="Control del editor de Unreal Engine 5.5 vía Model Context Protocol",
    lifespan=server_lifespan,
)

if SIMPLE_MODE:
    _register_tool = mcp.tool

    def _tool_without_blocked(*args, **kwargs):
        decorator = _register_tool(*args, **kwargs)

        def wrap(fn):
            if (kwargs.get("name") or fn.__name__) in SIMPLE_MODE_BLOCKED_TOOLS:
                return fn
            return decorator(fn)

        return wrap

    mcp.tool = _tool_without_blocked
    logger.info("Modo simple: desactivadas %s", ", ".join(sorted(SIMPLE_MODE_BLOCKED_TOOLS)))

from tools.editor_tools import register_editor_tools
from tools.level_tools import register_level_tools
from tools.script_tools import register_script_tools
from tools.material_tools import register_material_tools
from tools.blueprint_tools import register_blueprint_tools
from tools.node_tools import register_blueprint_node_tools
from tools.project_tools import register_project_tools
from tools.umg_tools import register_umg_tools
from tools.blueprint_read_tools import register_blueprint_read_tools
from tools.input_tools import register_input_tools
from tools.asset_tools import register_asset_tools
from tools.graph_edit_tools import register_graph_edit_tools

register_editor_tools(mcp)
register_level_tools(mcp)
register_script_tools(mcp)
register_material_tools(mcp)
register_asset_tools(mcp)
register_blueprint_tools(mcp)
register_blueprint_node_tools(mcp)
register_blueprint_read_tools(mcp)
register_graph_edit_tools(mcp)
register_input_tools(mcp)
register_project_tools(mcp)
register_umg_tools(mcp)


@mcp.prompt()
def info():
    """Guía de uso de las herramientas de Unreal MCP."""
    if SIMPLE_MODE:
        escape = (
            "- Modo simple: no hay execute_python, console_command ni delete_asset. Si algo no se puede\n"
            "  hacer con las herramientas, explícale al usuario los pasos para hacerlo a mano en el editor.\n"
            "- El usuario no programa: explica en palabras simples qué cambiaste y cómo probarlo (Play)."
        )
    else:
        escape = (
            "- Para algo que no tenga herramienta propia, usa `execute_python` (API `unreal` de Python)\n"
            "  o `console_command`."
        )
    return """
# Unreal MCP (UE 5.5)

## Reglas
- Los cambios quedan SIN guardar y se pueden deshacer con Ctrl+Z en el editor. Guarda con
  `save_assets` (assets) o `save_level` (nivel) solo cuando el usuario lo pida.
- Los actores se buscan por etiqueta (el nombre del Outliner) o por nombre interno.
- Los assets aceptan nombre corto (si es único) o ruta completa /Game/...
- Si una herramienta falla al conectar, pide al usuario que abra el editor. No uses capturas
  para entender lógica de Blueprints: usa read_blueprint_graph.
{escape}

## Descubrir
- `list_commands` muestra todos los comandos del editor agrupados por toolset.
- `get_level_info`, `get_actors_in_level`, `search_assets`, `get_selected_actors`.

## Propiedades (cualquier objeto)
- `get_object_properties(actor | asset_path, component, property, filter)`
- `set_object_property(actor | asset_path, component, property, value)` o `properties={ruta: valor}`
- Rutas: "RelativeLocation", "RelativeLocation.X", "Tags[0]", "MiComponente.Intensity".
- Vectores/rotadores/colores como [x, y, z(, w)]. Objetos como ruta de asset o nombre corto.

## Nivel y actores
- spawn_actor (clase nativa, Blueprint o StaticMesh), duplicate_actor, delete_actor,
  set_actor_transform, set_actor_label, set_actor_folder, set_actor_tags, attach_actor
- get_actor_components, set_selection, focus_actor, get/set_viewport_camera, take_screenshot
- open_level, save_level, list_levels, start_pie (viewport | vr | simulate | window), stop_pie

## Assets y materiales
- search_assets, get_asset_info, create_asset, duplicate_asset, rename_asset, delete_asset,
  create_folder, import_asset, find_asset_references, save_assets
- create_material_instance, set_material_instance_param, get_material_params, set_actor_material

## Blueprints
- Lectura: list_blueprints, get_blueprint_overview, read_blueprint_graph, find_in_blueprints
- Edición: apply_blueprint_graph (cuerpo completo de una función en una llamada), add_blueprint_node,
  connect_blueprint_pins, set_pin_default, add_blueprint_function, add_custom_event, compile_blueprint
- `compile_blueprint` devuelve errores reales del compilador; léelos antes de seguir.
- Input: usa las herramientas de Enhanced Input; `create_input_mapping` es el input legacy.

## UMG
- create_umg_widget_blueprint, add_widget (cualquier clase de widget), get_widget_tree,
  set_widget_property, bind_widget_event, set_text_block_binding
""".replace("{escape}", escape)


if __name__ == "__main__":
    logger.info("Iniciando servidor MCP con transporte stdio")
    mcp.run(transport="stdio")
