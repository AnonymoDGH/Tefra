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
| (nada) | Abre la página de inicio |
| `mi_lugar.tefra` | Abre ese lugar |
| `--template ID` | Lugar nuevo sin guardar desde una plantilla (`baseplate`, `classic_baseplate`, `flat_terrain`, `obby`, `beach`, `valley`) |
| `--demo` | Carga la escena de ejemplo |
| `--capture out.png --frames N [--capture-viewport]` | Captura la ventana (o solo el viewport 3D) y sale |
| `--play` | Pulsa Play al arrancar |
| `--no-mcp` / `--mcp-port N` | Desactiva el servidor MCP o cambia su puerto |
| `--validation` | Activa la validación Vulkan |

## Página de inicio
Al abrir Studio sin argumentos aparece la página de inicio, al estilo de Roblox Studio:
- **New Experience** y **Templates**: crean una experiencia nueva desde una plantilla y la guardan sola en `Documentos/Tefra/Projects/<Nombre>/`.
  - Plantillas: Baseplate, Classic Baseplate, Flat Terrain, **Obby** (juego completo con checkpoints, kill bricks, plataformas móviles, barra giratoria, HUD con cronómetro, botón Reset y sonidos), Beach y Valley.
- **Home**: experiencias recientes con miniatura (se captura del viewport al guardar), nombre y fecha de edición.
  - El menú **⋮** de cada tarjeta ofrece Open, Rename, Duplicate, Show in Folder, Archive y Delete.
- **My Experiences**: todas, con búsqueda y orden por fecha o nombre.
- **Archive**: experiencias archivadas (`Documentos/Tefra/Archive`), con Restore.
- **Open File...** abre cualquier `.tefra`; **Import Roblox Place...** importa un `.rbxl`/`.rbxlx` propio (ADR-017).

Cada proyecto es una carpeta: `<Nombre>.tefra`, `thumbnail.png` y `assets/` (los archivos que el lugar usa por ruta relativa). `File > Home` vuelve a la página de inicio. Si hay cambios sin guardar, Studio pregunta antes de cerrar o cambiar de lugar. La variable `TEFRA_HOME` cambia la carpeta raíz (instalaciones portátiles).

## Exportar un juego
`File > Export Game...` crea una carpeta autocontenida que se puede copiar a otro PC:
- `<Juego>.exe`: el `tefra_player` renombrado;
- `game.json` (título, ventana, pantalla completa) y `game.tefra`;
- `shaders/`, `resources/`, los assets referenciados y, si el lugar tiene scripts, `managed/`.

El juego exportado necesita una GPU con Vulkan 1.2. Si tiene scripts C#, también el runtime de .NET 8.

En el player, Esc abre el menú (Resume, Reset Character, Leave Game). `tefra_player carpeta|lugar.tefra [--fullscreen]` también ejecuta un lugar directamente.

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

Atajos: Ctrl+Z / Ctrl+Y deshacer y rehacer, Ctrl+D duplicar, Ctrl+G agrupar, Supr borrar, Ctrl+S guardar, Ctrl+Shift+S guardar como, Ctrl+O abrir, F5 Play/Stop.

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

### Dónde corre cada script
- **Script**: dentro de `Workspace` o `ServerScriptService`. Un script creado durante Play (con `Instance.New`) arranca en el paso siguiente.
- **LocalScript**: dentro de un `Player` (`PlayerGui`, `PlayerScripts`) o dentro del personaje de un jugador. Las copias de `StarterGui`, `StarterPlayer` y `StarterCharacterScripts` se crean al aparecer el personaje; las plantillas no corren.
- **ModuleScript**: código compartido. Sus `public` y `static` están disponibles en todos los scripts de la partida (`ObbyConfig.Stages`). Se compila al empezar Play; un módulo nuevo se ve en el siguiente Play.

### Eventos y entrada
```csharp
public class Checkpoint : TefraScript
{
    public override void Start()
    {
        var part = (SpawnLocation)Parent;
        part.Touched += other =>
        {
            var player = Players.GetPlayerFromCharacter(other.Parent);
            if (player != null) player.RespawnLocation = part;
        };
    }
}

public class Hud : TefraScript   // LocalScript dentro de un ScreenGui de StarterGui
{
    public override void Start()
    {
        var reset = Script.Parent.FindFirstChild("Reset") as TextButton;
        reset.Activated += () =>
        {
            var h = Players.LocalPlayer.Character?.FindFirstChildOfClass("Humanoid") as Humanoid;
            if (h != null) h.Health = 0;
        };
        UserInputService.InputBegan += io => { if (io.KeyCode == KeyCode.E) Debug.Log("E"); };
    }
}
```
- Señales: `Touched`/`TouchEnded` (piezas y personajes; un sensor con `CanCollide = false` también las recibe), `Activated`/`MouseButton1Click` (botones), `Died`/`HealthChanged` (Humanoid), `CharacterAdded`/`CharacterRemoving` (Player), `Ended` (Sound).
- `Workspace.Raycast(origen, dirección, distancia)` devuelve el primer sólido o `null`.
- Para matar o curar: `humanoid.Health = 0` o `humanoid.TakeDamage(10)`. Una caída fuera del mundo también mata.
- Para plataformas que se mueven: cambia `Position` cada paso desde un script; la pieza se vuelve cinemática y lleva a quien esté encima.
- Un script destruido (o deshabilitado, o con error) deja de correr: sus manejadores de eventos y sus `Task.Wait` pendientes tampoco se ejecutan.
- Limitación: un script con `namespace X;` (con ámbito de archivo) no compila; usa `namespace X { ... }`.

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
