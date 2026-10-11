# Brief común para subagentes: M3b (juegos completos)

M3b convierte Tefra Studio en una herramienta para publicar juegos. Cubre:
- página de inicio con plantillas;
- importador de lugares de Roblox (`.rbxl` / `.rbxlx`) del propio usuario;
- runtime compartido entre Studio y un player independiente, con exportación del juego;
- UI dentro del juego, sonido, skinning glTF;
- LocalScript/ModuleScript y paisaje en Studio.

Lee antes:
- `docs/research/M3_AGENT_BRIEF.md`: reglas innegociables, compilación y API del DataModel. Siguen vigentes.
- `docs/specs/2026-10-10-m3-studio.md`: los ADR-011 a ADR-017 son vinculantes.

Trabajas en tu **propio worktree**. **Sin push.** Haz commit en tu rama al terminar. Si el worktree solo trae un README, ejecuta `git reset --hard claude/new-session-ksgl0m` en TU rama.

Compila con `-j2`, porque varios agentes comparten 4 vCPU. Usa `export CCACHE_BASEDIR=/`.

## Contratos ya en el repo (commit fca551d). Úsalos, no los reinventes
- **`tefra/core/input.h`**: `InputState` con:
  - teclas (`key(Key::W)`, `key_pressed`) y nombres Enum.KeyCode con `key_name` / `key_from_name`;
  - ratón: `mouse_pos` relativo a la vista, `mouse_delta`, `wheel`, `mouse_pressed[0]`;
  - `hovered`, `keyboard` y `mouse_over_ui`.
- **`DataModel::fire_event(source, name, other, number, text)`** y **`WorldListener::event`** (`GameEvent`). Nombres:
  - `Touched` (other = la otra pieza);
  - `MouseButton1Click` / `Activated` / `MouseEnter` / `MouseLeave` (GUI);
  - `Died` / `HealthChanged` (Humanoid, number = vida);
  - `Ended` / `Looped` (Sound);
  - `InputBegan` / `InputEnded` (source = raíz del DataModel, text = nombre de tecla).
- **`DataModel::create_internal`**: crea clases concretas no insertables (Player, PlayerGui, ImportedInstance). `ClassInfo::is_abstract` distingue las abstractas de verdad.
- **`engine/runtime`, `tefra::runtime::GameRuntime`** (`tefra/runtime/game_runtime.h`):
  - Al arrancar crea `Players.Player` con PlayerGui y PlayerScripts, clona StarterGui y StarterPlayerScripts y genera el avatar R15 en el SpawnLocation (o en `Player.RespawnLocation`).
  - Contiene el CharacterController, PhysicsWorld, AnimatorSystem y ScriptHost (opcional, de quien lo llama).
  - Convierte `Touched` de la física en eventos.
  - Muerte: Health ≤ 0 o caer por debajo de `Workspace.FallenPartsDestroyHeight` fija State=Dead y dispara `Died`; tras `Players.RespawnTime` reaparece.
  - Cámara en tercera persona; `camera()` devuelve un `CameraState`.
  - `RuntimeSystem` (start / pre_physics / post_physics / stop) para módulos; `system("audio")` los busca por nombre.
  - `resolve_asset(path)` resuelve rutas relativas a la carpeta del proyecto.
- **`engine/ui`** (`tefra/ui/game_gui.h`, `imgui_input.h`):
  - `GameGui::draw(ImDrawList*, Rect, PlayerGui, InputState*, interactive)` y `layout_rect()`;
  - implementación básica: frames, texto, botones y eventos de click.
- **`engine/audio`** (`tefra/audio/audio_system.h`): `make_audio_system()` devuelve un RuntimeSystem "audio". Hoy es un stub silencioso.
- **Clases nuevas** (`engine/world/src/classes.cpp`):
  - Players (RespawnTime), SoundService (Volume, RolloffScale);
  - Player (DisplayName, Character, UserId, RespawnLocation), PlayerGui, PlayerScripts, StarterPlayerScripts, StarterCharacterScripts;
  - ImportedInstance (OriginalClass, OriginalProperties JSON);
  - `LuauSource` (oculta) en BaseScript y ModuleScript;
  - GuiBase/ScreenGui/GuiObject/Frame/TextLabel/TextButton/ImageLabel/ImageButton (propiedades UDim2 partidas: Position+PositionOffset, Size+SizeOffset; AnchorPoint; CornerRadius; Text*; Font; Image*);
  - Sound (SoundId, Volume, PlaybackSpeed, Looped, Playing, PlayOnRemove, RollOffMin/MaxDistance, TimePosition, TimeLength).
- Studio ya ejecuta el Play con GameRuntime (`apps/studio/src/play.cpp`): añade el sistema de audio y dibuja GameGui sobre el viewport. **No toques `apps/studio/src/{app,ui,play}.cpp` salvo que tu tarea lo pida explícitamente.** El integrador (yo) hace la página de inicio, los proyectos, la exportación y el player en paralelo y los toca mucho.

## Unidades y convenciones
- Metros y segundos. +Y arriba, −Z adelante. Orientation en grados YXZ.
- Las propiedades nuevas van en `classes.cpp`, en la sección de su clase. Si añades clases, actualiza también `managed/Tefra.Engine/src/Classes.cs` (wrappers a mano; el orden de los enums debe coincidir).

## Entregables de cada agente
1. Código, tests (doctest en `tests/unit/`, registrados en `tests/CMakeLists.txt`) y docs: `docs/research/m3b-<tema>/REPORT.md` con qué hiciste, cómo probarlo, límites y costes.
2. `ctest --preset linux-release` en verde y 0 errores de validación si tocas GPU.
3. Commit en tu rama, con un mensaje claro y sin identificadores de modelo.
4. Respuesta final con: rama, commit, archivos tocados fuera de tu módulo (para el merge) y riesgos.
