# Tefra Studio: guía rápida

## Arrancar
- **Windows**: descarga el artifact `tefra-windows` de la última ejecución de CI (GitHub → Actions) y ejecuta `tefra_studio.exe`.
  - Para los scripts C# hace falta el **.NET 8 SDK** (https://dotnet.microsoft.com/download).
- **Linux**:
  ```sh
  cmake --preset linux-release && cmake --build --preset linux-release
  ./build/linux-release/bin/tefra_studio
  ```

Opciones de línea de comandos:

| Opción | Efecto |
|---|---|
| `mi_lugar.tefra` | Abre ese lugar |
| `--demo` | Carga la escena de ejemplo |
| `--play` | Pulsa Play al arrancar |
| `--no-mcp` / `--mcp-port N` | Desactiva el servidor MCP o cambia su puerto |
| `--validation` | Activa la validación Vulkan |

## Interfaz
- **Explorer** (arriba a la derecha): el árbol del lugar.
  - Clic selecciona; Ctrl+clic añade a la selección.
  - Arrastra para cambiar el padre.
  - F2 renombra; doble clic enfoca, o abre el editor si es un script.
  - Clic derecho: Insert Object, Duplicate, Group, Delete.
- **Properties** (abajo a la derecha): todas las propiedades de la selección, agrupadas. Las referencias se asignan arrastrando una instancia desde el Explorer.
- **Viewport**:
  - Mantén el botón derecho y usa WASD/QE para volar (Shift va más rápido); la rueda acerca y aleja.
  - F enfoca la selección.
  - Clic selecciona; Alt+clic selecciona la pieza dentro de un Model.
  - Herramientas: 1 seleccionar, 2 mover, 3 escalar, 4 rotar. El snap se configura en View.
- **Output**: log del motor, prints y errores de los scripts.
- **Script editor**: doble clic en un Script.

Atajos: Ctrl+Z / Ctrl+Y deshacer y rehacer, Ctrl+D duplicar, Ctrl+G agrupar, Supr borrar, Ctrl+S guardar, Ctrl+O abrir, F5 Play/Stop.

## Modo Play
Al pulsar Play:
1. Se guarda una copia del lugar.
2. Se compilan los `Script` C#.
3. Arranca la física (Jolt).
4. Tu avatar aparece en el `SpawnLocation`.

Controles: WASD para andar, Espacio para saltar, botón derecho para orbitar la cámara y rueda para el zoom. Al pulsar Stop, el lugar vuelve exactamente a como estaba.

## Scripts C#
```csharp
using Tefra;

public class Spinner : TefraScript
{
    public override void Update(float dt)
    {
        var part = (Part)Parent;
        part.Orientation += new Vector3(0, 90 * dt, 0);
    }
}
```
API completa: `docs/research/m3-csharp/REPORT.md`. Incluye `Instance.New`, `FindFirstChild`, `Game.Workspace`, `RunService.Heartbeat`, `await Task.Wait(s)`, `Debug.Log`…

## IA (MCP)
Studio sirve MCP en `http://127.0.0.1:44755/mcp`. Con Claude Code:
```sh
claude mcp add --transport http tefra http://127.0.0.1:44755/mcp
```
Herramientas disponibles:
- get_tree, get_instance, list_classes;
- create_instance, set_properties, delete_instance, reparent, clone, find;
- get_script, set_script;
- save_place, load_place;
- play, stop, get_output, capture_viewport, set_camera.

Para clientes que solo hablan stdio (Claude Desktop) hay un puente, `tefra_mcp`; ver `docs/research/m3-mcp/REPORT.md`.
