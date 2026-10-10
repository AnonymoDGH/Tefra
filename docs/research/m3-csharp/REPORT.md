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
- Solo se ejecutan los `Script` (servidor). LocalScript y ModuleScript no tienen ejecución todavía ni `require`.
- No se ha probado en Windows real; solo hay compilación cruzada MinGW.
