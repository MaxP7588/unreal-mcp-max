"""Assets: búsqueda, info, crear, duplicar, renombrar, borrar, importar, guardar, referencias y log."""

import logging
from typing import Any, Dict, List, Optional

from mcp.server.fastmcp import Context, FastMCP
from mcp.types import ToolAnnotations

from tools.common import CREATE, DESTRUCTIVE, EDIT, READ_ONLY, command

logger = logging.getLogger("UnrealMCP")


def register_asset_tools(mcp: FastMCP):
    @mcp.tool(annotations=READ_ONLY)
    def search_assets(
        ctx: Context,
        query: str = "",
        class_name: Optional[str] = None,
        path: str = "/Game",
        limit: int = 100,
        recursive: bool = True,
    ) -> Dict[str, Any]:
        """Busca assets por nombre (todas las palabras deben aparecer en el nombre o la carpeta).

        Args:
            query: Palabras a buscar, p. ej. "puerta madera".
            class_name: Clase exacta del asset: StaticMesh, Material, MaterialInstanceConstant, Texture2D,
                        Blueprint, WidgetBlueprint, SoundWave, SoundCue, World, DataTable, NiagaraSystem...
            path: Carpeta raíz.
        """
        return command("search_assets", {
            "query": query, "class": class_name, "path": path, "limit": limit, "recursive": recursive,
        })

    @mcp.tool(annotations=READ_ONLY)
    def get_asset_info(
        ctx: Context, asset_path: str, include_properties: bool = False, filter: Optional[str] = None
    ) -> Dict[str, Any]:
        """Clase, tags del AssetRegistry, cantidad de referencias y, opcionalmente, propiedades
        (en Blueprints, los valores por defecto de la clase)."""
        return command("get_asset_info", {"asset_path": asset_path, "include_properties": include_properties, "filter": filter})

    @mcp.tool(annotations=CREATE)
    def create_asset(ctx: Context, class_name: str, name: str, folder: str = "/Game") -> Dict[str, Any]:
        """Crea un asset nuevo usando la factory del editor para esa clase (DataAsset, SoundCue, Curve...).
        Para Blueprints usa create_blueprint; para widgets, create_umg_widget_blueprint. No guarda."""
        return command("create_asset", {"class": class_name, "name": name, "folder": folder})

    @mcp.tool(annotations=CREATE)
    def duplicate_asset(ctx: Context, source_path: str, dest_path: str) -> Dict[str, Any]:
        """Duplica un asset. Falla si dest_path ya existe. No guarda."""
        return command("duplicate_asset", {"source_path": source_path, "dest_path": dest_path})

    @mcp.tool(annotations=EDIT)
    def rename_asset(
        ctx: Context, asset_path: str, new_name: Optional[str] = None, new_path: Optional[str] = None
    ) -> Dict[str, Any]:
        """Renombra o mueve un asset (deja un redirector y actualiza referencias)."""
        return command("rename_asset", {"asset_path": asset_path, "new_name": new_name, "new_path": new_path})

    @mcp.tool(annotations=DESTRUCTIVE)
    def delete_asset(ctx: Context, asset_path: str, force: bool = False) -> Dict[str, Any]:
        """Borra un asset del disco. NO se puede deshacer. Falla si otros assets lo referencian,
        salvo force=True. Confirma con el usuario antes de usarlo."""
        return command("delete_asset", {"asset_path": asset_path, "force": force})

    @mcp.tool(annotations=EDIT)
    def create_folder(ctx: Context, path: str) -> Dict[str, Any]:
        """Crea una carpeta en el Content Browser (debe empezar con /Game)."""
        return command("create_folder", {"path": path})

    @mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=True, idempotentHint=False, openWorldHint=False))
    def import_asset(
        ctx: Context, file: str, destination: str, name: Optional[str] = None, replace: bool = False
    ) -> Dict[str, Any]:
        """Importa un archivo del disco (fbx, png, wav, ...) a una carpeta /Game/... No guarda."""
        return command("import_asset", {"file": file, "destination": destination, "name": name, "replace": replace})

    @mcp.tool(annotations=DESTRUCTIVE)
    def save_assets(ctx: Context, paths: List[str]) -> Dict[str, Any]:
        """Guarda assets modificados. Pasa rutas de paquete."""
        return command("save_assets", {"paths": paths})

    @mcp.tool(annotations=READ_ONLY)
    def find_asset_references(ctx: Context, asset_path: str, reverse: bool = False) -> Dict[str, Any]:
        """Lista las dependencias de un paquete, o quién lo referencia con reverse=True."""
        return command("find_asset_references", {"asset_path": asset_path, "reverse": reverse})

    @mcp.tool(annotations=READ_ONLY)
    def get_editor_log(ctx: Context, filter: str = "", lines: int = 80, errors_only: bool = True) -> Dict[str, Any]:
        """Lee el final del log del editor. Sin filter y con errors_only devuelve solo errores y warnings."""
        return command("get_editor_log", {"filter": filter, "lines": lines, "errors_only": errors_only})

    logger.info("Asset tools registered")
