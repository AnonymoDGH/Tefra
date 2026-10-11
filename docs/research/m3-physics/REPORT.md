# M3: física con Jolt (ADR-014)

## Integración
Jolt Physics v5.6.0 (MIT), vendorizado en `third_party/jolt`. Configuración de build:
- SIMD hasta SSE4.2 (x86-64-v2);
- sin debug renderer ni profiler;
- `-fno-exceptions -fno-rtti`;
- determinismo entre plataformas desactivado.

## API (`engine/physics`)
- `physics::PhysicsWorld(world::DataModel&)`:
  - `step(dt)`: subpasos fijos de 1/60 s, máximo 8 por llamada;
  - `sync()`;
  - `raycast(origin, dir, max, RaycastResult&)`;
  - `add_touched_listener(fn)` / `remove_touched_listener(h)`;
  - `has_body`, `body_count`.
- Mapeo de BaseParts a cuerpos:
  - Anchored → cuerpo estático; si no, dinámico con masa = Density × volumen, Friction y Elasticity.
  - CanCollide=false → sensor.
  - Shape → forma Jolt (Block, Ball, Cylinder con eje Y, Wedge como convex hull). MeshPart usa una caja de su Size.
  - Write-back de Position, Orientation y AssemblyLinearVelocity.
  - Gravity y FallenPartsDestroyHeight se leen del Workspace.
- `physics::CharacterController(PhysicsWorld&, Instance& model)`:
  - `CharacterVirtual` con cápsula; WalkSpeed y JumpHeight del Humanoid.
  - Escalones de 0,4 m y pendientes de hasta 45°.
  - AutoRotate y `Humanoid.State` (Idle, Running, Jumping, Freefall, Landed).
  - Empuja cuerpos dinámicos.

## Enganche en Play
`on_play_start` crea el `PhysicsWorld` y los personajes; `on_play_step` llama a `step(dt)`; `on_play_stop` destruye primero los personajes y luego el mundo.

## Tests
9 casos:
- caída sobre el suelo;
- Touched por contacto y por sensor;
- bola rodando por una rampa;
- torre derribada por una bola;
- raycast;
- teletransporte y destrucción;
- el personaje camina, salta 1,65 m (objetivo 1,6) y sube escalones.

`tefra_physics_demo --validation` da 0 errores. Capturas: `tower_impact.png` y `tower_settled.png`.

## Limitaciones
- Sin WeldConstraint ni articulaciones físicas.
- MeshPart usa una caja como colisión.
- La cápsula del personaje no cambia de tamaño tras crearse.
- No determinista entre plataformas.
