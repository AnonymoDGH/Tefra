# Gap analysis: Tefra frente a Roblox Studio, Unity, Godot 4 y Unreal

Fecha: 2026-10-10. Base: rama `claude/new-session-ksgl0m` en el commit `5059d1f`. Es un informe de investigación: no toca código.

Petición del usuario: hacer juegos de verdad, no básicos, y sobre todo **UIs propias y totalmente personalizadas**.

Se dan por "en camino" las seis ramas abiertas y no se cuentan como huecos:

- importador de Roblox;
- paisaje en Studio;
- LocalScript/ModuleScript + eventos + UserInputService;
- mejoras de UI (UICorner, UIStroke, UIPadding, UIListLayout, UIGridLayout, UIGradient, TextBox, BillboardGui, imágenes);
- sonido con miniaudio;
- skinning glTF.

Cuando a una de ellas le falta algo importante, se señala con **(⚠ rama)**.

Las comparaciones salen de la documentación pública de cada producto: Roblox Creator Docs, Unity Manual (UI Toolkit/uGUI), Godot 4 docs (Control, Theme, AnimationPlayer, Tween) y Unreal docs (UMG, Slate, CommonUI). Es clean-room: solo se toman conceptos y no se copia código.

Escala de esfuerzo:

- **S**: menos de 2 días de un agente.
- **M**: de 3 a 7 días.
- **L**: más de una semana o varias piezas.

---

## 0. Estado real hoy (lo que se encontró en el código)

| Área | Qué hay | Dónde |
|---|---|---|
| Clases GUI | ScreenGui, Frame, TextLabel, TextButton, ImageLabel, ImageButton. UDim2 partido en `Position`+`PositionOffset` y `Size`+`SizeOffset` (tipo `Vector3`). Además AnchorPoint, Rotation, CornerRadius, Border*, Text*, `Font` como enum de 3 valores (Default/Bold/Mono) y ScaleType | `engine/world/src/classes.cpp:194-237` |
| Render de la UI | `GameGui::draw` en modo inmediato sobre un `ImDrawList` de Dear ImGui. Recorre el árbol entero en cada frame. La fuente es siempre la de ImGui (`ImGui::GetFont()`). El contorno del texto se hace con 8 `AddText` desplazados. **`Rotation` no se aplica.** El clip es solo rectangular y además no recorta a los hijos: `draw_children` recibe el clip del padre, no el rect del objeto. `ScaleType` se ignora | `engine/ui/src/game_gui.cpp` |
| Eventos GUI | MouseEnter/Leave, MouseButton1Click y Activated, vía `DataModel::fire_event` | `game_gui.cpp:160-185` |
| UI en el editor | **Studio no dibuja StarterGui en modo edición.** `StarterGui.ShowDevelopmentGui` existe pero nadie lo lee, y la GUI solo se ve al darle a Play (`apps/studio/src/play.cpp:99-104`). No hay forma visual de colocar la UI: solo numérica, en Properties | `apps/studio/src/ui.cpp` |
| Tipos de propiedad | `Bool, Int, Float, String, Vector3, Color3, Enum, Ref` y `Value::n[3]`. No hay UDim2, Vector2, Rect, ColorSequence, NumberSequence, NumberRange, CFrame ni FontFace | `engine/world/include/tefra/world/datamodel.h:19-27` |
| API C# | Instance (Changed, Destroying, indexador por nombre), BasePart, Part, MeshPart, SpawnLocation, Model, Folder, Workspace, Lighting y scripts. RunService.Heartbeat, `Task.Wait` y Debug. **No hay wrappers de GUI, Humanoid, Sound ni Camera, ni Raycast desde C#, Touched ni TweenService.** La plantilla Obby usa `TextLabel`/`TextButton.Activated`, que dependen de la rama de LocalScript | `managed/Tefra.Engine/src/*.cs`, `apps/studio/src/templates.cpp:237-275` |
| Render 3D | Deferred: gbuffer → lighting → ocean/water → TAA → exposure → bloom → tonemap. **No hay pase forward/transparente:** `Transparency` ≥ 0,99 oculta la pieza y los valores intermedios no se mezclan. **PointLight y SpotLight solo existen como clases:** el renderer no tiene luces locales. Neon se aproxima con emissive. Tampoco hay pase de partículas, de decals ni de UI propia en el render graph | `engine/renderer/src/renderer.cpp:835-1330`, `engine/world/src/scene_sync.cpp` |
| Física | Jolt: cuerpos, CharacterVirtual, `raycast()` en C++ y Touched. Sin constraints ni vehículos | `engine/physics/include/tefra/physics/physics_world.h` |
| Editor de scripts | `ImGui::InputTextMultiline`. Sin resaltado, autocompletado, errores en línea ni depurador | `apps/studio/src/ui.cpp:885` |
| Audio | `NullAudio` (stub). Lo resuelve la rama de miniaudio | `engine/audio/src/audio_system.cpp` |
| Runtime | `GameRuntime` con RuntimeSystems enchufables, compartido por Studio y `tefra_player` | `engine/runtime/` |
| Fuentes | Dear ImGui **1.93 WIP**: tiene fuentes dinámicas (desde 1.92 cualquier tamaño se rasteriza a demanda y se pueden cargar TTF en caliente) e incluye `imstb_truetype.h`, que genera SDF por glifo | `third_party/imgui/imgui.h:32` |

**Diagnóstico:** el sistema de UI es un prototipo inmediato encima de ImGui. Para "UIs propias, no básicas" hacen falta cuatro cimientos antes que más widgets:

1. Tipos de valor de UI (UDim2, secuencias, FontFace).
2. Un renderer de UI propio con SDF (esquinas, bordes, sombras, máscaras y texto nítido a cualquier escala).
3. Animación de propiedades (TweenService).
4. Un **editor visual** en Studio.

---

## 1. UI personalizada avanzada (prioridad del usuario)

### 1.1 Cimientos transversales (bloquean casi todo lo demás)

| # | Cimiento | Hoy | Esfuerzo | Diseño recomendado |
|---|---|---|---|---|
| F1 | **Tipos de valor de UI** | UDim2 partido en dos Vector3. No hay secuencias | M | Ampliar `Value` a `n[4]` (o `f64 n[4]` + `std::string s`) y añadir `PropType::Vector2, UDim, UDim2, Rect, NumberRange, ColorSequence, NumberSequence, FontFace`. Las secuencias se guardan como un vector de keypoints en `s` (JSON compacto) o en un `std::vector<f32>` aparte. Migrar `Position/PositionOffset` → `Position: UDim2` con un *upgrader* de `.tefra` (versión de formato) y mantener los alias de lectura durante un tiempo. Actualizar a la vez el editor de Properties, la serialización, el MCP (JSON `{"xs":..,"xo":..,"ys":..,"yo":..}`) y C# (`struct UDim2`, `ColorSequence`). **Conviene hacerlo antes de que existan más juegos** con el formato partido, y también lo usa el importador de Roblox |
| F2 | **Renderer de UI propio** (retained + batching) | ImDrawList inmediato y fuente de ImGui | L | Módulo `engine/ui/render` con su propio pase en el render graph, después de `tonemap` y en espacio de salida (sRGB). Pasos: (a) un *UI tree cache* que escucha `WorldListener` (dirty flags por layout y por estilo) y solo re-layout lo que cambió; (b) *display list* de quads instanciados `UIQuad {rect, radius[4], fill(color/gradient/texture idx), stroke, shadow, clip_id, transform 2x3}` en un único storage buffer con BDA y texturas bindless (ya existen, ADR-003); (c) **un ubershader SDF de rounded-rect**: relleno, borde, gradiente lineal/radial, drop shadow analítico (gaussiana aproximada en SDF) y 9-slice. Una sola draw call por capa (ScreenGui/DisplayOrder) salvo que cambie el blend; (d) texto SDF (ver 1.2.6). Coste en la HD 520: 2–5 k quads ≈ 0,2–0,4 ms, el fill-rate es lo que limita. ImGui se queda para el editor. Las rotaciones salen gratis en el vertex shader. El mismo renderer sirve después para SurfaceGui/BillboardGui (rendering a textura) |
| F3 | **TweenService** (UI y Parts) | No existe | M | RuntimeSystem nativo `"tween"` que interpola **propiedades reflejadas** (Float, Vector2/3, Color3, UDim2, la componente de rotación de un CFrame). C#: `TweenService.Create(inst, new TweenInfo(time, EasingStyle.Quad, EasingDirection.Out, repeat, reverses, delay), new { Position = ..., BackgroundTransparency = 1 })`, que devuelve `Tween` con `Play/Pause/Cancel`, el evento `Completed` y `await tween.PlayAsync()` (encaja con el `Yield` de `Services.cs`). Easing: Linear, Sine, Quad, Cubic, Quart, Quint, Expo, Circ, Back, Elastic, Bounce y Spring (críticamente amortiguado). Al ser nativo, también lo puede usar el MCP. Mejor que hacerlo en C#: sin GC por frame y válido sin .NET |
| F4 | **Editor visual de UI en Studio** | Nada (ni siquiera se ve en modo edición) | L | Ver 1.2.1 |
| F5 | **Estilos y estados** (hover/pressed/focus/disabled) | `AutoButtonColor` fijo (×0,85 / ×0,7) | M | Ver 1.2.16 |

### 1.2 Funcionalidades (tabla resumen y después diseño)

Columnas Ro/Un/Go/UE: ✔ = la plataforma lo tiene de serie.

| Función | Ro | Un | Go | UE | Tefra hoy | Esf. | Depende de |
|---|---|---|---|---|---|---|---|
| Editor visual (drag/resize, guías, snapping, anclas) | ✔ | ✔ (UI Builder) | ✔ | ✔ (UMG Designer) | ✗ | L | F1, F2 opcional |
| Tweening / easing | ✔ TweenService | DOTween (3.º) / USS transitions | ✔ Tween | ✔ | ✗ | M | F1 |
| Timeline de animación UI (keyframes, secuencias) | parcial | ✔ Animator/Timeline | ✔ AnimationPlayer | ✔ UMG Animations | ✗ | L | F3 |
| 9-slice | ✔ SliceCenter | ✔ | ✔ NinePatchRect | ✔ Box brush | ✗ | S | F2 (o ImDrawList con 9 AddImage) |
| Rich text | ✔ | ✔ (TMP) | ✔ RichTextLabel | ✔ | ✗ | M | F2 texto |
| Fuentes TTF del usuario | ✔ FontFace | ✔ | ✔ | ✔ | ✗ (enum de 3) | S–M | F1 FontFace |
| ViewportFrame (3D en UI) | ✔ | ✔ RenderTexture | ✔ SubViewport | ✔ | ✗ | M | renderer offscreen |
| ScrollingFrame | ✔ | ✔ | ✔ ScrollContainer | ✔ ScrollBox | ✗ | M | clipping de hijos |
| CanvasGroup (transparencia de grupo) | ✔ | ✔ CanvasGroup | ✔ modulate | ✔ render opacity | ✗ | M | F2 capas |
| UIScale | ✔ | ✔ CanvasScaler | ✔ scale | ✔ DPI curve | ✗ | S | — |
| UIFlexLayout | ✔ | ✔ (flex de Yoga) | ✔ BoxContainer | ✔ | ✗ (List/Grid en rama) | M | layout pass |
| UITableLayout / UIPageLayout | ✔ | parcial | ✔ GridContainer/TabContainer | ✔ | ✗ | M | F3 (páginas animadas) |
| Máscaras y clip con esquinas | ✔ ClipsDescendants | ✔ Mask/RectMask2D | ✔ clip_children | ✔ clipping | solo rect, sin recortar hijos | M | F2 |
| Efectos: blur detrás, sombra, glow | parcial | shaders custom | ✔ shaders CanvasItem | ✔ BackgroundBlur, materiales | ✗ | M | F2 + scene color |
| Gradientes | ✔ UIGradient | ✔ (USS en 6.x) | ✔ GradientTexture | ✔ | rama (⚠ ver nota) | S | F1 ColorSequence |
| Temas / hojas de estilo | ✔ StyleSheet/StyleRule | ✔ USS | ✔ Theme | ✔ Styles/CommonUI | ✗ | M | F1 |
| Data binding | — | ✔ (6.x runtime binding) | parcial | ✔ MVVM plugin | ✗ | M | C# |
| Foco y navegación con gamepad | ✔ SelectionGroup | ✔ | ✔ focus_neighbor | ✔ CommonUI | ✗ | M | input de gamepad |
| Drag & drop en juego | ✔ UIDragDetector | ✔ | ✔ `_get_drag_data` | ✔ DragDrop op | ✗ | S–M | eventos de input |
| Prefabs/plantillas de UI | ✔ (Packages, Clone) | ✔ Prefab/UXML templates | ✔ PackedScene | ✔ User Widgets | Clone en C# | M | ver P0 Prefabs |
| Localización | ✔ LocalizationTable | ✔ package | ✔ TranslationServer | ✔ | ✗ | M | — |
| DPI / resolución | ✔ (auto) | ✔ | ✔ stretch modes | ✔ DPI curve | ✗ (píxeles crudos) | S | UIScale |
| SurfaceGui (UI en partes 3D) | ✔ | ✔ world canvas | ✔ SubViewport en malla | ✔ WidgetComponent | ✗ (BillboardGui en rama) | M | F2 a textura |
| Barras de vida en pantalla | ✔ (Billboard) | ✔ | ✔ | ✔ | rama Billboard | S | — |
| Minimapa | ViewportFrame/cámara | ✔ RenderTexture | ✔ | ✔ SceneCapture2D | ✗ | M | render offscreen |
| Cursor personalizado | ✔ Mouse.Icon | ✔ | ✔ | ✔ | ✗ | S | GLFW cursor |
| Sonido en hover/click | script | ✔ | ✔ | ✔ | ✗ | S | rama miniaudio + estilos |
| Rendimiento (batching, retained) | ✔ | ✔ | ✔ | ✔ invalidation box | ✗ inmediato | L | F2 |

#### 1.2.1 Editor visual de UI en Studio (L)

Es lo que más acerca Tefra a "hago mis UIs". Referencias públicas: Roblox (UI Editor / Studio 2D tools), UI Builder de Unity, la vista 2D de Godot y UMG Designer.

Diseño:

- **Modo 2D/UI** en el Viewport (conmutador o automático al seleccionar un GuiObject). Dibuja StarterGui en modo edición y respeta `ShowDevelopmentGui`.
- **Selección por hit-test GUI.** Gana el ZIndex más alto. Alt-click atraviesa.
- **8 tiradores de redimensionado** + arrastre para mover + rotación con un tirador superior. Shift mantiene la proporción y Alt redimensiona desde el centro.
- **Snapping** al borde del padre, al centro y a los bordes/centros de los hermanos, con **guías de alineación** magenta y distancia en píxeles. Rejilla configurable.
- **Scale vs offset:** un toggle para que el arrastre escriba en escala o en offset (en Roblox es el dolor clásico) y "convertir a scale/offset" en el menú contextual.
- **Widget de AnchorPoint** con 9 presets + stretch (los anchor presets de Unity). Mostrar el ancla en el canvas.
- **Barra de alinear y distribuir** con multi-selección.
- **Vista previa de dispositivo:** 1280×720, 1920×1080, 2560×1440, móvil 2400×1080 horizontal y 4:3, más el slider de UIScale. Mostrar safe area.
- Las ediciones pasan por `history.cpp` (undo/redo) y por `session` (testeable en `tefra_studio_core`).
- **Cobertura MCP:** `gui_hit_test`, `capture_viewport` con la GUI y `set_preview_resolution`, para que Claude diseñe UIs por MCP.
- **Insertar en Explorer:** paleta con miniaturas de los widgets y de las plantillas propias (ver prefabs).

Criterio de hecho: crear un menú principal con 3 botones centrados, anclados abajo, que se adapta de 720p a 1440p sin tocar números.

#### 1.2.2 Tweening y animación de UI

- **TweenService** (F3) cubre las transiciones de código.
- **Secuencias:** `TweenSequence` en C# (Then, Join, Wait, Callback), al estilo de los `Tween` encadenables de Godot y de DOTween. Es una capa C# fina sobre los tweens nativos (S).
- **Timeline visual (L, P1):** una clase `UIAnimation` (keyframes por propiedad y curvas bezier/easing por segmento) que hija de un GuiObject o ScreenGui y se reproduce con `Animator`-like `Play("Open")`. Panel "Animation" en Studio con pistas, scrub y grabación ("modo record": lo que editas en el canvas se convierte en keyframe), como AnimationPlayer de Godot o UMG Animations. La misma pista sirve para Parts, así que es un mini-sequencer reutilizable para cutscenes.
- **Transiciones de estado:** las aportan los StyleRule con `TransitionTime`/`TransitionEasing` (ver 1.2.16). El hover y el pressed animan solos.

#### 1.2.3 9-slice (S)

`ImageLabel.ScaleType` añade `Slice` y `Tile` (hoy el enum es Stretch/Fit/Crop y **ninguno se implementa**: `game_gui.cpp` siempre estira). Propiedades:

- `SliceCenter: Rect` en píxeles de la imagen;
- `SliceScale: float`;
- `TileSize: UDim2`;
- `ImageRectOffset/ImageRectSize` (sprite sheets y atlas).

Con ImDrawList se resuelve con 9 `AddImage`. En el renderer SDF, con UVs por segmento en el shader.

#### 1.2.4 Rich text (M)

`TextLabel.RichText: bool`. Tags: `<b> <i> <u> <s> <font color="#hex" size="n" face="Name" weight="bold"> <stroke color thickness> <br/> <sc>` + escape `&lt;`.

- Parser propio a *runs* `{text, font, size, color, decoration}`.
- Layout con word-wrap (`TextWrapped`, que hoy no existe), `LineHeight`, `MaxVisibleGraphemes` (efecto máquina de escribir, imprescindible en diálogos) y `TextTruncate` (AtEnd).
- Sin HarfBuzz en la primera versión: kerning de `stb_truetype` y Latin/extendido (español OK). RTL y scripts complejos quedan fuera y se documentan.

#### 1.2.5 Fuentes del usuario (S con ImGui 1.92+, M con SDF propio)

- `FontFace` = `{Family: "assets/fonts/Orbitron.ttf", Weight, Style}`, que sustituye al enum `Font`. Familias incluidas: Inter y JetBrains Mono, que ya están en `resources/fonts`.
- **Corto plazo:** con ImGui 1.93 dinámico, `AddFontFromFileTTF` en caliente + `PushFont(font, size)`, sin atlas por tamaño.
- **Largo plazo** (con F2): atlas **SDF** dinámico R8 1024² por fuente (glifos rasterizados con `stbtt_GetGlyphSDF`, LRU), que da nitidez en UIScale/BillboardGui y contorno/sombra/glow de texto gratis en el shader. Así desaparecen los 8 `AddText` del stroke actual. MSDF (msdfgen, MIT) solo si las esquinas se ven blandas a más de 64 px.
- Export: copiar las fuentes referenciadas (ADR-018 ya copia assets).

#### 1.2.6 ViewportFrame (M)

Muestra un modelo 3D dentro de la UI: inventario, selector de personaje o arma girando.

- `ViewportFrame` con hijos `WorldModel`/Model y su `CurrentCamera`, más `Ambient`, `LightColor`, `LightDirection` e `ImageColor`.
- **Render:** pase forward simple (sin GBuffer/TAA) a un RT offscreen del tamaño del frame, con límite (por ejemplo 512² y un máximo de 4 ViewportFrames activos en Low). Se reutilizan `mesh.vert`, `brdf.glsl` e IBL. **Solo se re-renderiza si cambia algo** (cámara o hijos dirty), lo que deja el coste casi nulo en menús estáticos.
- El RT se registra como textura bindless y el UI renderer lo muestra como imagen. El mismo camino sirve para **minimapas** (cámara cenital a RT a 5–10 Hz) y para los "SceneCapture" de espejos/cámaras de seguridad.

#### 1.2.7 ScrollingFrame (M)

- Propiedades: `CanvasSize: UDim2`, `CanvasPosition: Vector2`, `AutomaticCanvasSize`, `ScrollBarThickness`, `ScrollBarImage`/color, `ScrollingDirection` y `ElasticBehavior`.
- Inercia con rueda, arrastre y gamepad.
- Requiere **clip de hijos** (hoy roto, ver §0) y `AutomaticSize` en Frame/TextLabel (el contenido define el tamaño), que también es clave para listas de chat e inventarios.
- Virtualización opcional (P2): en C#, un `VirtualList` que recicla N filas.

#### 1.2.8 CanvasGroup (M)

- `CanvasGroup` (GuiObject) con `GroupTransparency` y `GroupColor`.
- Los hijos se renderizan a una capa offscreen (atlas de capas, 1 RT reciclado) y se componen con alfa: un fade de un panel entero sin que se vean los solapes internos.
- Es también la base de **máscaras de forma arbitraria** (una imagen como máscara alfa) y de efectos por grupo (blur o desaturar un menú en pausa).
- Coste: un RT por grupo visible. Limitar en Low o recomendar `Visible`+tween de transparencia cuando no haya solapes.

#### 1.2.9 UIScale, DPI y resolución (S)

- `UIScale.Scale` multiplica el subárbol.
- `ScreenGui.ScreenInsets` (None / DeviceSafeInsets) e `IgnoreGuiInset`.
- Escala global automática: `ScreenGui.ScaleMode = {None, ReferenceResolution}` con `ReferenceResolution` 1920×1080 y `MatchWidthOrHeight` (el CanvasScaler de Unity y el stretch "canvas_items" de Godot). Ojo: en Roblox no existe este modo y es una queja habitual. Tefra puede mejorarlo.
- Leer `glfwGetWindowContentScale` para DPI real.

#### 1.2.10 Layouts avanzados (M)

La rama aporta List y Grid. Faltan:

- **UIFlexLayout** (como hijo de un Frame con UIListLayout, igual que en Roblox):
  - `FlexMode` Grow/Shrink/Fill/Custom con `GrowRatio`/`ShrinkRatio`;
  - `Wraps`, `HorizontalFlex`/`VerticalFlex` (Fill, SpaceBetween, SpaceAround, SpaceEvenly) e `ItemLineAlignment`.
- **UIAspectRatioConstraint** y **UISizeConstraint/UITextSizeConstraint**: imprescindibles para UIs que no se deforman en ultrawide.
- **UIPageLayout**: carrusel o páginas con tween (`Animated`, `EasingStyle`, `Circular`) y `JumpTo`/`Next`/`Previous`.
- **UITableLayout**: filas y columnas por hijos.
- **Implementación:** un layout pass en dos fases (measure → arrange) dentro del UI tree cache. Determinista y testeable en doctest (puro, como `layout_rect`). **(⚠ rama)** comprobar que List/Grid de la rama ya hacen measure/arrange con `AutomaticSize`. Si no, rehacerlos encima de ese pass.

#### 1.2.11 Máscaras y clipping (M)

- `ClipsDescendants: bool` en GuiObject. Hoy, además, se dibuja a los hijos con el clip del abuelo.
- Con el renderer SDF: cada quad lleva `clip_id` → pila de hasta 4 rounded-rects (centro, half-size, radio y transform) en el buffer de clip, y el fragment shader descarta con SDF. Eso da clip **con esquinas redondeadas** (avatar circular, barra de vida con píldora) sin stencil.
- Para máscaras por imagen: CanvasGroup + `MaskImage`.

#### 1.2.12 Efectos y shaders de UI (M)

- **Drop shadow / glow:** `UIShadow` (Offset, Blur, Spread, Color, Transparency) evaluado analíticamente en el SDF del rounded-rect (sin pases extra). `UIStroke` de la rama + `Glow` = sombra sin offset con blend aditivo.
- **Blur detrás (glassmorphism):** `UIBlur.Size` en un Frame. Hace un downsample del color de escena post-tonemap con 3–4 niveles dual-Kawase a 1/4 de resolución (reutilizando el patrón de `bloom_down.frag`), una sola vez por frame si algún frame visible lo pide, y se muestrea con el rect del frame. En la HD 520, ~0,3–0,5 ms a 720p. Flag `RenderSettings.ui_blur` y `--no-ui-blur` (convención del proyecto).
- **Shader de usuario (P2):** `UIMaterial` con GLSL restringido (función `vec4 shade(UIInput)`) compilado en runtime con glslang, que ya está vendored como herramienta de build. Habría que enlazarlo como biblioteca o precompilar al exportar. Es el equivalente a los CanvasItem shaders de Godot o a los materiales UMG. Riesgo: tamaño y licencias. Alternativa: 8–10 efectos parametrizados (desaturate, dissolve con ruido, shine/sweep, pixelate, chromatic, outline de imagen) cubren el 90 %.

#### 1.2.13 Gradientes (S)

- UIGradient está en la rama. **(⚠ rama)** Necesita `Color: ColorSequence` y `Transparency: NumberSequence` de verdad (multi-stop), `Rotation`, `Offset` y que se aplique también a texto e imágenes. Si la rama usa 2 colores por falta de tipo, F1 lo desbloquea.
- Gradiente radial: `UIGradient.Type`.

#### 1.2.14 Temas, hojas de estilo y estados (M), lo que hace que una UI parezca "de juego"

Referencias: StyleSheet/StyleRule de Roblox (UI Styling, con selectores y tokens), Theme y type variations de Godot, USS de Unity y CommonUI Styles de Unreal.

- Instancias:
  - `StyleSheet` (en ReplicatedStorage o StarterGui) → hijos `StyleRule { Selector: "TextButton.primary:hover", Priority }`, cuyas propiedades son **attributes** (requiere P0 Attributes);
  - `StyleLink` en un ScreenGui para aplicarla;
  - `GuiObject.StyleClasses: "primary large"`.
- **Selectores:** clase de instancia, `.tag`, `#Name`, descendiente `>`, pseudo-estados `:hover :pressed :focus :disabled :selected`.
- **Tokens:** attributes `$Accent = Color3` en la StyleSheet, referenciados como `"$Accent"`.
- **Transiciones:** `TransitionTime`/`TransitionEasing` en la regla. El motor tweenea al cambiar de estado, sin scripts.
- **Resolución:** al cambiar clase, estado o hoja se marcan dirty las propiedades derivadas. Las propiedades explícitas de la instancia ganan a las del estilo, como en Roblox.
- **Studio:** panel "Styles" con vista previa de estados (forzar `:hover` en el canvas).
- `GuiObject.Interactable` / `Active` habilitan `:disabled`.

#### 1.2.15 Data binding (M, P2)

No es imprescindible si TweenService y los eventos están bien, pero acelera mucho los HUD.

- C#: `Observable<T>` y `Bind(label, l => l.Text, () => $"{player.Coins}")`, reevaluado en Heartbeat solo cuando cambia la fuente (`Changed` de Instance o setter de Observable).
- En Studio: el atributo `BindText = "Player.leaderstats.Coins"` para casos simples sin código.

#### 1.2.16 Foco y navegación con gamepad (M)

- Requiere gamepad en `InputState` (GLFW lo soporta: `glfwGetGamepadState` con mappings SDL incluidos).
- Clases y propiedades:
  - `GuiObject.Selectable`, `NextSelectionUp/Down/Left/Right: Ref`;
  - `GuiService.SelectedObject`;
  - `SelectionGroup`/`SelectionBehavior` (Stop/Escape);
  - **navegación automática** por geometría (el vecino más cercano en la dirección), como el focus de Godot y Unity.
- Estado `:focus` en estilos e imagen de selección (`SelectionImageObject`).
- Teclado: Tab/Shift-Tab y Enter = Activated.
- Imprescindible para juegos de PC con mando y para accesibilidad.

#### 1.2.17 Drag & drop en juego (S–M)

- Eventos de bajo nivel: `InputBegan/InputChanged/InputEnded` por GuiObject (la rama de UserInputService debería traerlos a nivel global; **(⚠ rama)** faltan *por GuiObject*, con posición) y `MouseButton1Down/Up`, `MouseMoved` y `MouseWheelForward/Backward`.
- Componente `UIDragDetector` (DragStyle: TranslatePlane/Line/Scriptable, BoundingUI, eventos DragStart/DragContinue/DragEnd).
- Helper C# `DragDrop.Begin(payload, ghost)` + `DropTarget` para inventarios.

#### 1.2.18 Prefabs y componentes de UI (M; mismo sistema que P0 Prefabs)

- **Packages/Prefabs** (§2): un ScreenGui o Frame guardado como `.tefrapkg` en `assets/` e instanciado con `Prefab.Instantiate("ui/ShopItem")`.
- Override de propiedades por instancia y "aplicar cambios al prefab".
- **Componentes UI en C#:** clase `UIComponent : TefraScript` asociada a un Frame plantilla, con propiedades expuestas como attributes editables en Properties (el `[SerializeField]` de Unity, el `@export` de Godot o las variables "Instance Editable" de UMG).

#### 1.2.19 Localización (M)

- `LocalizationService` + `LocalizationTable` (CSV/JSON en assets: key, es, en, …).
- `TextLabel.AutoLocalize` y `Text = "@menu.play"` o `TextKey`.
- C#: `Localization.Get("key", args)`.
- Captura de cadenas en Studio (escanea los TextLabel y genera la tabla).
- Selector de idioma en la vista previa.

#### 1.2.20 SurfaceGui y BillboardGui

- BillboardGui está en la rama. **(⚠ rama)** Comprobar: `AlwaysOnTop`, `MaxDistance`, `Size` en studs/metros vs píxeles, oclusión por profundidad y `LightInfluence`.
- **SurfaceGui** (M): GUI renderizada a textura (`PixelsPerStud` → PixelsPerMeter) y aplicada a una cara (`Face`) de una Part. Necesita (a) el UI renderer a RT y (b) que el material de la pieza acepte una textura emissive/albedo por cara (ver P0 Decals/Textures). Interacción: raycast desde el cursor → UV → hit-test GUI (pantallas táctiles en el mundo, carteles, terminales).

#### 1.2.21 HUD de juego: barras de vida, minimapa, cursor, sonido

- **Barra de vida** sobre NPCs: BillboardGui + Frame con tween. En pantalla: Frame con `Size` tweened y clip redondeado (1.2.11). Plantilla en el Toolbox.
- **Minimapa:** ViewportFrame cenital o RT + máscara circular + iconos. Plantilla.
- **Cursor:** `UserInputService.MouseIcon` (ruta de imagen → `glfwCreateCursor`), `MouseIconEnabled` y `MouseBehavior` (Default/LockCenter/LockCurrent: tercera persona tipo shooter). Cursor dibujado por la UI para animarlo.
- **Sonido en UI:** con miniaudio (rama), `HoverSound`/`ClickSound` en StyleRule o GuiButton y `SoundService:PlayLocalSound`.

#### 1.2.22 Rendimiento

- El modelo actual recorre el DataModel completo y hace `get_*` por nombre (búsqueda de string) por objeto y por frame. Con 1–2 k GuiObjects (inventario) se nota en el i5-6200U.
- El UI tree cache (F2) con índices de propiedad precalculados y dirty flags lo resuelve. Hay que medirlo con `tefra_bench --scene ui-stress` (escena nueva: 2 000 frames, 500 textos).
- Objetivo en la HD 520: menos de 0,5 ms GPU y menos de 0,5 ms CPU con 2 000 elementos estáticos.

### 1.3 Resumen de clases nuevas propuestas (UI)

`CanvasGroup, ScrollingFrame, ViewportFrame, WorldModel, SurfaceGui, VideoFrame (P2), UIScale, UIAspectRatioConstraint, UISizeConstraint, UITextSizeConstraint, UIFlexItem, UIPageLayout, UITableLayout, UIShadow, UIBlur, UIDragDetector, StyleSheet, StyleRule, StyleLink, UIAnimation, LocalizationTable`.

Propiedades nuevas en GuiObject:

- `ClipsDescendants`, `AutomaticSize`, `Selectable`, `NextSelection*`, `StyleClasses`, `Interactable`, `LayoutOrder` (si la rama no lo trae).
- `TextWrapped`, `RichText`, `LineHeight`, `MaxVisibleGraphemes`, `TextTruncate`, `FontFace`.
- `SliceCenter`, `SliceScale`, `ImageRectOffset/Size`, `TileSize`.

---

## 2. Huecos generales del motor para juegos completos

La columna **Ship** marca lo imprescindible antes de que el usuario pueda publicar un juego no básico.

### 2.1 P0 (sin esto no hay juego "de verdad")

| Hueco | Hoy | Esf. | Ship | Diseño en Tefra |
|---|---|---|---|---|
| **API C# completa del DataModel** (wrappers GUI, Humanoid, Sound, Camera; eventos Touched, Died, HealthChanged; `Workspace.Raycast`) | Solo Parts y scripts. La rama de LocalScript trae eventos e input | M | ★ | **(⚠ rama)** comprobar que incluye `Touched`, `Humanoid.Died`, `Raycast`/`RaycastParams` (FilterDescendantsInstances, CollisionGroup) y wrappers GUI. Mejor **generar** `Classes.cs` desde la reflexión (un generador que vuelque `ClassRegistry` → C#) en vez de mantenerlo a mano, porque es la causa número 1 de desincronías |
| **Transparencias reales** (pase forward) | `Transparency` binaria (oculta a ≥ 0,99) | M | ★ | Pase `forward_transparent` después de `lighting` (ADR-008 ya lo preveía): ordenado back-to-front, con luz del sol + IBL + niebla y sin TAA jitter en el alfa (o con reactive mask). Material Glass con refracción barata (muestrear la escena opaca con offset, como en el agua). También es la base de las partículas |
| **Luces locales** (PointLight/SpotLight, emissive real) | Clases sin render | M | ★ | Clustered lighting: un compute que asigna luces a un grid 16×9×24 de froxels, mismo grid que la niebla. Límite de 128 luces visibles en Low y sombras solo en 2–4 spot (atlas 2k). SurfaceLight después. Neon → emissive que alimenta el bloom (ya existe) |
| **Partículas/VFX**: ParticleEmitter, Beam, Trail, Attachment | Nada | L | ★ | `Attachment` (punto local en una Part, base también de constraints). ParticleEmitter con Rate, Lifetime: NumberRange, Speed, SpreadAngle, Color: ColorSequence, Size/Transparency: NumberSequence, Texture + flipbook, LightEmission, Drag, Acceleration, LockedToPart, `Emit(n)`. Simulación GPU (compute, buffer anillo, 64 k partículas máximo en Low), render billboard en el pase forward con blend premultiplicado (aditivo/alfa con un solo shader). Beam/Trail como tiras de quads. Depende de F1 (secuencias) y del pase forward |
| **Texturas/decals por parte y PBR por material** | 21 Materials con valores escalares; `MeshPart.TextureId` | M | ★ | `Decal`/`Texture` (Face, Texture, Color3, Transparency, StudsPerTile) proyectados al gbuffer (decal deferred en caja, barato). `SurfaceAppearance`/`MaterialVariant` (ColorMap, NormalMap, RoughnessMap, MetalnessMap) con texturas bindless. Texturas CC0 por defecto para los 21 Materials (triplanar, como el terreno). Imprescindible para que no parezca "de bloques" |
| **TweenService** (Parts y UI) | Nada | M | ★ | F3 |
| **Constraints y joints**: Weld, WeldConstraint, Hinge, BallSocket, Prismatic, Rope, Spring, AlignPosition/Orientation, LinearVelocity, VectorForce | Nada | M–L | ★ | Mapeo directo a Jolt (Fixed, Hinge con motor/servo, Point, Slider, Distance con min/max = Rope, SixDOF; muelles por `SpringSettings`). Las Parts soldadas forman un *assembly*: un solo cuerpo compuesto (`MutableCompoundShape` o `StaticCompound`), que también resuelve los Models con Parts sueltas que hoy caen por separado. Es la base de puertas, ascensores, vehículos y ragdolls |
| **Prefabs/Packages + Toolbox de assets** | Clone en C#; plantillas solo en código (`templates.cpp`) | M | ★ | `.tefrapkg` (subárbol JSON, mismo formato `.tefra`) en `assets/`. "Save as Package" en Explorer. Instancia con `PackageLink` (ruta + versión) y actualizar todas las copias. Panel **Toolbox** con las carpetas del proyecto + una biblioteca CC0 incluida (Poly Haven ya es accesible). C#: `Assets.Load<Model>("prefabs/Coin")` |
| **Importación de assets por drag-and-drop** | Rutas a mano en Properties | M | ★ | Soltar `.glb/.gltf/.png/.jpg/.wav/.ogg/.ttf` en Studio: copia a `assets/<tipo>/`, crea MeshPart, ImageLabel/Decal, Sound o FontFace según el destino, genera miniatura y **cachea** la conversión (texturas a KTX2/BC7 offline, opcional). GLFW tiene `glfwSetDropCallback`. Panel Asset Manager con búsqueda, uso y "buscar referencias" |
| **Attributes + CollectionService (tags)** | Nada | S–M | ★ | `Instance:SetAttribute/GetAttribute/GetAttributeChangedSignal` guardados en `.tefra` (mapa `name → Value`) y editables en Properties (sección Attributes, botón "+"). `CollectionService.AddTag/GetTagged/GetInstanceAddedSignal`, con panel Tag Editor. Necesario para StyleRules, componentes y datos de diseño sin código |
| **Guardado de partidas** (DataStore local) | Nada | S | ★ | `DataStoreService.GetDataStore("save").SetAsync/GetAsync/UpdateAsync` → JSON en `%APPDATA%/TefraGames/<game-id>/` (en Linux, `$XDG_DATA_HOME`). Escritura atómica (tmp + rename) y versión de esquema. En Studio, carpeta de prueba aparte con "Clear Data" |
| **Editor de C# serio** | InputTextMultiline | M–L | ★ | Fase 1 (M): un widget ImGui con resaltado (ImGuiColorTextEdit, MIT, o propio), números de línea, buscar/reemplazar y **errores de Roslyn subrayados al escribir** (la compilación ya existe en `ScriptCompiler.cs`; exponer diagnostics con línea y columna). Fase 2 (M): **autocompletado con Roslyn `CompletionService`** (Microsoft.CodeAnalysis.Features) en el proceso .NET ya cargado, más hover con la documentación XML. Alternativa: botón "Open in VS Code" + generar `.csproj` con referencia a `Tefra.Engine.dll`, que da IntelliSense completo gratis y conviene hacerlo **ya** (S) |
| **Multi-selección y transform en grupo, pivots** | Selección múltiple básica; gizmo sobre una | S–M | ★ | Gizmo sobre el centro del bounding box, `Model.PivotOffset`/`WorldPivot`, rotar y escalar el grupo, y snapping de rotación (15°) y de movimiento configurable |
| **Exportación jugable y empaquetado** | Carpeta autocontenida (ADR-018) | S | ★ | Icono y versión del .exe (rc de MinGW), splash, ajustes de calidad en el juego (Low/Med/High), opción de **framework-dependent vs self-contained .NET** (el usuario final no tendrá el SDK: hay que publicar `Tefra.Engine` + los scripts **precompilados** a DLL al exportar, sin Roslyn en runtime, y empaquetar el runtime .NET 8 o usar NativeAOT). Comprimir assets en un `.pak` (zip) |

### 2.2 P1 (juegos ambiciosos y flujo profesional)

| Hueco | Esf. | Ship | Notas de diseño |
|---|---|---|---|
| Pathfinding/NavMesh + NPC | L | ★ si hay enemigos | Recast/Detour (zlib, vendored). `PathfindingService:CreatePath(agentParams):ComputeAsync(a,b)` → waypoints. `Humanoid:MoveTo(pos)` + evento `MoveToFinished`. NavMesh horneado en Studio (visualizable) y regenerado por tiles en runtime para Parts que se mueven. Helper C# de máquina de estados o behaviour tree ligero |
| Vehículos | M | — | `VehicleSeat` + `Seat` (Humanoid.Sit) sobre `JPH::VehicleConstraint` (WheeledVehicle) con plantilla de coche. Depende de constraints |
| Shape-casts y queries | S | ★ | `Workspace:Blockcast/Spherecast/Shapecast`, `GetPartBoundsInBox/Radius`, `CollisionGroups` (PhysicsService) con matriz editable |
| Animación: editor de clips y eventos | L | ★ para personajes propios | Animation Editor en Studio (keyframes sobre Motor6D, curvas, eventos con nombre `GetMarkerReachedSignal`). Formato `.tefraanim` y exportar glTF. Retarget R15 ↔ glTF (después de la rama de skinning). IK (two-bone, foot planting). La misma timeline que §1.2.2 |
| Post-procesado por lugar | S | — | Clases `BloomEffect`, `ColorCorrectionEffect` (Tint, Saturation, Contrast, Brightness), `DepthOfFieldEffect`, `SunRaysEffect`, `BlurEffect`, `Atmosphere`, `Sky` (cubemap del usuario) bajo Lighting o Camera. Mapean a `RenderSettings`, que ya existen casi todos (bloom, viñeta, CA, grano, exposición). Tweeneable (fundido a negro o daño en rojo) |
| Cámara scriptable | S | ★ | `Camera.CameraType` (Custom/Scriptable/Follow/Fixed), `CFrame`, `Focus`, `FieldOfView` tweeneable y shake. `CameraSubject`. API C# (hoy solo hay `set_camera` en C++) |
| CFrame como tipo | M | ★ | `Position+Orientation` en grados YXZ es frágil para gameplay. Añadir `CFrame` en C# (`LookAt`, `*`, `Inverse`, `ToWorldSpace`) aunque la propiedad se siga guardando partida |
| Sonido avanzado | M | — | Sobre la rama: `SoundGroup` (buses, volumen por categoría para el menú de opciones), efectos (Reverb, EQ, Compressor), música con crossfade y oclusión simple por raycast |
| Gamepad y entrada abstracta | M | ★ | Gamepad en `InputState` (GLFW), `ContextActionService:BindAction(name, fn, keys…)` / Input Actions remapeables (el Input System de Unity, las InputMap actions de Godot) con guardado del remapeo. **(⚠ rama)** UserInputService debería incluir `GamepadConnected` y `InputObject.KeyCode` de gamepad |
| Herramientas de terreno | L | — | La rama aporta paisaje. Faltan esculpir (subir/bajar/suavizar/aplanar), pintar capas de material, colocar vegetación con pincel y agua por regiones. Heightmap editable de 16 bits + splatmap por tile, guardado en `assets/terrain/` (binario al lado del `.tefra`) |
| Depurador de scripts | L | — | Breakpoints vía .NET debugger: es más realista adjuntar VS Code (`vsdbg` es propietario; `netcoredbg`, MIT, funciona con MinGW y Linux) que un depurador propio. Studio: "Debug" lanza con `DOTNET_` y puerto. Mientras tanto: stack traces con línea (los PDB en memoria con Roslyn `EmitOptions` + `pdbStream`) y clic en el Output para saltar a la línea |
| Profiler | M | ★ para optimizar en la HD 520 | Microprofile propio: zonas CPU (`TEFRA_ZONE`) + timestamps GPU por pase del render graph (las queries ya son baseline). Panel en Studio y `--profile out.json` en formato Chrome trace. Contadores: draw calls, partículas, luces y ms por script (cronometrar los handlers C#) |
| Versionado e historial del lugar | S | — | `.tefra` ya es JSON diffeable. Botón "Snapshots": autosave cada 5 min con 20 versiones en `.tefra-history/`, más guía Git. Team Create real queda fuera de alcance (P2) |
| Plugins de Studio | M | — | Scripts C# con `[StudioPlugin]` que añaden botones de toolbar y paneles ImGui mínimos (wrapper `PluginGui`) y acceden a la selección e historial. El MCP ya cubre parte (automatización por IA) |
| Accesibilidad | S–M | — | Escala de texto global, modos para daltonismo (filtro en tonemap), subtítulos (TextLabel + Sound markers), remapeo de controles y reducir movimiento (desactivar shake y blur) |
| Localización | M | — | §1.2.19 |
| Hot-reload de scripts en Play | M | — | `AssemblyLoadContext` coleccionable ya permite recargar. Recompilar al guardar y reiniciar solo ese script |

### 2.3 P2 (más adelante)

| Hueco | Esf. | Notas |
|---|---|---|
| Multijugador (RemoteEvent/RemoteFunction, replicación) | XL | El modelo Server/Client de scripts ya existe en el DataModel (RunContext, LocalScript). Diseño mínimo: servidor autoritativo headless (`tefra_player --server`), replicación de propiedades por diff del DataModel con ids estables (ya existen), sobre UDP con ENet (MIT) o GameNetworkingSockets. Introducir **RemoteEvent/RemoteFunction ya en single-player** como no-op local, para que los juegos se escriban bien desde el principio (S) |
| Team Create, colaboración | XL | No prioritario para un solo creador. Git + `.tefra` JSON basta |
| Móvil/táctil | L | El target es PC. Si llega: TouchEnabled, joystick virtual y botones de salto en StarterGui |
| Shaders de material de usuario | L | Junto a `UIMaterial` (§1.2.12) |
| VideoFrame | M | Decodificar (pl_mpeg MPEG-1, MIT) a textura. Útil para intros |
| Ragdoll, destrucción | M | Jolt `Ragdoll` + `Humanoid.BreakJointsOnDeath` |
| Cinemáticas (sequencer) | M | Se construye sobre la timeline de §1.2.2 (cámara, eventos, audio) |
| Monetización, logros, Steam | — | Fuera del alcance del motor (SDK de terceros) |
| Anti-cheat | N/A | Single-player local |

---

## 3. Roadmap por fases (M4–M6): top 10 en orden

El orden sigue las dependencias (los tipos antes que los widgets, el renderer antes que los efectos) y el valor para el usuario (UI propia lo antes posible). Cada ítem sigue las convenciones del proyecto: flag en `RenderSettings` y `--no-*` cuando cuesta GPU, tests doctest, MCP actualizado, docs en `docs/STUDIO.md` y coste medido en la HD 520 con `scripts/bench-windows.bat`.

### M4: "UI propia" (cimientos + editor)

**1. Tipos de valor y Attributes (F1 + P0 Attributes).** M.

- `PropType::{Vector2, UDim, UDim2, Rect, NumberRange, ColorSequence, NumberSequence, FontFace}`; `Value::n[4]`.
- Upgrader `.tefra` v2 (Position/PositionOffset → UDim2).
- Attributes y CollectionService.
- `Classes.cs` **generado** desde la reflexión.
- *Aceptación:*
  - un `.tefra` v1 (Obby) abre idéntico y se guarda como v2;
  - roundtrip de todas las clases en doctest;
  - Properties edita UDim2 (4 campos + "{s,o}") y secuencias (editor de stops);
  - C# `frame.Position = new UDim2(0.5f, 0, 1, -40)` compila y funciona;
  - el MCP lee y escribe UDim2 y attributes.

**2. TweenService nativo + secuencias C#.** M.

- *Aceptación:*
  - `TweenService.Create(part, new TweenInfo(1, EasingStyle.Back, EasingDirection.Out), new { Position = p })` y lo mismo sobre `BackgroundTransparency`, `Size` (UDim2) y `Color`;
  - Completed, Cancel, repeat/reverse y `await PlayAsync()`;
  - doctest de las 12 curvas de easing contra valores de referencia;
  - 1 000 tweens simultáneos por debajo de 0,3 ms CPU en el i5-6200U.

**3. Renderer de UI retained con SDF (F2) + fuentes TTF + texto rico.** L.

- Pase `ui` en el render graph, ubershader rounded-rect (relleno, borde, gradiente multi-stop, sombra, 9-slice, rotación) y clip con esquinas redondeadas anidado.
- Atlas SDF de fuentes del proyecto, `RichText`, `TextWrapped` y `MaxVisibleGraphemes`.
- *Aceptación:*
  - golden test GPU (lavapipe) de una escena UI de referencia;
  - texto nítido de 12 a 120 px;
  - `assets/fonts/X.ttf` se usa por FontFace y se copia al exportar;
  - 2 000 elementos por debajo de 0,5 ms GPU en la HD 520 (medido por el usuario);
  - 0 errores de validación;
  - `GameGui` queda como fallback (`--no-ui-renderer`);
  - las clases de la rama de UI (UICorner, UIStroke, UIGradient…) se dibujan con el nuevo renderer.

**4. Editor visual de UI en Studio.** L.

- Modo UI en el viewport (StarterGui visible en edición), selección, mover, redimensionar y rotar, snapping con guías, presets de ancla, toggle scale/offset, alinear y distribuir, vista previa de resolución y safe area, y undo.
- *Aceptación:*
  - construir sin teclear números un menú principal (logo, 3 botones, panel de ajustes) que se ve bien en 720p, 1080p, 1440p y 21:9;
  - todas las operaciones con undo/redo;
  - tests de `session` para mover, redimensionar y hacer snap;
  - herramienta MCP `gui_hit_test` + captura con la GUI.

**5. Widgets avanzados I: ScrollingFrame, ClipsDescendants, AutomaticSize, UIScale/ScaleMode, constraints de tamaño y aspecto, UIFlexLayout, estilos (StyleSheet/StyleRule con pseudo-estados y transiciones).** L.

- *Aceptación:*
  - plantilla "Tienda" (grid de 50 items con scroll, tarjetas con hover animado sin scripts, contador de monedas);
  - una StyleSheet cambia el tema de toda la UI en vivo;
  - foco con Tab/flechas y `:focus` visible.

### M5: "El mundo se ve y se siente de juego"

**6. Pase forward transparente + luces locales clustered + emissive.** M–L.

- *Aceptación:*
  - Parts con Transparency de 0,1 a 0,9 ordenadas sin artefactos con TAA;
  - Glass;
  - 64 PointLights en la escena de prueba a 720p Low dentro del presupuesto (medido);
  - 2 SpotLights con sombra;
  - Neon con bloom;
  - flags `--no-local-lights` y `--no-transparency`.

**7. Partículas, Beam y Trail + Attachment.** L.

- *Aceptación:*
  - presets en el Toolbox: fuego, humo, chispas, polvo de pisada, aura de pickup, estela de espada y láser (Beam);
  - `Emit(30)` desde C#;
  - 20 k partículas por debajo de 1 ms GPU en la HD 520;
  - editor de ColorSequence y NumberSequence con vista previa en vivo en Studio (sin Play).

**8. Constraints y assemblies + raycast/shapecast/CollisionGroups en C# + texturas/decals por parte + Camera scriptable.** L (se puede repartir entre 2 agentes).

- *Aceptación:*
  - plantillas de puerta con bisagra, plataforma móvil con Prismatic + servo, puente de cuerda y un coche simple (VehicleSeat);
  - un Model soldado cae como un solo cuerpo;
  - `Workspace.Raycast` con filtro desde C#;
  - Decal en una cara con StudsPerTile;
  - cámara de cutscene con tween de CFrame.

### M6: "Publicar un juego completo"

**9. Pipeline de assets + Prefabs/Packages + Toolbox + editor de C# con diagnósticos/autocompletado + "Open in VS Code".** L.

- *Aceptación:*
  - arrastrar `.glb/.png/.wav/.ttf` a Studio crea el objeto correcto y copia el asset;
  - "Save as Package" + actualizar 10 copias a la vez;
  - errores de Roslyn subrayados mientras se escribe;
  - autocompletar `Instance.`/`TweenService.` en menos de 150 ms;
  - el proyecto abre en VS Code con IntelliSense.

**10. Juego completo y empaquetado: DataStore local, UIAnimation (timeline), NavMesh + NPC básico, profiler, export con scripts precompilados y .NET incluido.** L.

- *Aceptación:* un juego de ejemplo **"Mazmorra"** de 15–20 minutos con:
  - menú principal animado (timeline), HUD propio (vida, minimapa con ViewportFrame, inventario con drag & drop) y pausa con blur;
  - enemigos con pathfinding;
  - guardado y carga;
  - partículas y luces;
  - opciones de gráficos, audio y controles con gamepad.
- Se exporta a una carpeta que corre en el PC del usuario **sin el SDK de .NET instalado**, a 30 fps o más en la HD 520 (Low, 720p TAAU).
- Profiler con zonas CPU/GPU por pase y por script.

### Riesgos y notas

- **Rendimiento HD 520:** cada efecto UI (blur, CanvasGroup, ViewportFrame) añade RTs y fill-rate. Necesitan límites por tier y flags. Hasta hoy, ningún coste de M2/M3 se ha medido en el hardware objetivo (CLAUDE.md), así que hay que exigirlo en cada ítem.
- **Migración de formato:** F1 cambia `.tefra`. Hay que hacerla antes de que la rama del importador de Roblox y las plantillas generen más contenido con el UDim2 partido.
- **Generar Classes.cs:** reduce mucho el coste de los ítems 1, 5, 7 y 8 (decenas de clases nuevas).
- **.NET en el PC del jugador:** hoy el export depende del SDK y Roslyn. Es bloqueante para "publicar" (ítem 10). Conviene adelantar la parte de precompilar scripts al exportar si el usuario quiere repartir builds pronto.
- **Ramas en curso:** al integrarlas, revisar los puntos **(⚠ rama)**:
  - UIGradient multi-stop;
  - List/Grid con measure/arrange;
  - BillboardGui con oclusión y MaxDistance;
  - eventos de input por GuiObject;
  - gamepad en UserInputService;
  - Touched/Died/Raycast y wrappers GUI en C#.
