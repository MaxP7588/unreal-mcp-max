"""
Read-only Blueprint tools.

Use these instead of screenshots. They return graph logic as text.
"""

import logging
from typing import Any, Dict, List, Optional

from mcp.server.fastmcp import Context, FastMCP

from tools.bp_pseudocode import render_dump
from tools.common import READ_ONLY

logger = logging.getLogger("UnrealMCP")


def _unwrap(response: Optional[Dict[str, Any]]) -> Dict[str, Any]:
    if not isinstance(response, dict):
        return {"success": False, "message": "No response from Unreal Engine"}
    if response.get("status") == "error":
        return {"success": False, "message": response.get("error") or response.get("message") or "Unknown error"}

    result = response.get("result")
    if not isinstance(result, dict):
        return {"success": False, "message": "Empty result from Unreal Engine"}
    if result.get("success") is False:
        return {"success": False, "message": result.get("error") or result.get("message") or "Unknown error"}

    data = result.get("data")
    if isinstance(data, dict):
        payload = dict(data)
        payload["success"] = True
        return payload

    payload = dict(result)
    payload["success"] = True
    return payload


def _command(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
    from unreal_mcp_server import get_unreal_connection

    try:
        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine. Open the editor with this project."}
        response = unreal.send_command(command, params)
        return _unwrap(response)
    except Exception as exc:
        logger.error("Error in %s: %s", command, exc)
        return {"success": False, "message": str(exc)}


def register_blueprint_read_tools(mcp: FastMCP):
    """Register read-only Blueprint tools."""

    @mcp.tool(annotations=READ_ONLY)
    def list_blueprints(
        ctx: Context,
        path: str = "/Game",
        kind: str = "",
        limit: int = 300,
    ) -> Dict[str, Any]:
        """Use this instead of screenshots to read Blueprint logic.

        List Blueprint, map, DataTable, struct, and enum assets under a folder.
        Start here when you do not know the asset path.

        Args:
            path: Content folder, for example /Game
            kind: Optional filter: blueprint, map, datatable, struct, or enum
            limit: Maximum assets to return
        """
        params: Dict[str, Any] = {"path": path, "limit": limit}
        if kind:
            params["kind"] = kind
        return _command("list_blueprints", params)

    @mcp.tool(annotations=READ_ONLY)
    def get_blueprint_overview(ctx: Context, blueprint_path: str) -> Dict[str, Any]:
        """Use this instead of screenshots to read Blueprint logic.

        Read parent class, interfaces, components, variables (with defaults),
        functions, events, dispatchers, macros, graph sizes, and compile status.
        Pass a map path to read the open Level Blueprint.

        Args:
            blueprint_path: Asset path, such as /Game/Blueprints/BP_Player
        """
        return _command("get_blueprint_overview", {"blueprint_path": blueprint_path})

    @mcp.tool(annotations=READ_ONLY)
    def read_blueprint_graph(
        ctx: Context,
        blueprint_path: str,
        graph_name: str = "",
        entry: str = "",
        format: str = "pseudocode",
        offset: int = 0,
        limit: int = 0,
    ) -> Dict[str, Any]:
        """Use this instead of screenshots to read Blueprint logic.

        Read nodes and pin connections. Default format is pseudocode, which shows
        execution order, data wires, unconnected exec pins, comments, and collapsed
        graphs. Use format=json with offset and limit only when you need raw pins.
        Pass a map path to read the open Level Blueprint.

        Args:
            blueprint_path: Asset path, or the open map path for its Level Blueprint
            graph_name: Optional graph, function, or collapsed-graph name
            entry: Optional event, custom event, or function name to isolate
            format: pseudocode or json
            offset: JSON pagination start. Ignored for pseudocode
            limit: JSON page size. Ignored for pseudocode
        """
        params: Dict[str, Any] = {"blueprint_path": blueprint_path}
        if graph_name:
            params["graph_name"] = graph_name
        if entry:
            params["entry"] = entry
        if format == "json":
            if offset:
                params["offset"] = offset
            if limit:
                params["limit"] = limit

        payload = _command("read_blueprint_graph", params)
        if not payload.get("success") or format == "json":
            return payload

        graphs = payload.get("graphs") or []
        return {
            "success": True,
            "blueprint": payload.get("blueprint"),
            "path": payload.get("path"),
            "format": "pseudocode",
            "graphs": [{"name": graph.get("name"), "nodes": len(graph.get("nodes") or [])} for graph in graphs],
            "text": "\n".join(render_dump(payload)),
        }

    @mcp.tool(annotations=READ_ONLY)
    def find_in_blueprints(
        ctx: Context,
        query: str,
        path: str = "/Game",
        max_assets: int = 200,
        max_results: int = 80,
    ) -> Dict[str, Any]:
        """Use this instead of screenshots to read Blueprint logic.

        Find which Blueprints call a function, use a variable, or fire an event.
        Example: query CambiarEstado.

        Args:
            query: Function, variable, or event name
            path: Folder to scan
            max_assets: Maximum Blueprints to load
            max_results: Maximum hits to return
        """
        return _command("find_in_blueprints", {
            "query": query,
            "path": path,
            "max_assets": max_assets,
            "max_results": max_results,
        })

    @mcp.tool(annotations=READ_ONLY)
    def export_blueprint_nodes_text(
        ctx: Context,
        blueprint_path: str,
        graph_name: str,
        node_ids: Optional[List[str]] = None,
    ) -> Dict[str, Any]:
        """Use this instead of screenshots to read Blueprint logic.

        Export the same text Unreal copies with Ctrl+C. Use it only when pseudocode
        is not detailed enough. Pass node_ids to export a subset.

        Args:
            blueprint_path: Asset path
            graph_name: Graph that contains the nodes
            node_ids: Optional node ids from read_blueprint_graph
        """
        params: Dict[str, Any] = {
            "blueprint_path": blueprint_path,
            "graph_name": graph_name,
        }
        if node_ids:
            params["node_ids"] = node_ids
        return _command("export_blueprint_nodes_text", params)

    @mcp.tool(annotations=READ_ONLY)
    def read_data_asset(
        ctx: Context,
        asset_path: str,
        offset: int = 0,
        limit: int = 50,
    ) -> Dict[str, Any]:
        """Use this instead of screenshots to read Blueprint logic.

        Read a DataTable (rows), a user struct (fields), or an enum (display names).

        Args:
            asset_path: Asset path
            offset: First DataTable row to return
            limit: How many DataTable rows to return
        """
        return _command("read_data_asset", {
            "asset_path": asset_path,
            "offset": offset,
            "limit": limit,
        })

    logger.info("Blueprint read tools registered successfully")
