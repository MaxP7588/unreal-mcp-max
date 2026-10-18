"""Python y comandos de consola en el editor: la salida de emergencia para lo que no tiene herramienta."""

from typing import Any, Dict

from mcp.server.fastmcp import Context, FastMCP
from mcp.types import ToolAnnotations

from tools.common import READ_ONLY, command


def register_script_tools(mcp: FastMCP):
    @mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=True, idempotentHint=False, openWorldHint=True))
    def execute_python(ctx: Context, code: str, mode: str = "file", undoable: bool = False) -> Dict[str, Any]:
        """Ejecuta Python dentro del editor (módulo `unreal`) y devuelve lo que imprime.

        Úsalo para lo que no tenga herramienta propia. Ejemplos:
            import unreal
            print(unreal.EditorLevelLibrary.get_all_level_actors())

        Args:
            code: Código Python. Con mode="file" puede tener varias líneas; usa print() para devolver datos.
            mode: "file" (script de varias líneas) o "eval" (una expresión; devuelve su valor en `result`).
            undoable: Envuelve la ejecución en una transacción de Ctrl+Z. No lo uses si el script abre niveles.
        """
        return command("execute_python", {"code": code, "mode": mode, "undoable": undoable})

    @mcp.tool(annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=True, idempotentHint=False, openWorldHint=False))
    def console_command(ctx: Context, command_line: str, pie: bool = False) -> Dict[str, Any]:
        """Ejecuta un comando de consola del editor (p. ej. "stat fps", "r.ScreenPercentage 50", "obj list").

        Args:
            command_line: El comando tal cual se escribiría en la consola.
            pie: Ejecutarlo en el mundo de PIE si está corriendo.
        """
        return command("console_command", {"command": command_line, "pie": pie})

    @mcp.tool(annotations=READ_ONLY)
    def list_commands(ctx: Context) -> Dict[str, Any]:
        """Lista todos los comandos que expone el plugin del editor, agrupados por toolset."""
        return command("list_commands")
