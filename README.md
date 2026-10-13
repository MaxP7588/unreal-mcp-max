# Unreal MCP Max

Conecta **Unreal Engine** con una IA (Claude, Cursor…) mediante el
[Model Context Protocol (MCP)](https://modelcontextprotocol.io). Le describes a la IA lo que quieres
("haz que esta puerta se abra cuando el jugador se acerque") y la IA crea los Blueprints, los nodos,
los actores, los materiales y los widgets directamente en tu editor.

Está pensado para que **diseñadores que no programan** puedan usar la IA para la lógica mientras
ellos se encargan del diseño.

> Basado en [unreal-mcp](https://github.com/chongdashu/unreal-mcp) de **Chong-U (chongdashu)**,
> licencia MIT. Ver [Créditos y licencia](#créditos-y-licencia).

---

## ¿Qué modo elijo?

| | **Modo simple** (estudiantes) | **Modo completo** (avanzado) |
|---|---|---|
| Para quién | Gente que no programa | Gente que sabe lo que hace |
| Compilar C++ / Visual Studio | **No hace falta**: el plugin viene compilado | Sí: Unreal compila el plugin con tu proyecto |
| Versión de Unreal | Solo la del zip que descargas (p. ej. 5.5) | Cualquiera compatible con el código |
| Herramientas | Todas menos las riesgosas | Todas (94) |
| `execute_python` (ejecutar Python en el editor) | Desactivada | Activada |
| `console_command` (comandos de consola) | Desactivada | Activada |
| `delete_asset` (borrar archivos del proyecto, no se deshace) | Desactivada | Activada |

En los dos modos, los cambios en el editor quedan **sin guardar** y se pueden deshacer con **Ctrl+Z**
hasta que guardes.

---

## Modo simple: instalación para estudiantes

### Lo que necesitas

- Windows 10 u 11.
- **Unreal Engine** en la versión del zip (mira el nombre del archivo: `...-UE5.5-...` es Unreal 5.5).
- **[Claude Desktop](https://claude.ai/download)** con una cuenta de Claude (el plan gratuito sirve
  para probar; para usarlo en serio conviene un plan de pago).

### Pasos

1. Ve a la página **[Releases](https://github.com/MaxP7588/unreal-mcp-max/releases/latest)** de este repositorio y descarga el zip de tu versión de
   Unreal (por ejemplo `unreal-mcp-max-v2.0-UE5.5-Win64.zip`).
   **No uses el botón verde "Code"**: ese trae el código sin compilar.
2. Haz clic derecho en el zip → **Extraer todo…** (en Descargas, por ejemplo; **no** dentro de tu proyecto).
3. **Cierra Unreal** si está abierto.
4. Abre la carpeta extraída y haz doble clic en **`Instalar.bat`**.
   - Si Windows muestra "Windows protegió su PC", haz clic en **Más información → Ejecutar de todas formas**.
5. Se abre una ventana para elegir tu proyecto: elige el archivo **`.uproject`** de tu proyecto.
6. Espera a que diga **Listo**. La primera vez descarga Python automáticamente (tarda un par de minutos).
7. **Cierra Claude Desktop por completo** (también el icono junto al reloj) y vuelve a abrirlo.
8. Abre tu proyecto en Unreal.
9. En Claude Desktop escribe: *"lista los actores del nivel"*. Si responde con la lista, ¡funciona!

> Hay que instalarlo **en cada proyecto** donde lo quieras usar (repite desde el paso 4).
> Claude Desktop se conecta siempre al editor que tengas abierto, así que funciona con cualquier
> proyecto que tenga el plugin instalado.

### Ideas para pedirle a la IA

- *"Crea un Blueprint de puerta que se abra suavemente cuando el jugador entre en una caja de colisión."*
- *"Pon 10 cubos en círculo alrededor del punto 0,0,0 con un material rojo."*
- *"Haz un menú de inicio con un botón Jugar que cargue el nivel Nivel1."*
- *"Explícame qué hace el Blueprint BP_Personaje."*
- *"Cuando presione la tecla E cerca de una lámpara, que se encienda y se apague."*
- *"Guarda todo."* (la IA no guarda sola salvo que se lo pidas)

**Consejo:** antes de pedir cambios grandes, haz una copia de tu proyecto (o usa control de versiones).
Si la IA hace algo raro, usa **Ctrl+Z** en Unreal o simplemente no guardes.

---

## Modo completo: instalación avanzada

### Lo que necesitas

- Unreal Engine 5.5 o superior (probado en 5.5).
- **Visual Studio 2022** con la carga de trabajo *Desarrollo de juegos con C++* (para compilar el plugin).
- Un cliente MCP: Claude Desktop, Claude Code, Cursor, Windsurf…

### Opción A: instalador

1. Clona o descarga este repositorio (o el zip de Releases).
2. Cierra Unreal y ejecuta **`Instalar-Completo.bat`**, o desde PowerShell:
   ```powershell
   .\Instalador\instalar.ps1 -Modo completo -Proyecto "C:\Ruta\MiProyecto.uproject"
   ```
3. Abre el proyecto: Unreal pedirá compilar el plugin la primera vez.

### Opción B: a mano

1. Copia la carpeta `UnrealMCP` a `TuProyecto/Plugins/UnrealMCP`.
2. Instala [uv](https://docs.astral.sh/uv/) y las dependencias:
   ```powershell
   uv --directory TuProyecto/Plugins/UnrealMCP/Python sync
   ```
3. Agrega el servidor a tu cliente MCP (`.mcp.json` para Claude Code, `claude_desktop_config.json`
   para Claude Desktop, `.cursor/mcp.json` para Cursor):
   ```json
   {
     "mcpServers": {
       "unrealMCP": {
         "command": "uv",
         "args": ["--directory", "C:/Ruta/TuProyecto/Plugins/UnrealMCP/Python", "run", "unreal_mcp_server.py"],
         "env": { "UNREAL_MCP_MODE": "completo" }
       }
     }
   }
   ```
4. Abre el proyecto y compila.

### Variables de entorno del servidor

| Variable | Por defecto | Uso |
|---|---|---|
| `UNREAL_MCP_MODE` | `completo` | `simple` desactiva `execute_python`, `console_command` y `delete_asset` |
| `UNREAL_MCP_HOST` | `127.0.0.1` | Host del editor |
| `UNREAL_MCP_PORT` | `55557` | Puerto TCP del plugin (también en *Project Settings → Plugins → Unreal MCP*) |
| `UNREAL_MCP_TIMEOUT` | `75` | Segundos de espera por respuesta |

---

## Cómo funciona

```
 Claude / Cursor  ──stdio──>  servidor Python (FastMCP)  ──TCP 127.0.0.1:55557──>  plugin UnrealMCP (editor)
```

- El **plugin** (`UnrealMCP/`, C++) abre un servidor TCP dentro del editor y ejecuta los comandos.
- El **servidor Python** (`UnrealMCP/Python/`) expone las herramientas MCP y las reenvía al editor.
  Se reconecta solo si el editor se reinicia.
- Protocolo: un JSON `{"type": "...", "params": {...}}` por línea; cada respuesta, un JSON por línea.

### Herramientas

| Grupo | Ejemplos |
|---|---|
| Nivel y actores | `get_actors_in_level`, `spawn_actor`, `set_actor_transform`, `duplicate_actor`, `attach_actor`, `start_pie` |
| Propiedades (cualquier objeto) | `get_object_properties`, `set_object_property` |
| Assets | `search_assets`, `create_asset`, `duplicate_asset`, `rename_asset`, `import_asset`, `save_assets` |
| Materiales | `create_material_instance`, `set_material_instance_param`, `set_actor_material` |
| Blueprints (lectura) | `list_blueprints`, `get_blueprint_overview`, `read_blueprint_graph` (pseudocódigo), `find_in_blueprints` |
| Blueprints (edición) | `create_blueprint`, `apply_blueprint_graph`, `add_blueprint_node`, `connect_blueprint_pins`, `compile_blueprint` |
| Input | Enhanced Input: `add_enhanced_input_mapping`, `read_input_asset` |
| UI (UMG) | `create_umg_widget_blueprint`, `add_widget`, `set_widget_property`, `bind_widget_event` |
| Script (solo modo completo) | `execute_python`, `console_command` |

`list_commands` devuelve la lista completa desde el editor. El prompt `info` del servidor explica a la
IA cómo usar las herramientas.

---

## Problemas comunes

| Problema | Solución |
|---|---|
| Claude dice que no puede conectar con Unreal | Abre tu proyecto en Unreal y vuelve a pedirlo. |
| Claude no muestra las herramientas de Unreal | Cierra Claude Desktop por completo (también desde la bandeja) y ábrelo de nuevo. |
| Unreal dice "módulos faltantes, ¿recompilar?" en modo simple | El zip no es de tu versión de Unreal. Descarga el correcto en Releases. |
| El instalador dice que el plugin no está compilado | Descargaste el código (botón "Code"). Descarga el zip de **Releases**. |
| "Windows protegió su PC" | **Más información → Ejecutar de todas formas**. |
| Algo salió mal en el editor | **Ctrl+Z**, o cierra sin guardar. |

Logs del servidor: `TuProyecto/Plugins/UnrealMCP/Python/logs/unreal_mcp.log`. La IA también puede
leer el log del editor con `get_editor_log`.

---

## Para mantenedores: publicar un release

```powershell
.\Herramientas\empaquetar-release.ps1 -Unreal "C:\Program Files\Epic Games\UE_5.5"
```

Genera `dist/unreal-mcp-max-v<versión>-UE5.5-Win64.zip` con el plugin compilado, el servidor Python,
el instalador y este README. Súbelo a un release de GitHub. Repite con cada versión de Unreal que
quieras soportar (`-Unreal "C:\Program Files\Epic Games\UE_5.6"`, etc.).

---

## Créditos y licencia

Este proyecto es una versión ampliada de **[unreal-mcp](https://github.com/chongdashu/unreal-mcp)**,
creado por **Chong-U ([@chongdashu](https://github.com/chongdashu))** y publicado con licencia MIT.

Cambios principales de *unreal-mcp-max*: transporte robusto con reconexión, lectura de Blueprints como
pseudocódigo, edición de grafos completa, propiedades por reflexión sobre cualquier objeto, Enhanced
Input, assets, materiales, UMG, modo simple para estudiantes e instalador.

Distribuido bajo la **licencia MIT** (ver [LICENSE](LICENSE)), que conserva el aviso de copyright del
proyecto original. Puedes usarlo, modificarlo y redistribuirlo libremente, también en clases y
proyectos comerciales, siempre que mantengas el aviso de licencia.

*Unreal Engine es una marca de Epic Games. Este proyecto no está afiliado a Epic Games ni a Anthropic.*
