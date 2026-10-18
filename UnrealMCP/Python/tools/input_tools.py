"""Enhanced Input tools. Use these instead of the legacy create_input_mapping."""

import logging
from typing import Any, Dict, List, Optional

from mcp.server.fastmcp import Context, FastMCP

from tools.common import CREATE, DESTRUCTIVE, READ_ONLY

logger = logging.getLogger("UnrealMCP")


def _command(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
    from unreal_mcp_server import get_unreal_connection

    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine. Open the editor with this project."}
        response = unreal.send_command(command, params)
        if not isinstance(response, dict):
            return {"success": False, "message": "No response from Unreal Engine"}
        if response.get("status") == "error":
            return {"success": False, "message": response.get("error") or "Unknown error"}
        result = response.get("result", response)
        if isinstance(result, dict) and result.get("success") is False:
            return {"success": False, "message": result.get("error") or result.get("message") or "Unknown error"}
        data = result.get("data") if isinstance(result, dict) else None
        if isinstance(data, dict):
            data = dict(data)
            data["success"] = True
            return data
        if isinstance(result, dict):
            result = dict(result)
            result["success"] = True
            return result
        return {"success": False, "message": "Empty result"}
    except Exception as exc:
        logger.error("Error in %s: %s", command, exc)
        return {"success": False, "message": str(exc)}


def register_input_tools(mcp: FastMCP):
    @mcp.tool(annotations=READ_ONLY)
    def read_input_asset(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Read an Enhanced Input Action or Mapping Context.

        Returns the value type, whether it consumes input, triggers, modifiers,
        and, for a context, every action with its keys.
        """
        return _command("read_input_asset", {"asset_path": asset_path})

    @mcp.tool(annotations=READ_ONLY)
    def find_input_bindings(ctx: Context, query: str, path: str = "/Game") -> Dict[str, Any]:
        """Find Enhanced Input actions or keys by name. Example: IA_Inspeccionar or Trigger."""
        return _command("find_input_bindings", {"query": query, "path": path})

    @mcp.tool(annotations=CREATE)
    def add_enhanced_input_mapping(
        ctx: Context,
        context_path: str,
        keys: List[str],
        action_path: str = "",
        action_name: str = "",
        folder: str = "/Game/Input/Actions",
        value_type: str = "Boolean",
        triggers: Optional[List[str]] = None,
        modifiers: Optional[List[str]] = None,
    ) -> Dict[str, Any]:
        """Add keys to an Enhanced Input Mapping Context.

        Reuses action_path or creates action_name. Keys look like OculusTouch_Right_Trigger_Axis.
        This does not write the legacy project input settings.
        """
        params: Dict[str, Any] = {"context_path": context_path, "keys": keys, "value_type": value_type, "folder": folder}
        if action_path:
            params["action_path"] = action_path
        if action_name:
            params["action_name"] = action_name
        if triggers:
            params["triggers"] = triggers
        if modifiers:
            params["modifiers"] = modifiers
        return _command("add_enhanced_input_mapping", params)

    @mcp.tool(annotations=DESTRUCTIVE)
    def remove_enhanced_input_mapping(ctx: Context, context_path: str, action: str, key: str) -> Dict[str, Any]:
        """Remove one key mapping from an Enhanced Input Mapping Context."""
        return _command("remove_enhanced_input_mapping", {"context_path": context_path, "action": action, "key": key})
