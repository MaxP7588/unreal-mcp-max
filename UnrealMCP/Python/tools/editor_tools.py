"""Actores del nivel, viewport y capturas."""

from typing import Any, Dict, List, Optional

from mcp.server.fastmcp import Context, FastMCP

from tools.common import CREATE, DESTRUCTIVE, EDIT, READ_ONLY, command


def register_editor_tools(mcp: FastMCP):
    @mcp.tool(annotations=READ_ONLY)
    def get_actors_in_level(
        ctx: Context,
        class_filter: Optional[str] = None,
        folder: Optional[str] = None,
        limit: int = 500,
        pie: bool = False,
    ) -> Dict[str, Any]:
        """Lista los actores del nivel (etiqueta, nombre interno, clase, carpeta y transform).

        Args:
            class_filter: Subcadena del nombre de clase, p. ej. "Light" o "BP_".
            folder: Solo actores en esta carpeta del Outliner (o subcarpetas).
            limit: Máximo de actores devueltos (el total siempre se informa).
            pie: Leer el mundo de PIE en vez del editor.
        """
        return command("get_actors_in_level", {"class_filter": class_filter, "folder": folder, "limit": limit, "pie": pie})

    @mcp.tool(annotations=READ_ONLY)
    def find_actors_by_name(ctx: Context, pattern: str, pie: bool = False) -> Dict[str, Any]:
        """Busca actores cuya etiqueta o nombre interno contenga `pattern`."""
        return command("find_actors_by_name", {"pattern": pattern, "pie": pie})

    @mcp.tool(annotations=CREATE)
    def spawn_actor(
        ctx: Context,
        type: str,
        name: Optional[str] = None,
        location: List[float] = [0.0, 0.0, 0.0],
        rotation: List[float] = [0.0, 0.0, 0.0],
        scale: List[float] = [1.0, 1.0, 1.0],
        static_mesh: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Crea un actor en el nivel (se puede deshacer).

        Args:
            type: Clase nativa ("PointLight", "StaticMeshActor", "/Script/Engine.SkyLight"),
                  ruta de Blueprint ("/Game/Blueprints/BP_Puerta") o ruta de StaticMesh
                  (crea un StaticMeshActor con ese mesh).
            name: Etiqueta del actor en el Outliner.
            location / rotation / scale: Transform; rotation es [pitch, yaw, roll] en grados.
            static_mesh: Mesh para un StaticMeshActor creado por clase.
        """
        return command("spawn_actor", {
            "type": type, "name": name, "location": location, "rotation": rotation, "scale": scale,
            "static_mesh": static_mesh,
        })

    @mcp.tool(annotations=CREATE)
    def spawn_blueprint_actor(
        ctx: Context,
        blueprint_name: str,
        actor_name: Optional[str] = None,
        location: List[float] = [0.0, 0.0, 0.0],
        rotation: List[float] = [0.0, 0.0, 0.0],
        scale: List[float] = [1.0, 1.0, 1.0],
    ) -> Dict[str, Any]:
        """Crea una instancia de un Blueprint en el nivel. blueprint_name acepta nombre corto o ruta /Game/..."""
        return command("spawn_blueprint_actor", {
            "blueprint_name": blueprint_name, "actor_name": actor_name,
            "location": location, "rotation": rotation, "scale": scale,
        })

    @mcp.tool(annotations=DESTRUCTIVE)
    def delete_actor(ctx: Context, name: str) -> Dict[str, Any]:
        """Borra un actor del nivel por etiqueta o nombre interno (se puede deshacer)."""
        return command("delete_actor", {"name": name})

    @mcp.tool(annotations=EDIT)
    def set_actor_transform(
        ctx: Context,
        name: str,
        location: Optional[List[float]] = None,
        rotation: Optional[List[float]] = None,
        scale: Optional[List[float]] = None,
    ) -> Dict[str, Any]:
        """Cambia location/rotation/scale de un actor. Los campos omitidos no cambian."""
        return command("set_actor_transform", {"name": name, "location": location, "rotation": rotation, "scale": scale})

    @mcp.tool(annotations=READ_ONLY)
    def get_actor_properties(
        ctx: Context, name: str, component: Optional[str] = None, filter: Optional[str] = None, include_all: bool = False
    ) -> Dict[str, Any]:
        """Datos de un actor (transform, tags, componentes) y sus propiedades editables con valores."""
        return command("get_actor_properties", {
            "name": name, "component": component, "filter": filter, "include_all": include_all,
        })

    @mcp.tool(annotations=EDIT)
    def set_actor_property(
        ctx: Context, name: str, property_name: str, property_value: Any, component: Optional[str] = None
    ) -> Dict[str, Any]:
        """Asigna una propiedad de un actor o de uno de sus componentes. Acepta rutas como "RelativeLocation.Z".

        Ver set_object_property para el formato de los valores.
        """
        return command("set_actor_property", {
            "name": name, "property_name": property_name, "property_value": property_value, "component": component,
        })

    @mcp.tool(annotations=EDIT)
    def focus_viewport(
        ctx: Context,
        target: Optional[str] = None,
        location: Optional[List[float]] = None,
        distance: float = 1000.0,
        orientation: Optional[List[float]] = None,
    ) -> Dict[str, Any]:
        """Encuadra un actor (target) o mira hacia una posición (location) desde `distance`."""
        return command("focus_viewport", {"target": target, "location": location, "distance": distance, "orientation": orientation})

    @mcp.tool(annotations=CREATE)
    def take_screenshot(ctx: Context, filepath: Optional[str] = None) -> Dict[str, Any]:
        """Captura el viewport (el de PIE si está corriendo) a PNG y devuelve la ruta del archivo.

        Sin filepath guarda en Saved/Screenshots/MCP del proyecto. Úsalo para revisar resultados
        visuales, no para entender lógica de Blueprints.
        """
        return command("take_screenshot", {"filepath": filepath})
