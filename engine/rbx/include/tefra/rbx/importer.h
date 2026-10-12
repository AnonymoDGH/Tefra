#pragma once

// Roblox place / model importer (ADR-017). User-authorized exception: it is meant for the user's OWN places, to
// migrate them to Tefra. Clean-room from public format documentation; no Roblox code; asset references
// (rbxassetid://...) are kept as text and never downloaded.
//
// Formats: binary .rbxl / .rbxm and XML .rbxlx / .rbxmx (auto-detected from the content, not the extension).
//
// Conversion (units: 1 stud = ImportOptions::stud_to_meters metres; Roblox and Tefra share +Y up, -Z forward):
//   - Services: Workspace, Lighting, ReplicatedStorage, ServerScriptService, StarterPlayer, StarterGui, Players and
//     SoundService map to the Tefra services. Other services (ServerStorage, Teams, StarterPack...) become a Folder
//     placeholder under ReplicatedStorage; empty ones are skipped.
//   - Native classes: Part (Block/Ball/Cylinder/Wedge), SpawnLocation, MeshPart, Model (PrimaryPart), Folder, lights
//     (PointLight, SpotLight; SurfaceLight as PointLight), Sound, Animator, Animation, Humanoid, Camera, Motor6D, Weld,
//     WeldConstraint, Attachment, Decal, Texture, Team, the Value classes (StringValue, IntValue, NumberValue,
//     BoolValue, Vector3Value, Color3Value, ObjectValue, CFrameValue), ScreenGui, BillboardGui, SurfaceGui, Frame,
//     TextLabel, TextButton, TextBox, ImageLabel, ImageButton, UICorner (sets CornerRadius), UIStroke, UIPadding,
//     UIListLayout, UIGridLayout, UIGradient, UIAspectRatioConstraint, ParticleEmitter, SpecialMesh (absorbed by its
//     part: its properties are kept as rbx.SpecialMesh.* attributes).
//   - Approximated (nearest Tefra class, RobloxClass attribute + rbx.* properties): Seat, VehicleSeat, TrussPart,
//     CornerWedgePart, UnionOperation, NegateOperation, WedgePart, Tool / Accessory / Hat (Model), ScrollingFrame and
//     CanvasGroup / ViewportFrame (Frame), SurfaceLight (PointLight), MeshPart without a file.
//   - Placeholder: any other class (and voxel Terrain) becomes a Folder with the same name, the attribute RobloxClass
//     holding the original class name, and every readable property as rbx.<Property> (numbers, bools, strings,
//     Vector3 and Color3 keep their type; the rest is its JSON text; blobs truncated). Children are kept.
//   - Lighting effects (Atmosphere, Sky, Clouds, BloomEffect, ColorCorrectionEffect, SunRaysEffect,
//     DepthOfFieldEffect, BlurEffect) have no Tefra class: their properties are attributes rbx.<Class>.<Property> of
//     Lighting and their children move up to Lighting. Lighting properties Tefra lacks are rbx.<Property>.
//   - Roblox Attributes (AttributesSerialize) become Tefra attributes, CollectionService Tags become tags. Attribute
//     blobs with an unknown layout are kept as rbx.AttributesSerialize. The layout is not verified against Roblox
//     files (docs/research/rbx-import/REPORT.md).
//   - Scripts: Script / LocalScript / ModuleScript keep the Luau in LuauSource; Source is a C# stub: the Luau as a
//     comment block and an empty class (TefraScript subclass, Start override). Imported disabled (not for ModuleScript,
//     which has no Enabled and is only compiled).
//   - Assets: MeshId (MeshPart, FileMesh), SoundId and Decal / Texture images become a file path when the file exists
//     in ImportOptions::assets_dir (default "<place dir>/<place stem>_assets", files "<id>.glb|.gltf" for meshes,
//     "<id>.ogg|mp3|wav|flac" for sounds, "<id>.png|jpg|jpeg" for images). Otherwise the rbxassetid:// text is kept.
//   - Part properties: CFrame -> Position + Orientation, Size, Color3uint8 / BrickColor -> Color, Material -> nearest
//     Tefra material, Transparency, Reflectance, Anchored, CanCollide, CanTouch, CastShadow, Massless and custom
//     physical properties. Roblox cylinders run along X and Tefra's along Y: the importer rotates them by Rz(-90 deg).
//   - Workspace.Gravity and FallenPartsDestroyHeight, StarterPlayer character settings, Players.RespawnTime and
//     Lighting (ClockTime / TimeOfDay, GeographicLatitude, ExposureCompensation, fog end -> FogDensity, Clouds.Cover
//     -> CloudCover) where Tefra has an equivalent.

#include "tefra/core/json_value.h"
#include "tefra/rbx/document.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace tefra::world {
class DataModel;
class Instance;
} // namespace tefra::world

namespace tefra::rbx {

struct ImportOptions {
    bool convert_scripts_disabled = true; // imported scripts get Enabled = false (their C# is only a skeleton)
    f32 stud_to_meters = 0.28f;           // length scale; speeds, gravity and distances scale the same way
    // Folder with the assets exported next to the place: "<dir>/<asset id>.glb" (meshes), ".ogg/.mp3/.wav/.flac"
    // (sounds), ".png/.jpg" (images). Empty = "<place dir>/<place stem>_assets" when the importer reads a file.
    std::string assets_dir;
    // Also keep every property the converter does not map on native classes, as rbx.<Prop> attributes (off by default:
    // large places would carry dozens of extra attributes per part). Placeholders and approximations always keep them.
    bool keep_all_properties = false;
};

struct ImportReport {
    u32 instances = 0;                 // instances decoded from the file
    u32 converted = 0;                 // turned into a native Tefra class (services merged count as converted)
    u32 imported_as_unsupported = 0;   // kept as a placeholder (a Folder with RobloxClass + rbx.* attributes)
    u32 approximated = 0;              // converted to the nearest Tefra class (RobloxClass attribute kept)
    u32 scripts = 0;                   // Script / LocalScript / ModuleScript converted (subset of `converted`)
    u32 attributes = 0;                // Roblox Attributes copied to Tefra attributes
    u32 tags = 0;                      // CollectionService tags copied
    u32 assets_linked = 0;             // MeshId / SoundId / image references resolved to a file in assets_dir
    std::vector<std::string> warnings; // de-duplicated, "message (xN)" when repeated
    std::unordered_map<std::string, u32> unsupported_classes; // original class -> placeholder count
    std::unordered_map<std::string, u32> class_counts;        // original class -> decoded instances (all of them)
};

// Replaces the content of the place (like DataModel::load): every child of every service is destroyed and the
// service properties are reset to their defaults, then the file is converted. The model is only touched once the
// file has been fully decoded: on a parse error it returns false and `dm` is unchanged.
bool import_place(const char* path, world::DataModel& dm, const ImportOptions& options = {},
                  ImportReport* report = nullptr, std::string* error = nullptr);
bool import_place_memory(const u8* data, usize size, world::DataModel& dm, const ImportOptions& options = {},
                         ImportReport* report = nullptr, std::string* error = nullptr);

// Inserts the instances of a model file (.rbxm / .rbxmx; a place file works too) under `parent`, which must belong
// to a DataModel. `roots` (optional) receives the top-level instances created, in file order (for selection and
// undo in Studio). On a parse error nothing is created.
bool import_model(const char* path, world::Instance* parent, const ImportOptions& options = {},
                  ImportReport* report = nullptr, std::string* error = nullptr,
                  std::vector<world::Instance*>* roots = nullptr);
bool import_model_memory(const u8* data, usize size, world::Instance* parent, const ImportOptions& options = {},
                         ImportReport* report = nullptr, std::string* error = nullptr,
                         std::vector<world::Instance*>* roots = nullptr);

// {"instances", "converted", "imported_as_unsupported", "approximated", "scripts", "attributes", "tags",
//  "assets_linked", "warnings": [...], "unsupported_classes": {"Class": count, ...} (placeholders),
//  "class_counts": {"Class": count, ...}} (classes sorted by count, then name).
JsonValue report_to_json(const ImportReport& report);
// One line: "1234 instances: 1100 converted (12 scripts), 134 kept as placeholders, 5 warnings".
std::string report_summary(const ImportReport& report);

} // namespace tefra::rbx
