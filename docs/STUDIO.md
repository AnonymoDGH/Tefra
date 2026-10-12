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
- **Import 3D Model...** (menú File) inserta un modelo como MeshPart, seleccionado, donde mira la cámara. Ver «Importar modelos 3D».

Cada proyecto es una carpeta: `<Nombre>.tefra`, `thumbnail.png` y `assets/` (los archivos que el lugar usa por ruta relativa). `File > Home` vuelve a la página de inicio. Si hay cambios sin guardar, Studio pregunta antes de cerrar o cambiar de lugar. La variable `TEFRA_HOME` cambia la carpeta raíz (instalaciones portátiles).

## Importar modelos 3D
**File > Import 3D Model...** y la herramienta MCP `import_model` aceptan:
- `.glb` y `.gltf`: se usan tal cual (un `.gltf` copia toda su carpeta, con buffers y texturas).
- `.fbx` (binario o ASCII) y `.obj` (con su `.mtl` y texturas): leídos con ufbx (MIT o dominio público, `third_party/ufbx`).
- `.stl` (ASCII y binario) y `.ply` (ASCII y binario little endian): leídos por el propio importador.
- `.dae` (Collada) y `.3mf`: no soportados todavía.

Se convierten a glTF 2.0 binario (`.glb`), con geometría, normales, UV, materiales PBR (color, metalness/roughness, normal map, emisión) y la jerarquía de nodos con sus transformaciones. Las unidades quedan en metros y el eje vertical en +Y. FBX usa sus propias unidades y ejes; OBJ, STL y PLY no las llevan, así que usan `unit_scale` (1 = metros, 0.01 = cm) y, en STL y PLY, se asume Z hacia arriba.

- En un proyecto guardado, el `.glb` va a `assets/<nombre>/<nombre>.glb`. En un lugar sin guardar, a una carpeta de caché de Studio.
- Las texturas van dentro del `.glb` (solo PNG y JPEG). Sin embeber, se escriben junto al `.glb`.
- Límite opcional de triángulos por primitiva (`max_triangles`, simplificación con meshoptimizer).
- Los modelos con esqueleto se importan en su pose de reposo: no se exporta el esqueleto ni las formas (blend shapes).

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
- `Workspace.Raycast(origen, dirección, distancia)` devuelve el primer sólido o `null`. Con `RaycastParams` filtra por instancias (`FilterDescendantsInstances`, `FilterType.Exclude` por defecto o `Include`; filtra también a sus descendientes).
- Consultas de volumen (`OverlapParams`, con `MaxParts`): `GetPartBoundsInRadius(centro, radio)`, `GetPartBoundsInBox(centro, tamaño)` o `GetPartBoundsInBox(centro, orientaciónGrados, tamaño)`, y `GetPartsInPart(pieza)` (no incluye la propia pieza). Devuelven `BasePart[]`. Usan la caja orientada de cada pieza (una `Ball` cuenta como su esfera inscrita en el radio); no hay `CFrame`, así que la orientación va como Vector3 en grados (Y, X, Z).
- Barridos: `Workspace.Spherecast(origen, radio, dirección, params)` y `Workspace.Blockcast(centro, orientaciónGrados, tamaño, dirección, params)`. La longitud de la dirección es la distancia. Son barridos de forma reales de Jolt.
- Física de piezas: `ApplyImpulse(v)` (kg·m/s) y `ApplyAngularImpulse(v)` se aplican en el siguiente paso a piezas sin anclar; `GetMass()` = Density × volumen; `AssemblyAngularVelocity` se puede escribir; `GetTouchingParts()` devuelve las piezas en contacto ahora (incluidos sensores y las piezas que toca un personaje).
- Ejemplo de prueba: `tests/scripting/fixtures/queries.tefra` (`scripting_queries` en CTest).
- Para matar o curar: `humanoid.Health = 0` o `humanoid.TakeDamage(10)`. Una caída fuera del mundo también mata.
- Para plataformas que se mueven: cambia `Position` cada paso desde un script; la pieza se vuelve cinemática y lleva a quien esté encima.
- Un script destruido (o deshabilitado, o con error) deja de correr: sus manejadores de eventos y sus `Task.Wait` pendientes tampoco se ejecutan.
- Limitación: un script con `namespace X;` (con ámbito de archivo) no compila; usa `namespace X { ... }`.

### Animar propiedades (TweenService)
```csharp
public class Lift : TefraScript
### Atributos y etiquetas
Cualquier instancia puede llevar atributos (pares nombre-valor libres, como en Roblox) y etiquetas (CollectionService).
```csharp
public class Breakable : TefraScript
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
### Mensajes, Debris y datos
```csharp
// Bindables: Fire y Invoke llaman a los manejadores en el momento.
var bus = (BindableEvent)Game.Workspace.FindFirstChild("Bus");
bus.Event += args => print("got", args[0]);
bus.Fire("hello");
var sum = (BindableFunction)Game.Workspace.FindFirstChild("Adder");
sum.OnInvoke = args => (int)args[0] + (int)args[1];
int r = (int)sum.Invoke(2, 3);

// Remotes (servidor y cliente comparten proceso): el mensaje llega en el paso siguiente.
var net = (RemoteEvent)Game.Workspace.FindFirstChild("Net");
net.OnServerEvent += (player, args) => print("server", args[0]);
net.OnClientEvent += args => print("client", args[0]);
net.FireServer("ping");          // OnServerEvent (player = Players.LocalPlayer)
net.FireAllClients("news");      // OnClientEvent

Debris.AddItem(part, 2f);        // destruye la pieza a los 2 s de juego

var store = DataStoreService.GetDataStore("PlayerData");
store.SetAsync("coins", 42);
double coins = (double)store.GetAsync("coins");   // los números vuelven como double
store.IncrementAsync("coins", 8);
var board = DataStoreService.GetOrderedDataStore("Scores");
var top = board.GetSortedAsync(false, 10);        // (Key, Value) de mayor a menor

string json = HttpService.JSONEncode(new Dictionary<string, object> { { "a", 1 } });
```
- `RemoteFunction` (`InvokeServer` / `InvokeClient`, con `OnServerInvoke` / `OnClientInvoke`) responde de forma síncrona.
- `FireClient(player, ...)` llega al único cliente del proceso aunque `player` no sea el local.
- `DataStore`: un fichero JSON por almacén en `%APPDATA%/Tefra/datastores` (o `~/.config/Tefra/datastores`); `TEFRA_DATASTORE_DIR` cambia la carpeta. Cada operación lee y escribe el fichero, así que no hay caché entre llamadas. El almacén se identifica solo por su nombre: dos juegos con el mismo nombre comparten el fichero. Las llamadas son síncronas (no hay `await`).
- Al parar Play se olvidan todos los manejadores de estos mensajes y la cola de Debris.
        part.SetAttribute("Health", 3);            // bool, número, string, Vector3 o Color3; null lo borra
        part.AddTag("Breakable");
        part.GetAttributeChangedSignal("Health").Connect(() =>
        {
            if (Convert.ToInt32(part.GetAttribute("Health")) <= 0) part.Destroy();
        });
        foreach (var other in CollectionService.GetTagged("Breakable")) { /* ... */ }
        CollectionService.GetInstanceAddedSignal("Breakable").Connect(i => Debug.Log("nuevo: " + i.Name));
    }
}
```
- `GetAttribute` devuelve `double` para cualquier número (usa `Convert.ToInt32` para enteros); `SetAttribute` guarda `int`/`long` como entero y `float`/`double` como real.
- `AttributeChanged` y `GetAttributeChangedSignal(nombre)`, igual que `Changed`, llegan en el paso siguiente del motor, no dentro de `SetAttribute`.
- `CollectionService.GetTagged(etiqueta)` devuelve las instancias conectadas al DataModel. `GetInstanceAddedSignal` / `GetInstanceRemovedSignal` avisan por etiqueta. Al destruir una instancia no se emite `InstanceRemoved`.
- Persistencia: se guardan en `.tefra` como `"attributes": {...}` y `"tags": [...]` por instancia (opcionales). Un número entero en un atributo se recarga como entero; un Color3 se guarda como `{"Color3": [r, g, b]}`.
- Los cambios de atributos y etiquetas no entran en el historial de deshacer (Ctrl+Z) de Studio. El panel Properties los muestra en solo lectura.
- MCP: `get_instance` devuelve `attributes` y `tags`; `set_properties` acepta `attributes` (se fusionan, `null` borra) y `tags` (reemplaza la lista).

## Importar lugares de Roblox (ADR-017)
Solo para lugares propios. `Import Roblox Place...` (o `tefra_import in.rbxlx out.tefra [--report r.json]`) convierte
`.rbxl`, `.rbxlx`, `.rbxm` y `.rbxmx`. Qué se trae y qué no está en `docs/research/rbx-import/REPORT.md`.

- **Scripts**: `Script`, `LocalScript` y `ModuleScript` conservan el Luau en `LuauSource`. `Source` es un esqueleto C#
  (`public class <Nombre> : TefraScript` con `Start()` vacío) con el Luau original como comentario. Se importan
  desactivados (`Enabled = false`). Solo queda traducir el Luau a C#. Si dos scripts se llaman igual, el esqueleto
  lleva el índice del fichero en el nombre de la clase.
- **Clases sin equivalente**: se crea una `Folder` con el mismo nombre, el atributo `RobloxClass` (clase original) y
  cada propiedad como `rbx.<Propiedad>` (números, booleanos, cadenas, `Vector3`, `Color3`; el resto como texto JSON).
  Los hijos se conservan. Los `rbx.*` son valores crudos de Roblox (studs), sin convertir.
- **Atributos y etiquetas** de Roblox pasan a atributos y etiquetas de Tefra.
- **Valores, decals, attachments, welds, equipos y GUI**: `StringValue`, `IntValue`, `NumberValue`, `BoolValue`,
  `Vector3Value`, `Color3Value`, `ObjectValue`, `CFrameValue`, `Decal`, `Texture`, `Attachment`, `Weld`,
  `WeldConstraint`, `Team`, `SurfaceGui`, `BillboardGui` y los modificadores `UIStroke`, `UIPadding`, `UIListLayout`...
- **Efectos de Lighting** (`Atmosphere`, `Sky`, `BloomEffect`...): sus propiedades son atributos `rbx.<Clase>.<Prop>`
  de `Lighting`.
- **Assets**: si `<lugar>_assets/<id>.glb` (o `.ogg`, `.png`...) existe junto al lugar, `MeshId` y `SoundId` apuntan a
  ese fichero. Si no, se conserva el texto `rbxassetid://`. Studio no descarga nada.
- **Terreno voxel** (SmoothGrid): sin convertir; queda una `Folder` `Terrain` con sus ajustes en `rbx.*`.
- El informe lista contadores por clase y cada clase convertida a marcador de posición.

## Efectos (ParticleEmitter)
Un `ParticleEmitter` bajo una Part emite partículas de verdad (billboards en GPU, ordenadas, *soft particles*, máscara reactiva para el TAA). Se ven también en modo edición.
- `Texture`: `builtin:fire`, `smoke`, `spark`, `star`, `snow`, `ring`, `circle`.
- Emisión: `Rate`, `Lifetime`, `Speed`, `SpreadAngle`, `Variation`, `Shape` (Box/Sphere/Disc/Point), `EmissionDirection`.
- Aspecto: `Size`→`SizeEnd`, `Color`→`ColorEnd`, `Transparency`→`TransparencyEnd`, `LightEmission` (1 = aditivo), `Brightness`, `Stretch`.
- Movimiento: `Acceleration`, `Drag`, `RotSpeed`, `LockedToPart`, `TimeScale`.
- Desde C#: `emitter.Emit(30)` lanza una ráfaga. Límite total según la calidad (Low 4000 … Ultra 40000).

Eventos de `Humanoid` por personaje: `Jumping`, `FreeFalling`, `Landed`, `StateChanged(old, new)`, `Died`, `HealthChanged`.

## Cielo (Lighting, Sky, Atmosphere, Clouds)
Dentro de `Lighting` (Insertar → Environment): `Sky`, `Atmosphere` y `Clouds`. Sin ellos, el cielo es el de siempre; `ClockTime` y `GeographicLatitude` mueven el sol.
- `Sky`: `SkyboxMode` = `Procedural` (atmósfera física), `Cubemap` (`SkyboxFt/Bk/Lf/Rt/Up/Dn`, seis imágenes; `Ft` es -Z, `Rt` es +X) o `HDRI` (`HdriPath`, un `.hdr` equirectangular que también ilumina la escena). `StarCount`, `StarBrightness`, `SunAngularSize`, `MoonVisible`, `MoonPhase` (0 nueva, 0.5 llena), `MoonAngularSize`, `MoonBrightness`, `CelestialBodiesShown`.
- `Atmosphere`: `Haze` (densidad del aerosol, 6 = valle brumoso), `Decay` (tinte de la dispersión), `Density` (niebla extra), `Offset` (altura de referencia de la niebla), `Color` (tinte de la niebla), `Glare` (halo del sol).
- `Clouds`: `Cover`, `Density`, `Color`, `Altitude`, `Size`, `WindSpeed`, `WindDirection` (grados), `Enabled`. Sustituye a `Lighting.CloudCover`.
- De noche el sol baja del horizonte: la Luna es la luz direccional (sombras incluidas), salen las estrellas y la exposición baja sola.
- Coste: la atmósfera solo se recalcula al cambiar sus propiedades; la luz de imagen (IBL) se rehace al cambiar el fondo o cuando el sol se mueve más de medio grado en modo Procedural. Los cielos Cubemap/HDRI no dibujan nubes ni estrellas (las nubes sobre el HDRI sí).
- Limitación: el cubemap se convierte a un equirectangular de 2048 px al cargarlo (fondo e IBL); no hay cubemap nativo. `SunTextureSize` no existe.

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
- import_model (glTF/GLB, FBX, OBJ, STL o PLY → MeshPart; los modelos se convierten a GLB en `assets/`; opciones `unit_scale` y `max_triangles`), import_roblox.

Para clientes que solo hablan stdio (Claude Desktop) hay un puente, `tefra_mcp`; ver `docs/research/m3-mcp/REPORT.md`.
