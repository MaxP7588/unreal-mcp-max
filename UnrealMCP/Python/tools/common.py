"""Helpers compartidos por los módulos de herramientas."""

import logging
from typing import Any, Dict, Optional

from mcp.types import ToolAnnotations

logger = logging.getLogger("UnrealMCP")

# Anotaciones MCP de cada herramienta. Los clientes las usan para decidir qué piden confirmar.
# "Destructiva" = borra, descarta o escribe en disco, aunque el cambio en el editor se pueda deshacer con Ctrl+Z.
# Solo lee el editor o el proyecto.
READ_ONLY = ToolAnnotations(readOnlyHint=True, destructiveHint=False, idempotentHint=True, openWorldHint=False)
# Cambia un valor o el estado del editor; repetirla con los mismos argumentos no cambia nada más.
EDIT = ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=True, openWorldHint=False)
# Agrega algo nuevo (asset, actor, nodo, archivo); repetirla agrega otro o falla.
CREATE = ToolAnnotations(readOnlyHint=False, destructiveHint=False, idempotentHint=False, openWorldHint=False)
# Borra, descarta cambios o guarda en disco (no se deshace con Ctrl+Z).
DESTRUCTIVE = ToolAnnotations(readOnlyHint=False, destructiveHint=True, idempotentHint=True, openWorldHint=False)


def unwrap(response: Optional[Dict[str, Any]]) -> Dict[str, Any]:
    """Convierte la respuesta del editor en {"success": bool, ...datos} o {"success": False, "message": ...}."""
    if not isinstance(response, dict):
        return {"success": False, "message": "Sin respuesta de Unreal Engine"}
    if response.get("status") == "error":
        return {"success": False, "message": response.get("error") or response.get("message") or "Error desconocido"}

    result = response.get("result", response)
    if not isinstance(result, dict):
        return {"success": False, "message": "Resultado vacío de Unreal Engine"}
    if result.get("success") is False:
        return {"success": False, "message": result.get("error") or result.get("message") or "Error desconocido"}

    data = result.get("data")
    payload = dict(data) if isinstance(data, dict) else dict(result)
    payload["success"] = True
    return payload


def command(name: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """Envía un comando al editor. Los parámetros con valor None no se envían."""
    from unreal_mcp_server import get_unreal_connection

    clean = {key: value for key, value in (params or {}).items() if value is not None}
    try:
        return unwrap(get_unreal_connection().send_command(name, clean))
    except Exception as exc:  # noqa: BLE001 - cualquier fallo se devuelve como error de la herramienta
        logger.error("Error en %s: %s", name, exc)
        return {"success": False, "message": str(exc)}
