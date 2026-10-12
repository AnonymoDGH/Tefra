# Importador de Roblox: cobertura (2026-10-12)

Solo para lugares propios del usuario (ADR-017).

## Qué se importa
- **Clases nativas, nuevas o mapeadas:** Attachment, Decal, Texture, Weld, WeldConstraint, SurfaceGui, BillboardGui, TextBox, Team, los *Value, los modificadores de UI y SpecialMesh (absorbido por la Part).
- **Clases desconocidas:** pasan a un Folder del mismo nombre con el atributo `RobloxClass` y cada propiedad como `rbx.<Prop>`. Los hijos se conservan.
- **Clases aproximadas** (con `RobloxClass` y `rbx.*`): Seat, TrussPart, CornerWedgePart, UnionOperation/NegateOperation (Part con su caja), Tool/Accessory (Model), ScrollingFrame (Frame) y SurfaceLight (PointLight).
- **Efectos de Lighting** (Atmosphere, Sky, Bloom, ColorCorrection, Clouds…): sus propiedades quedan como `rbx.<Clase>.<Prop>` en Lighting.
- **Atributos y etiquetas** (AttributesSerialize, Tags): pasan a los atributos y etiquetas de Tefra.
- **Scripts:**
  - `LuauSource` guarda el código original.
  - `Source` es un esqueleto C# que compila, con el Luau como comentario.
  - Se importan con `Enabled = false`.
- **Assets:** MeshId, SoundId e imágenes apuntan a `<lugar>_assets/<id>.(glb|ogg|png…)` si el fichero existe.
- **Informe:** recuentos por clase, placeholders, clases aproximadas, atributos, etiquetas y assets enlazados.
- **`--all-properties`:** conserva también las propiedades no mapeadas de las clases nativas.

## Supuestos sin verificar con un archivo real
- Los ids de tipo dentro de AttributesSerialize.
- El orden de caras de Roblox.
- Que los Tags vayan separados por NUL.

## Pendiente
- Terreno voxel (SmoothGrid): se guarda el blob pero no se convierte.
- Geometría de uniones CSG y de mallas sin fichero.
- Solver de constraints.
- Render de Decal, SurfaceGui y BillboardGui.
- Conversión de animaciones (KeyframeSequence).
