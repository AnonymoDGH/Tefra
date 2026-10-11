# M3: servidor MCP integrado (ADR-015)

Estado: implementado y probado en Linux. La compilación cruzada MinGW (`-Werror`, ws2_32) es limpia. Falta probarlo en Windows real.

## Conexión
Escucha en `http://127.0.0.1:44755/mcp`, solo en loopback.
- **Claude Code**: `claude mcp add --transport http tefra http://127.0.0.1:44755/mcp`
- **Claude Desktop** (solo habla stdio): usa el puente `tefra_mcp`:
  ```json
  {"mcpServers": {"tefra": {"command": "C:\\Tefra\\tefra_mcp.exe", "args": ["--url", "http://127.0.0.1:44755/mcp"]}}}
  ```

## Herramientas del mundo (`register_world_tools`)

| Herramienta | Qué hace |
|---|---|
| `get_tree` | Devuelve el árbol del lugar |
| `get_instance` | Devuelve una instancia con todas sus propiedades |
| `list_classes` | Lista las clases y sus propiedades |
| `create_instance` | Crea una instancia; todo o nada |
| `set_properties` | Cambia propiedades; todo o nada |
| `delete_instance` | Borra una instancia |
| `reparent` | Cambia el padre de una instancia |
| `clone` | Clona una instancia |
| `find` | Busca por nombre o clase |
| `get_script` / `set_script` | Lee o escribe el código de un script |
| `save_place` / `load_place` | Guarda o carga el lugar |

Studio añade `play`, `stop`, `get_output` y `capture_viewport`.

Formato de valores:
- Vector3 y Color3 se pasan como `[x, y, z]`.
- Los enum, por nombre (`"Metal"`).
- Las referencias, por ruta o id.
- Las rutas tienen la forma `Workspace.Model.Part`.

## Arquitectura
- `engine/mcp` contiene:
  - el parser HTTP/1.1: Content-Length, chunked, keep-alive y 100-continue;
  - sockets POSIX y Winsock2;
  - JSON-RPC 2.0;
  - MCP 2025-06-18 con transporte Streamable HTTP: sesiones `Mcp-Session-Id`, Origin validado, GET 405.
- Las herramientas **solo se ejecutan en `Server::poll()`**, en el hilo principal del editor, porque el DataModel no es thread-safe. Los hilos HTTP esperan con un timeout de 15 s.
- `apps/mcp_bridge` produce `tefra_mcp` (puente stdio↔HTTP). `apps/mcp_demo` es un servidor headless.

## Pruebas
- `tests/unit/test_mcp.cpp`: 16 casos.
- `tests/mcp/test_mcp_demo.py`: 51 comprobaciones de extremo a extremo, puente incluido.
- Sin errores con ASan + UBSan.

## Límites y siguientes pasos
- Sin autenticación: está limitado a localhost; falta un token o una confirmación en Studio.
- Sin SSE: el servidor no empuja notificaciones al cliente.
- La validación de esquemas solo cubre el primer nivel.
