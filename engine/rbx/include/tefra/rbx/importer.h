#pragma once

// Roblox place / model importer (ADR-017). User-authorized exception: it is meant for the user's OWN places, to
// migrate them to Tefra. Clean-room from public format documentation; no Roblox code; asset references
// (rbxassetid://...) are kept as text and never downloaded.
//
// Formats: binary .rbxl / .rbxm and XML .rbxlx / .rbxmx (auto-detected from the content, not the extension).
//
// Conversion (units: 1 stud = ImportOptions::stud_to_meters metres; Roblox and Tefra share +Y up, -Z forward):
//   - Services: Workspace, Lighting, ReplicatedStorage, ServerScriptService, StarterPlayer, StarterGui, Players and
//     SoundService map to the Tefra services. Other services with content (ServerStorage, ReplicatedFirst,
//     StarterPack...) become an ImportedInstance under ReplicatedStorage; empty ones are skipped.
//   - Part (Block/Ball/Cylinder/Wedge), WedgePart, SpawnLocation, MeshPart (MeshId kept as text), Seat / TrussPart /
//     CornerWedgePart / UnionOperation (nearest Part shape, with a warning). CFrame -> Position + Orientation,
//     Size, Color3uint8 / BrickColor -> Color, Material -> nearest Tefra material, Transparency, Reflectance,
//     Anchored, CanCollide, CanTouch, CastShadow, Massless, custom physical properties. Roblox cylinders run along
//     X and Tefra's along Y: the importer rotates them by Rz(-90 deg) and swaps Size.x / Size.y.
//   - Model (PrimaryPart), Folder, PointLight / SpotLight (Range scaled), Sound (SoundId kept as text, roll-off
//     distances scaled), Animator, Animation, Humanoid (speeds and heights scaled), Camera, Motor6D.
//   - ScreenGui, Frame, TextLabel, TextButton, ImageLabel, ImageButton (UDim2 split into scale + pixel offset,
//     colours, transparency, text properties, ZIndex, Visible, AnchorPoint); a UICorner child sets CornerRadius.
//   - Script / LocalScript / ModuleScript: the Luau code goes to LuauSource; Source becomes a C# skeleton (a
//     TefraScript subclass with a unique name, the Luau as a comment); the script is imported disabled.
//   - Workspace.Gravity and FallenPartsDestroyHeight, StarterPlayer character settings, Players.RespawnTime and
//     Lighting (ClockTime / TimeOfDay, GeographicLatitude, ExposureCompensation, fog end -> FogDensity, Clouds.Cover
//     -> CloudCover) where Tefra has an equivalent.
//   - Everything else (and voxel Terrain) becomes an ImportedInstance: OriginalClass + OriginalProperties (JSON with
//     every decoded property, typed; binary blobs as base64 truncated to 4 KB), so children are preserved.

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
};

struct ImportReport {
    u32 instances = 0;               // instances decoded from the file
    u32 converted = 0;               // turned into a native Tefra class (services merged count as converted)
    u32 imported_as_unsupported = 0; // kept as ImportedInstance
    u32 scripts = 0;                 // Script / LocalScript / ModuleScript converted (subset of `converted`)
    std::vector<std::string> warnings; // de-duplicated, "message (xN)" when repeated
    std::unordered_map<std::string, u32> unsupported_classes; // original class -> ImportedInstance count
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

// {"instances", "converted", "imported_as_unsupported", "scripts", "warnings": [...],
//  "unsupported_classes": {"Class": count, ...}} (classes sorted by count, then name).
JsonValue report_to_json(const ImportReport& report);
// One line: "1234 instances: 1100 converted (12 scripts), 134 kept as ImportedInstance, 5 warnings".
std::string report_summary(const ImportReport& report);

} // namespace tefra::rbx
