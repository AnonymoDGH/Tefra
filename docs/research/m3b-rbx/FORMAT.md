# Formato de lugares y modelos de Roblox (resumen propio)

Estado: resumen escrito por nosotros a partir de la documentación pública del proyecto rbx-dom (licencia MIT):
`docs/binary.md` y `docs/xml.md` (rojo-rbx/rbx-dom). No se copia texto ni código de Roblox ni de terceros. Lo que
sigue es la descripción que usa `engine/rbx` (ADR-017: solo lugares propios del usuario, sin descargas de assets).

Archivos: `.rbxl` / `.rbxm` (binario) y `.rbxlx` / `.rbxmx` (XML). El lector detecta el formato por el contenido, no
por la extensión: el binario empieza por `<roblox!` seguido de `89 FF 0D 0A 1A 0A`; el XML por `<roblox` (tras BOM,
espacios o declaración `<?xml`).

## 1. Binario

### 1.1 Cabecera (32 bytes)

| Campo | Tamaño | Notas |
|---|---|---|
| Magic + firma | 14 | `<roblox!` + `89 FF 0D 0A 1A 0A` |
| Versión | u16 LE | 0 |
| Número de clases | i32 LE | número de chunks `INST` |
| Número de instancias | i32 LE | total |
| Reservado | 8 | ceros |

### 1.2 Chunks

Cada chunk tiene una cabecera de 16 bytes: nombre (4 bytes, relleno con ceros), longitud comprimida (u32 LE; 0 =
contenido sin comprimir), longitud descomprimida (u32 LE) y 4 bytes reservados. Después va el contenido.

- Compresión: **LZ4 en bloque** (sin cabecera de trama) o **zstd** (trama completa). Si los 4 primeros bytes del
  contenido son `28 B5 2F FD`, es zstd; si no, LZ4 de bloque. `END` nunca se comprime.
- Orden: `META` (0 o 1), `SSTR` (0 o 1), un `INST` por clase, un `PROP` por propiedad, `PRNT` (exactamente uno) y
  `END` (exactamente uno, contenido `</roblox>`). El lector acepta otro orden y avisa de los chunks desconocidos.

Contenidos:

- **META**: u32 número de pares, luego pares de cadenas (clave, valor). La clave conocida es `ExplicitAutoJoints`.
- **SSTR**: u32 versión (0), u32 número de entradas; cada entrada es un hash MD5 de 16 bytes (ignorable) y una cadena.
  Los valores de tipo SharedString son índices en esta tabla.
- **INST**: u32 id de clase (único), cadena con el nombre de la clase, u8 formato (1 = servicio, 0 = normal), u32 número
  de instancias, array de referentes y, si es servicio, un byte 1 por instancia.
- **PROP**: u32 id de clase, cadena con el nombre serializado (no siempre coincide con el nombre Lua), u8 id de tipo,
  y un valor por cada instancia de esa clase (todas tienen la propiedad).
- **PRNT**: u8 versión (0), u32 número, array de referentes de hijos y array de referentes de padres (-1 = raíz). Los
  pares se emparejan por índice; el orden de los hijos define el orden de hermanos.
- **END**: `</roblox>`.

### 1.3 Codificaciones de primitivas

- **Enteros transformados (zigzag)**: un valor no negativo x se guarda como 2x; uno negativo como 2|x| - 1. Se decodifica
  con `(v >> 1) ^ -(v & 1)`. Se usa para Int32 e Int64 (y para referentes, ver abajo).
- **Intercalado (interleaving)**: un array de N valores de w bytes big-endian se guarda por columnas: primero el byte
  más significativo de todos los valores, luego el segundo, etc. Es un layout pensado para la compresión.
- **Float de Roblox**: un Float32 se guarda con el bit de signo movido de arriba a abajo, es decir, el patrón de bits
  rotado a la izquierda una posición. Se deshace con una rotación a la derecha.
- **Referentes acumulados**: cada valor guardado es la diferencia con el referente anterior; el referente real es la
  suma acumulada (módulo 2^32). Los referentes del INST y del PRNT usan esta codificación.
- Los enteros little-endian son los escalares (longitudes, ids de clase, versiones).

### 1.4 Tabla de tipos de propiedad (id: tipo, codificación)

| Id | Tipo | Codificación |
|---|---|---|
| 01 | String | u32 longitud + bytes; sin intercalar |
| 02 | Bool | 1 byte |
| 03 | Int32 | zigzag, intercalado, big-endian |
| 04 | Float32 | float rotado, intercalado, big-endian |
| 05 | Float64 | IEEE-754 little-endian; sin intercalar |
| 06 | UDim | array de scales (Float32) y luego array de offsets (Int32) |
| 07 | UDim2 | arrays X.Scale, Y.Scale (Float32) y luego X.Offset, Y.Offset (Int32) |
| 08 | Ray | seis f32 LE (origen y dirección) |
| 09 | Faces | 1 byte, 6 bits bajos |
| 0A | Axes | 1 byte, 3 bits bajos |
| 0B | BrickColor | u32 big-endian, intercalado (número de la paleta) |
| 0C | Color3 | arrays R, G, B (Float32), cada uno intercalado |
| 0D | Vector2 | arrays X, Y (Float32) |
| 0E | Vector3 | arrays X, Y, Z (Float32) |
| 10 | CFrame | ver 1.5 |
| 12 | Enum | u32 big-endian, intercalado |
| 13 | Referent | zigzag acumulado, intercalado |
| 14 | Vector3int16 | tres i16 LE; sin intercalar |
| 15 | NumberSequence | u32 número de puntos, luego triples (tiempo, valor, envolvente) f32 |
| 16 | ColorSequence | u32 número de puntos, luego (tiempo, R, G, B, envolvente) f32 |
| 17 | NumberRange | dos f32 LE (mín, máx) |
| 18 | Rect | arrays min.x, min.y, max.x, max.y (Float32) |
| 19 | PhysicalProperties | u8 flags; si el bit 0 está activo, seis f32 (densidad, fricción, elasticidad, pesos de fricción y elasticidad, absorción acústica si el bit 1 está activo) |
| 1A | Color3uint8 | arrays R, G, B (u8), sin intercalar |
| 1B | Int64 | zigzag de 64 bits, big-endian, intercalado |
| 1C | SharedString | u32 big-endian, índices en SSTR, intercalado |
| 1D | Bytecode | igual que String (bytecode opaco) |
| 1E | OptionalCFrame | array de CFrame (tipo 10) y luego array de Bool que marca cuáles existen |
| 1F | UniqueId | u32 índice, u32 tiempo, i64 aleatorio, intercalado |
| 20 | Font | String familia, u16 peso LE, u8 estilo, String de cara en caché |
| 22 | Content | array de tipo de origen (Enum), luego URIs, referencias a objeto y contadores |

Los ids 0F, 11 y 21 no están documentados. Un tipo desconocido hace que su chunk `PROP` se omita con un aviso.

### 1.5 CFrame

Cada valor empieza con un byte de id.

- Id `00`: a continuación van nueve floats IEEE-754 sin transformar (la matriz de rotación R00..R22, row-major).
- Otros ids (24 en total): una tabla fija de rotaciones de ejes alineados, expresada en grados en orden Y, X, Z
  (por ejemplo `02` = identidad, `03` = (90, 0, 0), `14` = (0, 180, 0)).
- Después de todos los ids (y las matrices del id 00) viene el array de posiciones: X, Y, Z como arrays Float32
  intercalados.

## 2. XML (format version 4)

- Raíz `<roblox version="4">`. Hijos: `<Meta name="...">valor</Meta>` (pares clave-valor), `<External>` (sin efecto),
  `<SharedStrings>` (entradas `<SharedString md5="...">base64</SharedString>`) e `<Item>`.
- `<Item class="..." referent="...">`: una instancia. Los referentes son únicos y no son `null`. Los hijos se anidan
  como `<Item>` dentro del Item padre, junto a `<Properties>`.
- `<Properties>`: exactamente uno por Item. Cada hijo es un elemento de tipo con atributo `name`. Casi siempre la etiqueta
  es el tipo (`string`, `bool`, `int`, `float`, `double`, `Vector3`, `Color3`...), con estas excepciones: BrickColor es
  `int`; CFrame es `CoordinateFrame`; Enum es `token`; Rect es `Rect2D`. Conviene mapear por nombre de propiedad.
- Codificaciones de texto: bool `true`/`false`; enteros en decimal; `double` y `float` con notación decimal, admitiendo
  `INF`, `-INF` y `NAN`; `Color3uint8` es un entero de 32 bits con R, G, B en los 24 bits bajos (el orden exacto de
  los canales no está en la documentación que hemos leído: ver REPORT.md); `token` es un entero; `Ref` es el
  referente de destino o `null`; `SharedString` es el md5 de una entrada de SharedStrings; `ProtectedString` es texto
  literal (CDATA recomendado); `BinaryString` es base64.
- Tipos con hijos: `UDim` (`S`, `O`), `UDim2` (`XS`, `XO`, `YS`, `YO`), `Vector2/3` (`X`, `Y`, `Z`), `Color3` (`R`,
  `G`, `B`), `CoordinateFrame` (`X`, `Y`, `Z`, `R00`..`R22`), `Ray` (`origin`, `direction`), `Rect2D` (`min`, `max`),
  `PhysicalProperties` (`CustomPhysics` y seis floats), `Font` (`Family`, `Weight`, `Style`, `CachedFaceId`),
  `Content` (un hijo: `null`, `url` o `Ref`), `Optional...` (un hijo opcional de tipo CFrame).
- `NumberSequence` y `ColorSequence` son listas de números separados por espacios en grupos de 3 y de 5.
- `UniqueId` son 16 bytes en hexadecimal; la parte aleatoria va rotada respecto al binario.
- Escapes XML estándar (`&amp;`, `&lt;`, `&#x41;`...) y CDATA. El espacio en blanco sobra fuera de ProtectedString.

## 3. Consecuencias para el importador

- Los dos formatos producen el mismo árbol de instancias con valores tipados (`engine/rbx/include/tefra/rbx/document.h`).
- Los referentes se resuelven en dos pasos (primero se crean todas las instancias, luego se resuelven las referencias),
  porque una referencia puede apuntar a una instancia posterior en el fichero.
- Las rutas de datos (zigzag, intercalado, float rotado, referentes acumulados) se verifican con tests que construyen
  ficheros en código (`tests/unit/test_rbx.cpp`), no con ficheros de Roblox.
