# M3: avatares R15 y sistema de animación (ADR-016)

## API
- `anim::AnimatorSystem(DataModel&)`:
  - `update(dt)` cada frame;
  - `play(humanoid, ClipPtr|nombre, fade=0.2, weight=1, speed=1)`;
  - `stop(humanoid, nombre="", fade=0.2)`;
  - `is_playing(humanoid, nombre)`.
- `anim::build_r15_avatar(dm, parent, AvatarDesc)` devuelve el Model "Character".
  - Presets: Explorer, Stocky y Lanky.
  - `r15_hip_height(scale)`; `kJointNames` con los 15 Motor6D.
- `anim::builtin_clip(nombre)` y `builtin_clip_names()`. Formato JSON `.tefraanim` (`tefra-anim/1`).

## Clips integrados
| Clip | Duración | Bucle | Prioridad | Notas |
|---|---|---|---|---|
| idle | 3,5 s | sí | 0 | |
| walk | 1 s | sí | 0 | |
| run | 0,7 s | sí | 0 | |
| jump | 0,6 s | no | 1 | mantiene la última pose |
| fall | 1,2 s | sí | 1 | |
| climb | 1,4 s | sí | 1 | |
| wave | 2,4 s | no | 2 | solo brazo derecho (emote) |

Convención: el avatar mira a −Z, la derecha es +X y un pitch positivo lleva la pierna o el brazo hacia delante.

## Composición
1. Velocidad xz de `HumanoidRootPart.AssemblyLinearVelocity`. De ella salen los pesos de idle, walk y run (walk↔run entre 3 y 5 m/s); walk y run comparten la fase de la marcha.
2. `Humanoid.State`: Jumping → jump, Freefall → fall, Climbing → climb.
3. Crossfade de 0,2 s y capas por prioridad: la capa superior cubre la inferior en proporción a su peso.
4. Se escribe desde la raíz recorriendo los Motor6D de padre a hijo: `Part1 = Part0 * C0 * T(anim) * inv(C1)`. Motor6D gana las propiedades `C0Orientation` y `C1Orientation`.

## Avatar
- 16 Parts: HumanoidRootPart invisible + 15 de cuerpo.
- 15 Motor6D, un Humanoid (HipHeight = 0,89·escala) y un Animator.
- Mide 1,80 m con escala 1.

## Tests
Hay 8 casos. Comprueban:
- la estructura del avatar;
- que las articulaciones cierran en reposo;
- que los pies quedan en el suelo;
- que el walk es periódico;
- que el crossfade es continuo;
- que el emote no toca las piernas;
- que los estados jump y fall se respetan;
- el round trip por JSON.

`tefra_avatar_demo --validation` da 0 errores.

## Limitaciones y siguientes pasos
- `update` recorre el árbol completo cada frame: falta una caché de Humanoids.
- No hay cara (decal).
- Pendiente: skinning glTF en GPU para personajes importados.
