# Brief común para subagentes: M3 Tefra Studio

Trabajas en **Tefra**, un motor 3D open source (C++20, Vulkan 1.2). El objetivo de M3 es convertirlo en una herramienta para crear juegos completos, al estilo de Roblox Studio. Lee primero `docs/specs/2026-10-10-m3-studio.md`: los ADR-011 a ADR-016 son vinculantes.

Trabajas en tu **propio worktree** de git. **NO hagas push.** Haz commit en tu rama al terminar. Si tu worktree solo contiene un README, haz primero `git reset --hard claude/new-session-ksgl0m` (solo en TU rama de worktree).

## Reglas innegociables
- Clean-room: nada de código, assets ni formatos de Roblox, Rockstar u otros productos comerciales. Inspirarse en la UX está bien; copiar código o assets no.
- Dependencias de terceros: solo licencias permisivas (MIT, BSD, zlib, Apache-2.0). Van vendored en `third_party/<nombre>/` con su LICENSE, se registran en `third_party/CMakeLists.txt` y se documentan en `THIRD_PARTY.md` (versión, licencia, uso). Nada de FetchContent en tiempo de build: el código se copia al repo.
- Motor C++: sin excepciones ni RTTI (`-fno-exceptions -fno-rtti`), `-Werror`, `-Wconversion`. El lado C# (gestionado) sí puede usar excepciones, pero nunca deben cruzar la frontera nativa.
- Vulkan: 0 errores de validación (`--validation`).
- No uses `-march=native`.
- Formato: `clang-format -i` en los archivos C++ que toques (hay `.clang-format`).
- Compila con `cmake --preset linux-release && cmake --build --preset linux-release -j2`. Hay 4 vCPU compartidas por varios agentes, así que usa `-j2`.
- Tests: `ctest --preset linux-release`. Añade tests unitarios (doctest; mira `tests/unit/test_world.cpp` y `tests/CMakeLists.txt`) para la lógica que no necesita GPU.
- `export CCACHE_BASEDIR=/` y `ln -s /home/user/Tefra/assets assets` (assets CC0 descargados, no versionados).

## API base que debes usar (ya existe, en `engine/world`)
- `tefra::world::DataModel` (`tefra/world/datamodel.h`): árbol de instancias con propiedades reflejadas.
  - Servicios: Workspace, Lighting, ReplicatedStorage, ServerScriptService, StarterPlayer y StarterGui.
  - `create(class, parent, name)`, `destroy`, `set_parent`, `clone`.
  - `find(id)`, `find_path("Workspace.Model.Part")`.
  - `to_json`/`from_json`, `save`/`load` (formato `.tefra`).
  - `add_listener(WorldListener{added, removing, changed, reparented})`.
- `Instance`: `id()`, `class_name()`, `is_a("BasePart")`, `name()`, `parent()`, `children()`, `descendants()`.
  - `get(prop)` / `set(prop, Value)` por nombre o índice global. El índice 0 es siempre Name.
  - `get_vec3/get_float/get_bool/get_string`.
- `Value`: tipos `Bool, Int, Float, String, Vector3, Color3, Enum, Ref`. Constructores `Value::number(...)`, `Value::vector3(...)`, etc.
- `ClassRegistry::get()`: `all()`, `find(name)`. `ClassInfo`: `props`, `base`, `creatable`, `service`, `category`, `doc`. `PropInfo`: `name`, `type`, `def`, `enum_info`, `flags`, `category`, `doc`.
- Clases integradas: lista completa en `engine/world/src/classes.cpp`. Entre ellas:
  - Part (con Shape), MeshPart, SpawnLocation, Model, Folder.
  - Terrain, Ocean, PointLight y SpotLight.
  - Script, LocalScript y ModuleScript (propiedad Source, C#).
  - Humanoid, Animator, Animation, Motor6D y Camera.
- Unidades: metros y segundos. Orientation en grados, en orden Y, luego X, luego Z: `orientation_to_quat()` / `quat_to_orientation()`.
- `tefra::world::SceneSync` (`tefra/world/scene_sync.h`): convierte los BaseParts del Workspace en instancias de `render::Scene`, con un raycast de selección y `bounds()`.
- `tefra::JsonValue` (`tefra/core/json_value.h`): DOM JSON con parse y dump.
- El renderer (`engine/renderer`) y el runner (`apps/common/runner.cpp`) muestran cómo crear el dispositivo, la escena y el render a un offscreen o una ventana (`tefra_viewer`). `tefra_bench --capture` escribe un PNG.

## Entregables
1. Código integrado (módulo con API clara y documentada en el header) y tests.
2. `docs/research/m3-<tema>/REPORT.md`: decisiones, API, cómo probarlo, limitaciones y siguientes pasos. Si no puedes escribir el `.md`, pon el contenido en tu respuesta final.
3. Commit en tu rama. Responde con: rama, hash, resumen de 5 líneas, cómo probarlo y resultado de los tests y la validación.

Un resultado pequeño y sólido vale más que uno grande y roto. No toques módulos de otros agentes salvo lo mínimo para integrarte (CMake), para evitar conflictos.
