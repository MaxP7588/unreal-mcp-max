# Servidor MCP de Unreal MCP Max

Servidor MCP (stdio, FastMCP) que reenvía cada herramienta al plugin `UnrealMCP` del editor por TCP.
La instalación y la configuración de los clientes están en el [README principal](../../README.md).

```powershell
uv sync                              # instalar dependencias
uv run unreal_mcp_server.py          # arrancar a mano (normalmente lo arranca el cliente MCP)
```

- `tools/`: una carpeta de herramientas por toolset; cada módulo expone `register_*_tools(mcp)`.
- `tools/common.py`: `command(nombre, params)` envía un comando al editor y normaliza la respuesta.
- `scripts/`: pruebas que hablan con el editor directamente por TCP, sin cliente MCP.
- `logs/unreal_mcp.log`: log del servidor (con rotación).

Para agregar una herramienta: implementa el comando en C++ (`Source/UnrealMCP/Private/Commands/`),
regístralo en el bridge y expónlo con `@mcp.tool()` en el módulo de `tools/` que corresponda. Si la
herramienta es destructiva o ejecuta código arbitrario, agrégala a `SIMPLE_MODE_BLOCKED_TOOLS` en
`unreal_mcp_server.py`.
