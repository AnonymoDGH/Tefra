# M3: scripting C# con .NET 8 (ADR-013)

## Partes

| Parte | Ruta / contenido |
|---|---|
| Host nativo | `engine/scripting`: `ScriptHost` con `init/start/step/stop` y callback de log |
| Interop | `abi.h`, ABI 1, solo tipos C planos: `TefraValue` + tabla nativa de 19 funciones + tabla gestionada (compile/start/step/dispatch/stop) |
| Gestionado | `managed/Tefra.Engine` (net8.0), compila en memoria con Roslyn 4.8.0 |
| Runner | `apps/run` (`tefra_run --place x.tefra --frames N --dt 0.016 --out result.tefra`) |
| Tests | `tests/unit/test_scripting.cpp` y ctest `scripting_run` |

Detalles del host:
- Localiza hostfxr en tiempo de ejecución con `dlopen`/`LoadLibraryW`, sin nethost.
- Busca en `DOTNET_ROOT` y en las rutas típicas (`/usr/lib/dotnet`, `C:/Program Files/dotnet`…).
- Si .NET no está, `init()` devuelve false y el motor sigue sin scripts.

## API (namespace `Tefra`)
- `Instance`:
  - Name, Parent, ClassName, FullName;
  - FindFirstChild, FindFirstChildOfClass, GetChildren, GetDescendants;
  - IsA, IsDescendantOf, Clone, Destroy, `Instance.New(cls, parent)`;
  - indexador `this["Prop"]`;
  - eventos `Changed(string)` y `Destroying()`.
- `BasePart`/`Part`: Position, Orientation, Size, Color, Material, Transparency, Anchored, CanCollide, AssemblyLinearVelocity…
- Clases tipadas: MeshPart, SpawnLocation, Model, Folder, Workspace, Lighting y scripts.
- `Vector3` y `Color3` con operadores, Magnitude, Unit, Dot, Cross, Lerp y FromRGB.
- `Game.Workspace` y `Game.GetService("Lighting")`.
- `RunService.Heartbeat` (`Action<float>`).
- `Task.Wait(s)` / `Task.Delay(s)`: awaitable sobre el bucle del motor, sin hilos.
- `Debug.Log/LogWarning/LogError` y `print(...)`.

### API de juego (M3b, ABI 2)
- `Workspace` (dentro de un `TefraScript`, o `Game.Workspace`): `Raycast(origin, direction, maxDistance = 1000)` devuelve `RaycastResult` (`Instance`, `Position`, `Normal`, `Distance`) o `null`. Ignora sensores.
- `Players` (estática): `LocalPlayer`, `GetPlayerFromCharacter(model)`, `GetPlayers()`. El servicio es `Game.Players` (`PlayersService`, con `RespawnTime`).
- `Player`: `Character`, `RespawnLocation`, `DisplayName`, `UserId`; eventos `CharacterAdded(Instance)` y `CharacterRemoving(Instance)`.
- `Humanoid`: `Health`, `MaxHealth`, `WalkSpeed`, `JumpHeight`, `HipHeight`, `State` (solo lectura), `MoveDirection`; `TakeDamage(float)`; eventos `Died()` y `HealthChanged(float)`.
- Física: `BasePart.Touched(Instance)` y `TouchEnded(Instance)`. El personaje reporta sus contactos con `HumanoidRootPart` como origen; los sensores (`CanCollide = false`, `CanTouch = true`) también tocan.
- `UserInputService`: `IsKeyDown(KeyCode)`, `IsMouseButtonPressed(UserInputType)`, `MouseLocation` (`Vector2`), eventos `InputBegan(InputObject)` / `InputEnded(InputObject)`. `KeyCode` tiene los mismos nombres que `tefra::Key`.
- `RunService.Heartbeat(float)`; `Task.Wait(s)` / `Task.Delay(s)`.
- Interfaz: `ScreenGui`, `Frame`, `TextLabel`, `TextButton` (`Activated`, `MouseButton1Click`, `MouseEnter`, `MouseLeave`), `ImageLabel`, `ImageButton`; `Position` y `Size` como `Vector2`.
- Sonido: `Sound` (`Play()`, `Stop()`, `IsPlaying`, `Looped`, `Volume`, evento `Ended`), `SoundService`.
- Instancias: `Clone(parent)`, `Destroy()`, `Instance.New(cls, parent)`.

### Ejecución por instancia
- Cada `Script` / `LocalScript` tiene su propio objeto `TefraScript`. Los textos idénticos comparten una clase compilada; los distintos se compilan cada uno en su propio namespace (`TefraS<n>`), así que dos scripts pueden declarar `class KillBrick`.
- Un script destruido, deshabilitado o editado deja de ejecutarse y sus manejadores (`Touched`, `Heartbeat`, `InputBegan`, `Changed`...) y sus `Task.Wait` pendientes no vuelven a correr.
- Los `ModuleScript` se compilan con el primer lote; sus tipos y miembros estáticos se comparten con todos los scripts del Play (un único ensamblado). Los módulos añadidos durante Play no son visibles hasta el siguiente Play.
- Un script que se crea o clona durante Play arranca en el paso siguiente y recibe las señales de su propia vida (por ejemplo, un HUD clonado al reaparecer recibe `CharacterAdded`).

## Cómo escribir un script
```csharp
public class Spinner : TefraScript {
    public override void Update(float dt) {
        var p = (Part)Parent;
        p.Orientation += new Vector3(0, 90 * dt, 0);
    }
}
public class Waiter : TefraScript {
    public override async void Start() {
        await Task.Wait(0.5f);   // se reanuda en el bucle del motor
        Parent.Name = "Lamp_ready";
    }
}
```

## Ciclo de vida
- Cada Play compila todos los scripts en un ensamblado (PDB portable, para tener líneas en los stack traces) dentro de un `AssemblyLoadContext` coleccionable, que se descarga en Stop.
- Los errores de compilación van al Output con script, línea y columna.
- Las excepciones se registran con su stack trace, desactivan ese script y nunca cruzan a C++.
- Los eventos (`Changed`, `Destroying`) se encolan y se entregan al final de cada paso.

## Build y requisitos
- El target `tefra_managed` ejecuta `dotnet build` cuando `TEFRA_MANAGED=ON` (por defecto) y `dotnet` está en el PATH.
- En el PC del usuario hace falta el **.NET 8 SDK**.
- Roslyn llega por NuGet (MIT) y no está vendorizado; está documentado en THIRD_PARTY.md.

## Limitaciones
- No hay `require`: los módulos se comparten por ensamblado, no por importación.
- Un script con `namespace X;` (namespace con ámbito de archivo) no compila: el envoltorio usa un bloque de namespace.
- Sin ragdoll al morir (opcional, no implementado).
- El coste de un `CollideShape` por personaje y paso, y del despacho de señales, no está medido en la HD 520.
- No se ha probado en Windows real; solo hay compilación cruzada MinGW.
