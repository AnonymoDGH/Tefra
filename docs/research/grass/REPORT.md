# Hierba procedural (vertex-generated)

### Técnica
- **Sin vertex buffer.** Cada invocación calcula un vértice. Las briznas son cintas: 3 segmentos (7 vértices, 15 de triángulo) a menos de 12 m, 2 segmentos (5 vértices, 9 de triángulo) más lejos.
- **Forma.** Bézier cúbica en el plano (inclinación, arriba). Anchura `w(t) = W·(1-t)^0.8`. La punta es un triángulo degenerado.
- **Terreno.** Altura y gradiente del heightmap bindless; máscara (húmedo, parches). Tipos compartidos en `shaders/common/terrain_data.glsl`, separados de `terrain.glsl`.
- **Densidad.** `(1 - 0.9·húmedo) · mix(0.5, 1, parche) · suavizado de pendiente · LOD · desvanecido de borde`.
- **Viento.** Desplazamiento de punta `∝ t²`, ráfagas que recorren el valle y aleteo rápido. Amplitud 0,12 m.
- **Color y sombreado.** Verde oscuro a seco amarillento. AO de raíz (0,35 a 1). Normal de cinta mezclada con arriba al 50 %. Clase `1/3` con translucencia 0,55. Sin alpha test. Velocidad 0.
- **Culling.** Cuadrícula de 63x63 celdas. La CPU deja solo las celdas en el disco y el frustum (margen 1,3 m) y escribe la lista (cercanas primero). Los draws usan `firstInstance`.
- **Conteo.** `grass_count.comp` usa las mismas funciones que el vertex shader (`common/grass.glsl`) y cuenta con `atomicAdd`. Se lee con retardo de frames en vuelo.

### Parámetros
Radio 30 m. Inicio de LOD 12 m. 256 briznas candidatas por m² × `grass_density` (por defecto 1,5). Altura 0,18-0,62 m. Anchura 2,5-4,5 cm. Push constants 80 B.

### Medidas (valle de arranque)
- Celdas: 193 cercanas y 725 lejanas.
- Briznas candidatas dibujadas: 352.128. Visibles tras densidad: 124.773.
- Invocaciones de vértice por frame: ~3,6 M (el 65 % corresponde a briznas que la densidad descarta).
- Triángulos enviados (sin degenerados): ~1,2 M.

### Coste estimado en Intel HD 520 (tier Low)
No medido; lavapipe solo valida corrección. El cuello de botella son las invocaciones de vértice (4 lecturas de textura cada una) y el setup de triángulos diminutos. Orden de magnitud: 3-8 ms a 1280x720, solo la hierba. Incertidumbre alta. Palancas, en orden:
1. `grass_density` 1,0 → ~83k briznas (-30 % de coste); 0,5 → ~40k.
2. Compactación en GPU (cómputo que escribe la lista de briznas visibles y draw indirecto). Elimina el 65 % de invocaciones inútiles. Es la palanca más eficaz.
3. Cintas lejanas de 3 vértices en vez de 5.

Las cifras reales deben salir de `scripts/bench-windows.bat`.

### Limitaciones
- Sin sombras de hierba (era opcional).
- Viento sin velocidad: el TAA puede dejar ghosting leve en la punta.
- Conteo con retardo y solo con `grass_stats`.
- Culling por celda, no por brizna: el coste de vértice no baja con la densidad.
- Solapamiento: la escena sigue colocando `grass_medium_02` y helechos de Poly Haven. Hay que decidir si se retiran.
- Orientación de cara verificada visualmente (iluminación coherente), sin test automático.
- Sin golden específico de hierba.

### Hallazgos
- `patch` es palabra reservada en GLSL 4.60. Renombrada a `patchy`.
- **Con la proyección de Vulkan (`proj[1][1] < 0`) el radio de frustum salía negativo y se descartaban celdas cercanas fuera del eje central. Cualquier culling en CPU que use escalas de proyección debe tomar el valor absoluto.** Conviene revisar si hay otros tests así.
- `grass_counter` necesita `TRANSFER_SRC` además de `TRANSFER_DST`.
- `vertexPipelineStoresAndAtomics` no se fuerza en el dispositivo, así que el conteo va en cómputo.

### Fuentes y clean-room
Charla GDC 2021 (Sucker Punch) sobre hierba procedural en Ghost of Tsushima: solo la idea general (briznas en el vertex shader sin vertex buffer, celdas, Bézier, viento por ruido). Sin código, datos ni assets del juego. El resto son técnicas de dominio público; el hash ya estaba en el motor.

### Archivos
- Nuevos: `shaders/common/grass.glsl`, `shaders/common/terrain_data.glsl`, `shaders/grass.vert`, `shaders/grass.frag`, `shaders/grass_count.comp`, `docs/research/grass/{before,after}.png`.
- Modificados: `shaders/common/terrain.glsl`, `shaders/CMakeLists.txt`, `engine/renderer/src/renderer.cpp`, `engine/renderer/include/tefra/renderer/renderer.h`, `apps/common/{options.cpp,runner.cpp,runner.h}`, `apps/bench/main.cpp`.

### Siguientes pasos
1. Compactación de briznas en GPU con draw indirecto.
2. Decidir sobre la vegetación de Poly Haven que se solapa con la hierba.
3. Sombra de hierba en la cascada 0 (radio 12 m, solo cintas cercanas).
4. Velocidad con viento para el TAA.
5. `grass_density` por tier en `runner.cpp` (Low y High).
