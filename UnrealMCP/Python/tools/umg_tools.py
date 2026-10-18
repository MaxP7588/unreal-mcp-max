"""Widget Blueprints (UMG). Los widgets se buscan por nombre corto o ruta /Game/..."""

from typing import Any, Dict, List, Optional

from mcp.server.fastmcp import Context, FastMCP

from tools.common import CREATE, EDIT, READ_ONLY, command


def register_umg_tools(mcp: FastMCP):
    @mcp.tool(annotations=CREATE)
    def create_umg_widget_blueprint(
        ctx: Context,
        name: str,
        path: str = "/Game/UI",
        parent_class: str = "UserWidget",
        root_widget: str = "CanvasPanel",
    ) -> Dict[str, Any]:
        """Crea un Widget Blueprint con un widget raíz (CanvasPanel por defecto). No guarda."""
        return command("create_umg_widget_blueprint", {
            "name": name, "path": path, "parent_class": parent_class, "root_widget": root_widget,
        })

    @mcp.tool(annotations=READ_ONLY)
    def get_widget_tree(ctx: Context, blueprint_name: str) -> Dict[str, Any]:
        """Árbol de widgets de un Widget Blueprint: nombres, clases, slots, textos y bindings."""
        return command("get_widget_tree", {"blueprint_name": blueprint_name})

    @mcp.tool(annotations=CREATE)
    def add_widget(
        ctx: Context,
        blueprint_name: str,
        widget_type: str,
        widget_name: str,
        parent: Optional[str] = None,
        text: Optional[str] = None,
        position: Optional[List[float]] = None,
        size: Optional[List[float]] = None,
        properties: Optional[Dict[str, Any]] = None,
        slot: Optional[Dict[str, Any]] = None,
        is_variable: bool = True,
    ) -> Dict[str, Any]:
        """Agrega cualquier widget (TextBlock, Button, Image, VerticalBox, HorizontalBox, Border, Overlay,
        ProgressBar, Slider, o un Widget Blueprint por ruta) dentro de un panel.

        Args:
            parent: Panel padre; por defecto el widget raíz.
            text: Texto inicial si es un TextBlock.
            position / size: Atajos para slots de CanvasPanel, en píxeles.
            properties: Propiedades del widget por reflexión, p. ej. {"ColorAndOpacity": [1,0,0,1]}.
            slot: Propiedades del slot, p. ej. {"Padding": "(Left=8,Top=8,Right=8,Bottom=8)"}.
        """
        return command("add_widget", {
            "blueprint_name": blueprint_name, "widget_type": widget_type, "widget_name": widget_name,
            "parent": parent, "text": text, "position": position, "size": size,
            "properties": properties, "slot": slot, "is_variable": is_variable,
        })

    @mcp.tool(annotations=EDIT)
    def set_widget_property(
        ctx: Context,
        blueprint_name: str,
        widget_name: str,
        properties: Optional[Dict[str, Any]] = None,
        text: Optional[str] = None,
        position: Optional[List[float]] = None,
        size: Optional[List[float]] = None,
        slot: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """Cambia propiedades de un widget existente y/o de su slot."""
        return command("set_widget_property", {
            "blueprint_name": blueprint_name, "widget_name": widget_name, "properties": properties,
            "text": text, "position": position, "size": size, "slot": slot,
        })

    @mcp.tool(annotations=CREATE)
    def add_text_block_to_widget(
        ctx: Context,
        blueprint_name: str,
        widget_name: str,
        text: str = "New Text Block",
        parent: Optional[str] = None,
        position: Optional[List[float]] = None,
        size: Optional[List[float]] = None,
    ) -> Dict[str, Any]:
        """Atajo de add_widget para un TextBlock."""
        return command("add_text_block_to_widget", {
            "blueprint_name": blueprint_name, "widget_name": widget_name, "text": text,
            "parent": parent, "position": position, "size": size,
        })

    @mcp.tool(annotations=CREATE)
    def add_button_to_widget(
        ctx: Context,
        blueprint_name: str,
        widget_name: str,
        text: Optional[str] = None,
        parent: Optional[str] = None,
        position: Optional[List[float]] = None,
        size: Optional[List[float]] = None,
    ) -> Dict[str, Any]:
        """Agrega un Button (con un TextBlock hijo si se indica `text`)."""
        return command("add_button_to_widget", {
            "blueprint_name": blueprint_name, "widget_name": widget_name, "text": text,
            "parent": parent, "position": position, "size": size,
        })

    @mcp.tool(annotations=CREATE)
    def bind_widget_event(ctx: Context, blueprint_name: str, widget_name: str, event_name: str) -> Dict[str, Any]:
        """Crea (o encuentra) el nodo de evento de un widget en el Event Graph, p. ej. OnClicked de un Button."""
        return command("bind_widget_event", {
            "blueprint_name": blueprint_name, "widget_name": widget_name, "event_name": event_name,
        })

    @mcp.tool(annotations=EDIT)
    def set_text_block_binding(ctx: Context, blueprint_name: str, widget_name: str, binding_name: str) -> Dict[str, Any]:
        """Enlaza el Text de un TextBlock a una variable Text del widget (la crea si no existe)."""
        return command("set_text_block_binding", {
            "blueprint_name": blueprint_name, "widget_name": widget_name, "binding_name": binding_name,
        })

    @mcp.tool(annotations=CREATE)
    def add_widget_to_viewport(ctx: Context, blueprint_name: str, z_order: int = 0) -> Dict[str, Any]:
        """Con PIE corriendo, crea el widget y lo muestra en pantalla para probarlo."""
        return command("add_widget_to_viewport", {"blueprint_name": blueprint_name, "z_order": z_order})
