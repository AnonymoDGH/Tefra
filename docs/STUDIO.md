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

### Animar propiedades (TweenService)
```csharp
public class Lift : TefraScript
{
    public override void Start()
    {
        var part = (Part)Parent;
        var info = new TweenInfo(0.5f, EasingStyle.Quad, EasingDirection.Out); // Time, Style, Direction, RepeatCount, Reverses, DelayTime
        var tween = TweenService.Create(part, info, new { Position = new Vector3(10, 4, 0), Transparency = 0.5f });
        tween.Completed += state => Debug.Log("lift " + state);
        tween.Play();
    }
}
```
- Metas: un objeto anónimo o un `Dictionary<string, object>` con el nombre de cada propiedad. Tipos animables: `float`, `int`, `Vector2`, `Vector3`, `Color3`. Los valores de partida se leen en `Play()`.
- `Pause()`, `Play()` (reanuda), `Cancel()` (se queda donde está). `PlaybackState`: Begin, Playing, Paused, Completed, Cancelled. `Completed` recibe el estado final.
- `TweenInfo.RepeatCount = -1` repite sin fin; `Reverses` vuelve al valor inicial en cada ciclo; `DelayTime` espera antes de empezar.
- `TweenService.GetValue(alpha, style, direction)` devuelve la curva evaluada en `alpha` (0 a 1).
- Los tweens avanzan en cada paso del motor. Si su instancia se destruye, se paran sin evento. Al pulsar Stop se borran todos.

### Tipos de valor (Roblox)
Código C# puro (`managed/Tefra.Engine/src/MathTypes.cs`), sin llamadas nativas.
- **CFrame**: posición y rotación. `part.CFrame` lee y escribe la posición y la `Orientation` (grados, orden Y-X-Z, igual que el motor). Constructores: `new CFrame(pos)`, `new CFrame(pos, lookAt)`, `CFrame.Angles(rx, ry, rz)` (radianes, orden X-Y-Z, como Roblox), `CFrame.fromEulerAnglesYXZ(rx, ry, rz)` (orden del motor), `CFrame.fromAxisAngle(eje, ángulo)`, `CFrame.lookAt(desde, hacia[, arriba])`, `CFrame.Identity`. Operadores `*` (CFrame·CFrame, CFrame·Vector3) y `+`/`-` con Vector3. Métodos: `Inverse()`, `Lerp(meta, alfa)`, `ToWorldSpace`/`ToObjectSpace`, `PointToWorldSpace`/`PointToObjectSpace`, `VectorToWorldSpace`/`VectorToObjectSpace`, `ToEulerAnglesYXZ()` (radianes). Propiedades: `Position`, `LookVector` (-Z), `RightVector`, `UpVector`, `Rotation`.
- **Modelos**: `model.GetPivot()` (la `PrimaryPart` si existe, si no la primera pieza descendiente) y `model.PivotTo(cf)`, que mueve todas las piezas descendientes de forma rígida. `part.PivotTo(cf)` y `part.GetPivot()` existen también.
- **UDim / UDim2**: `new UDim2(escalaX, desplazX, escalaY, desplazY)`, `UDim2.fromScale(x, y)`, `UDim2.fromOffset(x, y)`, `+`, `-`, `Lerp`. El constructor reemplaza a `UDim2.new` (`new` es palabra reservada en C#).
- **NumberRange**, **NumberSequence** (`new NumberSequence(new NumberSequenceKeypoint(t, valor), ...)` o `(inicio, fin)`) y **ColorSequence** (`new ColorSequence(new ColorSequenceKeypoint(t, color), ...)` o `(inicio, fin)`), con `Evaluate(t)`. Los puntos se ordenan por `t` (en [0, 1]); `t` fuera de rango toma el primer o último valor.
- **Color3**: `FromRGB(0-255)`, `FromHex("#RRGGBB")`, `FromHSV(h, s, v)` (0-1), `ToHSV()` (devuelve un Vector3), `Lerp`.
- **TefraRandom**: `new TefraRandom(semilla)`, `NextNumber()`, `NextNumber(min, max)`, `NextInteger(min, max)` (ambos extremos incluidos), `NextUnitVector()`. Se llama así y no `Random` porque los scripts importan `System`, y `Random` sería ambiguo con `System.Random`.

## Efectos (ParticleEmitter)
Un `ParticleEmitter` bajo una Part emite partículas de verdad (billboards en GPU, ordenadas, *soft particles*, máscara reactiva para el TAA). Se ven también en modo edición.
- `Texture`: `builtin:fire`, `smoke`, `spark`, `star`, `snow`, `ring`, `circle`.
- Emisión: `Rate`, `Lifetime`, `Speed`, `SpreadAngle`, `Variation`, `Shape` (Box/Sphere/Disc/Point), `EmissionDirection`.
- Aspecto: `Size`→`SizeEnd`, `Color`→`ColorEnd`, `Transparency`→`TransparencyEnd`, `LightEmission` (1 = aditivo), `Brightness`, `Stretch`.
- Movimiento: `Acceleration`, `Drag`, `RotSpeed`, `LockedToPart`, `TimeScale`.
- Desde C#: `emitter.Emit(30)` lanza una ráfaga. Límite total según la calidad (Low 4000 … Ultra 40000).

Eventos de `Humanoid` por personaje: `Jumping`, `FreeFalling`, `Landed`, `StateChanged(old, new)`, `Died`, `HealthChanged`.

## IA (MCP)
Studio sirve MCP en `http://127.0.0.1:44755/mcp`. Con Claude Code:
```sh
claude mcp add --transport http tefra http://127.0.0.1:44755/mcp
```
Herramientas disponibles:
- engine_guide (léela primero: qué sabe hacer el motor);
- get_tree, get_instance, list_classes;
- create_instance, create_instances (lote de hasta 5000), set_properties, delete_instance, reparent, clone, find;
- get_script, set_script;
- save_place, load_place;
- play, stop, get_output, capture_viewport, set_camera;
- terrain_height, raycast (medir el terreno y apoyar objetos en el suelo);
- new_place_from_template (meadow, beach, valley, obby...), save_project;
- import_model (glTF/GLB → MeshPart, copiado a `assets/` del proyecto), import_roblox.

Para clientes que solo hablan stdio (Claude Desktop) hay un puente, `tefra_mcp`; ver `docs/research/m3-mcp/REPORT.md`.
