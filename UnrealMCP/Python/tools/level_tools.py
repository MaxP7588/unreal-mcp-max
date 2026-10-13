"""Nivel, selección, PIE, cámara del viewport y propiedades genéricas de cualquier objeto."""

from typing import Any, Dict, List, Optional

from mcp.server.fastmcp import Context, FastMCP

from tools.common import command


def register_level_tools(mcp: FastMCP):
    # --- Propiedades genéricas -------------------------------------------------

    @mcp.tool()
    def get_object_properties(
        ctx: Context,
        actor: Optional[str] = None,
        asset_path: Optional[str] = None,
        component: Optional[str] = None,
        property: Optional[str] = None,
        filter: Optional[str] = None,
        include_all: bool = False,
        pie: bool = False,
    ) -> Dict[str, Any]:
        """Lee propiedades de un actor, un componente de un actor o un asset.

        Args:
            actor: Etiqueta o nombre interno del actor en el nivel.
            asset_path: Ruta o nombre corto de un asset (en vez de actor).
            component: Nombre del componente dentro del actor.
            property: Ruta de una propiedad concreta (p. ej. "RelativeLocation.Z", "Tags[0]"). Sin esto lista todas.
            filter: Subcadena para filtrar nombres al listar.
            include_all: Incluir propiedades no editables.
            pie: Leer del mundo de PIE.
        """
        return command("get_object_properties", {
            "actor": actor, "asset_path": asset_path, "component": component, "property": property,
            "filter": filter, "include_all": include_all, "pie": pie,
        })

    @mcp.tool()
    def set_object_property(
        ctx: Context,
        actor: Optional[str] = None,
        asset_path: Optional[str] = None,
        component: Optional[str] = None,
        property: Optional[str] = None,
        value: Any = None,
        properties: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """Asigna una o varias propiedades por reflexión (se puede deshacer con Ctrl+Z).

        Valores: números, bool, strings, enums por nombre, [x,y,z] para vectores/rotadores,
        [r,g,b,a] para colores, rutas o nombres de assets para referencias, objetos JSON para structs,
        o el texto de copiar/pegar de Unreal como "(X=1,Y=2,Z=3)".

        Args:
            actor / asset_path / component: Objeto destino (igual que get_object_properties).
            property: Ruta de la propiedad, p. ej. "Intensity", "RelativeLocation.X", "LightComponent.LightColor".
            value: Valor para `property`.
            properties: Varias a la vez: {"Intensity": 5000, "LightColor": [1, 0.5, 0.2, 1]}.
        """
        return command("set_object_property", {
            "actor": actor, "asset_path": asset_path, "component": component,
            "property": property, "value": value, "properties": properties,
        })

    # --- Actores ---------------------------------------------------------------

    @mcp.tool()
    def get_actor_components(ctx: Context, actor: str, pie: bool = False) -> Dict[str, Any]:
        """Lista los componentes de un actor con clase, jerarquía y transform relativo."""
        return command("get_actor_components", {"actor": actor, "pie": pie})

    @mcp.tool()
    def get_selected_actors(ctx: Context) -> Dict[str, Any]:
        """Devuelve los actores seleccionados en el editor."""
        return command("get_selected_actors")

    @mcp.tool()
    def set_selection(ctx: Context, actors: List[str], add: bool = False) -> Dict[str, Any]:
        """Selecciona actores en el editor. Con add=False reemplaza la selección; una lista vacía la limpia."""
        return command("set_selection", {"actors": actors, "add": add})

    @mcp.tool()
    def set_actor_label(ctx: Context, actor: str, label: str) -> Dict[str, Any]:
        """Cambia la etiqueta (nombre visible en el Outliner) de un actor."""
        return command("set_actor_label", {"actor": actor, "label": label})

    @mcp.tool()
    def set_actor_folder(ctx: Context, actors: List[str], folder: str = "") -> Dict[str, Any]:
        """Mueve actores a una carpeta del Outliner (p. ej. "Iluminacion/Interior"). Vacío = raíz."""
        return command("set_actor_folder", {"actors": actors, "folder": folder})

    @mcp.tool()
    def set_actor_tags(ctx: Context, actors: List[str], tags: List[str], mode: str = "set") -> Dict[str, Any]:
        """Cambia los tags de actores. mode: "set" (reemplaza), "add" o "remove"."""
        return command("set_actor_tags", {"actors": actors, "tags": tags, "mode": mode})

    @mcp.tool()
    def duplicate_actor(
        ctx: Context, actor: str, new_label: Optional[str] = None, offset: Optional[List[float]] = None
    ) -> Dict[str, Any]:
        """Duplica un actor del nivel, opcionalmente desplazado [x, y, z] y con otra etiqueta."""
        return command("duplicate_actor", {"actor": actor, "new_label": new_label, "offset": offset})

    @mcp.tool()
    def attach_actor(ctx: Context, actor: str, parent: str = "", socket: str = "") -> Dict[str, Any]:
        """Adjunta un actor a otro (parent vacío lo separa), conservando el transform en el mundo."""
        return command("attach_actor", {"actor": actor, "parent": parent, "socket": socket})

    # --- Niveles ---------------------------------------------------------------

    @mcp.tool()
    def get_level_info(ctx: Context) -> Dict[str, Any]:
        """Información del nivel abierto: nombre, paquete, cambios sin guardar, sublevels, GameMode."""
        return command("get_level_info")

    @mcp.tool()
    def list_levels(ctx: Context, path: str = "/Game") -> Dict[str, Any]:
        """Lista los niveles (.umap) bajo una carpeta."""
        return command("list_levels", {"path": path})

    @mcp.tool()
    def open_level(ctx: Context, path: str, discard_changes: bool = False) -> Dict[str, Any]:
        """Abre un nivel. Falla si el actual tiene cambios sin guardar, salvo discard_changes=True."""
        return command("open_level", {"path": path, "discard_changes": discard_changes})

    @mcp.tool()
    def save_level(ctx: Context, all_dirty: bool = False) -> Dict[str, Any]:
        """Guarda el nivel actual. Con all_dirty=True guarda también todos los assets modificados."""
        return command("save_level", {"all_dirty": all_dirty})

    # --- PIE -------------------------------------------------------------------

    @mcp.tool()
    def start_pie(ctx: Context, mode: str = "viewport") -> Dict[str, Any]:
        """Inicia Play In Editor. mode: "viewport", "window", "vr" (VR Preview) o "simulate"."""
        return command("start_pie", {"mode": mode})

    @mcp.tool()
    def stop_pie(ctx: Context) -> Dict[str, Any]:
        """Detiene Play In Editor."""
        return command("stop_pie")

    @mcp.tool()
    def is_pie_running(ctx: Context) -> Dict[str, Any]:
        """Indica si PIE está corriendo."""
        return command("is_pie_running")

    # --- Viewport --------------------------------------------------------------

    @mcp.tool()
    def get_viewport_camera(ctx: Context) -> Dict[str, Any]:
        """Posición, rotación y FOV de la cámara del viewport del editor."""
        return command("get_viewport_camera")

    @mcp.tool()
    def set_viewport_camera(
        ctx: Context,
        location: Optional[List[float]] = None,
        rotation: Optional[List[float]] = None,
        fov: Optional[float] = None,
    ) -> Dict[str, Any]:
        """Mueve la cámara del viewport. rotation es [pitch, yaw, roll]."""
        return command("set_viewport_camera", {"location": location, "rotation": rotation, "fov": fov})

    @mcp.tool()
    def focus_actor(ctx: Context, actors: List[str], select: bool = True) -> Dict[str, Any]:
        """Encuadra uno o varios actores en el viewport (como la tecla F) y opcionalmente los selecciona."""
        return command("focus_actor", {"actors": actors, "select": select})
