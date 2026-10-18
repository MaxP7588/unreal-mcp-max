"""Blueprint graph editing. Prefer apply_blueprint_graph for a whole function body."""

import logging
from typing import Any, Dict, List, Optional

from mcp.server.fastmcp import Context, FastMCP

from tools.bp_pseudocode import render_dump
from tools.common import CREATE, DESTRUCTIVE, EDIT, READ_ONLY
from tools.input_tools import _command

logger = logging.getLogger("UnrealMCP")


def register_graph_edit_tools(mcp: FastMCP):
    @mcp.tool(annotations=CREATE)
    def add_blueprint_function(
        ctx: Context,
        blueprint_path: str,
        name: str,
        inputs: Optional[List[Dict[str, Any]]] = None,
        outputs: Optional[List[Dict[str, Any]]] = None,
        pure: bool = False,
    ) -> Dict[str, Any]:
        """Create a function graph. Pins use type, optional sub_type, and container."""
        return _command("add_blueprint_function", {
            "blueprint_path": blueprint_path,
            "name": name,
            "inputs": inputs or [],
            "outputs": outputs or [],
            "pure": pure,
        })

    @mcp.tool(annotations=CREATE)
    def add_event_dispatcher(
        ctx: Context,
        blueprint_path: str,
        name: str,
        params: Optional[List[Dict[str, Any]]] = None,
    ) -> Dict[str, Any]:
        """Add an event dispatcher and its signature parameters."""
        return _command("add_event_dispatcher", {"blueprint_path": blueprint_path, "name": name, "params": params or []})

    @mcp.tool(annotations=CREATE)
    def add_custom_event(
        ctx: Context,
        blueprint_path: str,
        name: str,
        graph_name: str = "",
        params: Optional[List[Dict[str, Any]]] = None,
    ) -> Dict[str, Any]:
        """Add a custom event to a graph. graph_name defaults to the Event Graph."""
        payload: Dict[str, Any] = {"blueprint_path": blueprint_path, "name": name, "params": params or []}
        if graph_name:
            payload["graph_name"] = graph_name
        return _command("add_custom_event", payload)

    @mcp.tool(annotations=CREATE)
    def add_blueprint_node(
        ctx: Context,
        blueprint_path: str,
        type: str,
        graph_name: str = "",
        x: float = 0,
        y: float = 0,
        function: str = "",
        target: str = "",
        variable: str = "",
        class_name: str = "",
        name: str = "",
        action: str = "",
        macro: str = "",
        dispatcher: str = "",
        text: str = "",
        defaults: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """Add one node. type is call_function, variable_get, variable_set, branch, sequence, cast, self, event, custom_event, call_dispatcher, bind_dispatcher, enhanced_input_action, macro, component_get, knot, or text (paste one node copied with Ctrl+C or export_blueprint_nodes_text; works for any node class, including AnimGraph nodes)."""
        payload: Dict[str, Any] = {"blueprint_path": blueprint_path, "type": type, "x": x, "y": y}
        if graph_name:
            payload["graph_name"] = graph_name
        for key, value in {
            "function": function, "target": target, "variable": variable, "class": class_name,
            "name": name, "action": action, "macro": macro, "dispatcher": dispatcher, "text": text,
        }.items():
            if value:
                payload[key] = value
        if defaults:
            payload["defaults"] = defaults
        return _command("add_blueprint_node", payload)

    @mcp.tool(annotations=EDIT)
    def connect_blueprint_pins(ctx: Context, blueprint_path: str, from_pin: str, to_pin: str, graph_name: str = "") -> Dict[str, Any]:
        """Connect two pins. Endpoints look like NodeGuid.PinName. graph_name defaults to the Event Graph."""
        payload: Dict[str, Any] = {"blueprint_path": blueprint_path, "from": from_pin, "to": to_pin}
        if graph_name:
            payload["graph_name"] = graph_name
        return _command("connect_blueprint_pins", payload)

    @mcp.tool(annotations=DESTRUCTIVE)
    def disconnect_blueprint_pins(ctx: Context, blueprint_path: str, from_pin: str, graph_name: str = "") -> Dict[str, Any]:
        """Break every link on one pin. from_pin looks like NodeGuid.PinName."""
        payload: Dict[str, Any] = {"blueprint_path": blueprint_path, "from": from_pin}
        if graph_name:
            payload["graph_name"] = graph_name
        return _command("disconnect_blueprint_pins", payload)

    @mcp.tool(annotations=EDIT)
    def set_pin_default(ctx: Context, blueprint_path: str, pin: str, value: str, graph_name: str = "") -> Dict[str, Any]:
        """Set a literal default on a pin. pin looks like NodeGuid.PinName."""
        payload: Dict[str, Any] = {"blueprint_path": blueprint_path, "pin": pin, "value": value}
        if graph_name:
            payload["graph_name"] = graph_name
        return _command("set_pin_default", payload)

    @mcp.tool(annotations=DESTRUCTIVE)
    def delete_blueprint_node(ctx: Context, blueprint_path: str, node_id: str, graph_name: str = "") -> Dict[str, Any]:
        """Delete one node from a graph."""
        payload: Dict[str, Any] = {"blueprint_path": blueprint_path, "node_id": node_id}
        if graph_name:
            payload["graph_name"] = graph_name
        return _command("delete_blueprint_node", payload)

    @mcp.tool(annotations=CREATE)
    def apply_blueprint_graph(
        ctx: Context,
        blueprint_path: str,
        graph_name: str,
        nodes: List[Dict[str, Any]],
        links: Optional[List[Dict[str, Any]]] = None,
        create_graph_if_missing: bool = False,
        compile: bool = True,
    ) -> Dict[str, Any]:
        """Create nodes and wires in one undoable call. Each node has a ref. Links use from/to like ref.pin.

        Node types are the same as add_blueprint_node. Use type=text with a "text" field to paste one node
        exported as text; this is how to add nodes that have no dedicated type, such as AnimGraph nodes.

        On failure the whole edit is cancelled. On success the result includes compiler messages and pseudocode.
        """
        payload = _command("apply_blueprint_graph", {
            "blueprint_path": blueprint_path,
            "graph_name": graph_name,
            "nodes": nodes,
            "links": links or [],
            "create_graph_if_missing": create_graph_if_missing,
            "compile": compile,
        })
        if payload.get("success") and payload.get("graphs"):
            payload["text"] = "\n".join(render_dump(payload))
        return payload

    @mcp.tool(annotations=READ_ONLY)
    def search_callable_functions(
        ctx: Context,
        query: str = "",
        class_name: str = "",
        blueprint_path: str = "",
    ) -> Dict[str, Any]:
        """Find BlueprintCallable functions and their parameters before adding a call_function node."""
        params: Dict[str, Any] = {"query": query}
        if class_name:
            params["class"] = class_name
        if blueprint_path:
            params["blueprint_path"] = blueprint_path
        return _command("search_callable_functions", params)

    logger.info("Graph edit tools registered")
