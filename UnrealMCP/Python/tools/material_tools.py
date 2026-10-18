"""Material instances y asignación de materiales."""

from typing import Any, Dict, Optional

from mcp.server.fastmcp import Context, FastMCP

from tools.common import CREATE, EDIT, READ_ONLY, command


def register_material_tools(mcp: FastMCP):
    @mcp.tool(annotations=CREATE)
    def create_material_instance(
        ctx: Context,
        name: str,
        parent: str,
        folder: str = "/Game/Materials",
        params: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """Crea un Material Instance Constant a partir de un material o instancia padre. No guarda.

        Args:
            name: Nombre del asset, p. ej. "MI_Madera_Oscura".
            parent: Ruta o nombre del material padre.
            folder: Carpeta destino.
            params: Valores iniciales, p. ej. {"BaseColor": [0.3, 0.2, 0.1, 1], "Roughness": 0.7}.
        """
        return command("create_material_instance", {"name": name, "parent": parent, "folder": folder, "params": params})

    @mcp.tool(annotations=EDIT)
    def set_material_instance_param(
        ctx: Context,
        asset_path: str,
        name: Optional[str] = None,
        value: Any = None,
        type: Optional[str] = None,
        params: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """Cambia parámetros de un Material Instance.

        El tipo se deduce del valor: número = scalar, bool = static switch, [r,g,b,a] o "#RRGGBB" = vector,
        ruta de textura = texture. Usa `type` para forzarlo, o `params` para varios a la vez.
        """
        return command("set_material_instance_param", {
            "asset_path": asset_path, "name": name, "value": value, "type": type, "params": params,
        })

    @mcp.tool(annotations=READ_ONLY)
    def get_material_params(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Lista los parámetros scalar/vector/texture/switch de un material o instancia con sus valores."""
        return command("get_material_params", {"asset_path": asset_path})

    @mcp.tool(annotations=EDIT)
    def set_actor_material(
        ctx: Context,
        actor: str,
        material: str,
        slot: int = 0,
        slot_name: Optional[str] = None,
        component: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Asigna un material a los mesh components de un actor (o a uno concreto) en un slot por índice o nombre."""
        return command("set_actor_material", {
            "actor": actor, "material": material, "slot": slot, "slot_name": slot_name, "component": component,
        })
