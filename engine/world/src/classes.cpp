// Built-in classes of the DataModel (spec M3 §2, ADR-012). Units: metres, seconds, degrees for Orientation.
#include "tefra/world/datamodel.h"

namespace tefra::world {

namespace {
struct Builder {
    ClassRegistry& r;
    ClassInfo* c = nullptr;
    std::string cat = "Data";

    Builder& cls(const char* name, const char* base, const char* doc, const char* insert_cat = "") {
        c = &r.add(name, base);
        c->doc = doc;
        c->category = insert_cat;
        cat = "Data";
        return *this;
    }
    Builder& abstract() {
        c->creatable = false;
        c->is_abstract = true;
        return *this;
    }
    Builder& engine_only() { // concrete, but created by the engine only (not in the Insert menu / Instance.new)
        c->creatable = false;
        return *this;
    }
    Builder& service() {
        c->service = true;
        c->creatable = false;
        return *this;
    }
    Builder& group(const char* g) {
        cat = g;
        return *this;
    }
    Builder& prop(const char* name, Value def, const char* doc, u32 flags = 0, const EnumInfo* e = nullptr) {
        PropInfo p;
        p.name = name;
        p.type = def.type;
        p.def = std::move(def);
        p.doc = doc;
        p.flags = flags;
        p.enum_info = e;
        p.category = cat;
        c->props.push_back(std::move(p));
        return *this;
    }
};
} // namespace

void register_builtin_classes(ClassRegistry& r) {
    const EnumInfo* material = r.add_enum(
        "Material", {"Plastic", "SmoothPlastic", "Wood",    "WoodPlanks", "Metal", "DiamondPlate", "Concrete",
                     "Brick",   "Cobblestone",   "Granite", "Marble",     "Slate", "Sand",         "Grass",
                     "Ground",  "Rock",          "Ice",     "Glass",      "Neon",  "Fabric",       "Foil"});
    const EnumInfo* shape = r.add_enum("PartType", {"Block", "Ball", "Cylinder", "Wedge"});
    const EnumInfo* terrain_kind = r.add_enum("TerrainKind", {"None", "Valley", "Coast", "Plains"});
    const EnumInfo* script_ctx = r.add_enum("RunContext", {"Server", "Client"});
    const EnumInfo* humanoid_state = r.add_enum("HumanoidState", {"Idle", "Walking", "Running", "Jumping", "Freefall",
                                                                  "Landed", "Dead", "Seated", "Climbing", "Swimming"});
    const EnumInfo* normal_id = r.add_enum("NormalId", {"Top", "Bottom", "Front", "Back", "Left", "Right"});
    const EnumInfo* particle_shape = r.add_enum("ParticleEmitterShape", {"Box", "Sphere", "Disc", "Point"});
    const EnumInfo* rig = r.add_enum("RigType", {"R15", "Custom"});
    const EnumInfo* text_x = r.add_enum("TextXAlignment", {"Left", "Center", "Right"});
    const EnumInfo* text_y = r.add_enum("TextYAlignment", {"Top", "Center", "Bottom"});
    const EnumInfo* scale_type = r.add_enum("ScaleType", {"Stretch", "Fit", "Crop"});
    const EnumInfo* font = r.add_enum("Font", {"Default", "Bold", "Mono"});
    const EnumInfo* h_align = r.add_enum("HorizontalAlignment", {"Left", "Center", "Right"});
    const EnumInfo* v_align = r.add_enum("VerticalAlignment", {"Top", "Center", "Bottom"});
    const EnumInfo* fill_dir = r.add_enum("FillDirection", {"Vertical", "Horizontal"});
    const EnumInfo* sort_order = r.add_enum("SortOrder", {"LayoutOrder", "Name"});
    const EnumInfo* display_dist = r.add_enum("HumanoidDisplayDistanceType", {"Viewer", "Subject", "None"});

    Builder b{r};
    // Every class starts with Name (property index 0).
    b.cls("Instance", nullptr, "Base class of everything in the DataModel.").abstract();
    b.prop("Name", Value::string("Instance"), "Name used in paths (Workspace.Model.Part).");
    b.cls("DataModel", "Instance", "The root of a place: 'game'.").abstract();

    // ---- Services ----------------------------------------------------------------------------------------
    b.cls("Workspace", "Instance", "Everything that exists in the 3D world.").service();
    b.group("Physics").prop("Gravity", Value::number(9.81), "Gravitational acceleration (m/s^2).");
    b.prop("FallenPartsDestroyHeight", Value::number(-500.0), "Unanchored parts below this height are destroyed.");
    b.group("Camera").prop("CurrentCamera", Value::reference(0), "Camera used by the player.");

    b.cls("Lighting", "Instance", "Sun, sky, fog and exposure.").service();
    b.group("Appearance").prop("ClockTime", Value::number(14.0), "Hour of the day (0-24): sets the sun position.");
    b.prop("GeographicLatitude", Value::number(20.0), "Latitude in degrees (sun path).");
    b.prop("SunAzimuthOffset", Value::number(0.0), "Rotates the sun path around the vertical axis (degrees).");
    b.prop("CloudCover", Value::number(0.3), "Cumulus coverage (0 = clear).");
    b.prop("FogDensity", Value::number(0.0), "Height fog density at sea level (1/m).");
    b.prop("FogHeightFalloff", Value::number(0.03), "Height fog falloff (1/m).");
    b.prop("ExposureCompensation", Value::number(0.0), "EV added to the automatic exposure.");
    b.prop("Saturation", Value::number(1.05), "Colour grading saturation.");
    b.prop("Contrast", Value::number(1.05), "Colour grading contrast.");

    b.cls("ReplicatedStorage", "Instance", "Storage shared by server and clients (templates, modules).").service();
    b.cls("ServerScriptService", "Instance", "Server scripts that are not attached to world objects.").service();
    b.cls("StarterPlayer", "Instance", "Settings for player characters.").service();
    b.group("Character").prop("CharacterWalkSpeed", Value::number(6.0), "Default walk speed (m/s).");
    b.prop("CharacterJumpHeight", Value::number(1.6), "Default jump height (m).");
    b.prop("CharacterRig", Value::enumeration(0), "Default avatar rig.", 0, rig);
    b.prop("CharacterMesh", Value::string(""), "Rigged .glb used as the character (empty = the default R15 avatar).");
    b.prop("CharacterMeshScale", Value::number(1.0), "Scale of the imported character.");
    b.prop("CameraMaxZoomDistance", Value::number(40.0), "Maximum third-person camera distance (m).");
    b.cls("StarterGui", "Instance", "UI cloned into each player's screen.").service();
    b.group("Behavior").prop("ShowDevelopmentGui", Value::boolean(true), "Shows the UI in the editor viewport.");
    b.cls("Players", "Instance", "The players in a running game (one, the local player, in Studio).").service();
    b.group("Data").prop("RespawnTime", Value::number(3.0), "Seconds before a dead character respawns.");
    b.cls("SoundService", "Instance", "Global sound settings.").service();
    b.group("Data").prop("Volume", Value::number(1.0), "Master volume (0-1).");
    b.prop("RolloffScale", Value::number(1.0), "Scales the distance attenuation of every 3D sound.");
    b.prop("CharacterSounds", Value::boolean(true),
           "Plays jump, landing, footstep and death sounds for the local character.");

    // ---- Containers ----------------------------------------------------------------------------------------
    b.cls("Folder", "Instance", "Groups instances without any behaviour.", "Containers");
    // Created at play start under Players (not insertable).
    b.cls("Player", "Instance", "A player connected to the game.").engine_only();
    b.group("Data").prop("DisplayName", Value::string("Player"), "Name shown to other players.");
    b.prop("Character", Value::reference(0), "The player's avatar Model (nil while dead).", kPropNotSerialized);
    b.prop("UserId", Value::integer(1), "Player id (1 for the local player).", kPropReadOnly);
    b.prop("RespawnLocation", Value::reference(0), "SpawnLocation used when the character respawns (checkpoints).");
    b.cls("PlayerGui", "Instance", "The player's screen UI (StarterGui is copied here at spawn).").engine_only();
    b.cls("PlayerScripts", "Instance", "The player's client scripts (StarterPlayerScripts is copied here).")
        .engine_only();
    b.cls("StarterPlayerScripts", "Instance", "LocalScripts that run for each player.", "Containers");
    b.cls("StarterCharacterScripts", "Instance", "LocalScripts copied into each character when it spawns.",
          "Containers");
    // Kept by importers for content Tefra cannot represent yet (GUI, constraints, effects...).
    b.cls("ImportedInstance", "Instance", "An object imported from another tool that Tefra does not support yet.")
        .engine_only();
    b.group("Data").prop("OriginalClass", Value::string(""), "Class name in the source file.", kPropReadOnly);
    b.prop("OriginalProperties", Value::string(""), "Source properties as JSON (kept for later conversion).",
           kPropReadOnly | kPropHidden);
    b.cls("Model", "Instance", "A group of parts that moves as one (e.g. a car, a character).", "Containers");
    b.prop("PrimaryPart", Value::reference(0), "Part that defines the model's pivot.");

    // ---- Parts ---------------------------------------------------------------------------------------------
    b.cls("BasePart", "Instance", "A physical object in the world.").abstract();
    b.group("Transform").prop("Position", Value::vector3({0, 0, 0}), "Centre in world space (m).");
    b.prop("Orientation", Value::vector3({0, 0, 0}), "Rotation in degrees (applied Y, then X, then Z).");
    b.prop("Size", Value::vector3({4, 1, 2}), "Dimensions (m).");
    b.group("Appearance").prop("Color", Value::color3({0.64f, 0.64f, 0.64f}), "Base colour (sRGB, 0-1).");
    b.prop("Material", Value::enumeration(0), "Surface material.", 0, material);
    b.prop("Transparency", Value::number(0.0), "0 = opaque, 1 = invisible.");
    b.prop("Reflectance", Value::number(0.0), "Extra mirror-like reflection (0-1).");
    b.prop("CastShadow", Value::boolean(true), "Casts shadows from the sun.");
    b.group("Behavior").prop("Anchored", Value::boolean(false), "Fixed in place (not moved by physics).");
    b.prop("CanCollide", Value::boolean(true), "Other parts and characters collide with it.");
    b.prop("CanTouch", Value::boolean(true), "Fires Touched events.");
    b.prop("Massless", Value::boolean(false), "Adds no mass to its assembly.");
    b.group("Physics").prop("Density", Value::number(700.0), "kg/m^3 (default: wood-like plastic).");
    b.prop("Friction", Value::number(0.5), "Coulomb friction coefficient.");
    b.prop("Elasticity", Value::number(0.3), "Restitution (bounciness).");
    b.prop("AssemblyLinearVelocity", Value::vector3({0, 0, 0}), "Velocity (m/s).", kPropNotSerialized);

    b.cls("Part", "BasePart", "A primitive shape: block, ball, cylinder or wedge.", "Parts");
    b.group("Data").prop("Shape", Value::enumeration(0), "Primitive shape.", 0, shape);
    b.cls("MeshPart", "BasePart", "A part whose shape comes from a glTF mesh.", "Parts");
    b.group("Data").prop("MeshId", Value::string(""), "Path of the .gltf/.glb asset.");
    b.prop("TextureId", Value::string(""), "Optional albedo texture override.");
    b.cls("SpawnLocation", "Part", "Where player characters appear.", "Gameplay");
    b.group("Data").prop("Enabled", Value::boolean(true), "Players can spawn here.");
    b.prop("Neutral", Value::boolean(true), "Any team can spawn here.");

    // ---- Environment -----------------------------------------------------------------------------------------
    b.cls("Terrain", "Instance", "Procedural landscape (one per Workspace).", "Environment");
    b.group("Data").prop("Kind", Value::enumeration(0), "Landscape generator.", 0, terrain_kind);
    b.prop("Seed", Value::integer(1337), "Generator seed.");
    b.prop("SeaLevel", Value::number(0.0), "Coast: height of the still sea (m).");
    b.prop("Vegetation", Value::boolean(true), "Scatter trees / palms / rocks.");
    b.cls("Ocean", "Instance", "Animated ocean surface (needs a Coast terrain for surf).", "Environment");
    b.group("Data").prop("SwellHeight", Value::number(0.9), "Significant height of the open-sea swell (m).");
    b.prop("SurfHeight", Value::number(0.7), "Height of the breaking waves at the shore (m).");
    b.prop("WindDirection", Value::vector3({0.18f, 0, 1}), "Direction the swell travels (xz).");
    b.prop("WaterColor", Value::color3({0.0f, 0.35f, 0.45f}), "Tint of the scattered light in the water body.");

    // ---- Lights ----------------------------------------------------------------------------------------------
    b.cls("Light", "Instance", "Base class of local lights (parent them to a part).").abstract();
    b.group("Appearance").prop("Color", Value::color3({1, 0.95f, 0.85f}), "Light colour.");
    b.prop("Brightness", Value::number(2.0), "Intensity multiplier.");
    b.prop("Range", Value::number(16.0), "Reach (m).");
    b.prop("Shadows", Value::boolean(false), "Casts shadows.");
    b.prop("Enabled", Value::boolean(true), "Is on.");
    b.cls("PointLight", "Light", "Omnidirectional light.", "Lights");
    b.cls("SpotLight", "Light", "Cone light along the part's -Z (front) face.", "Lights");
    b.group("Appearance").prop("Angle", Value::number(45.0), "Cone angle (degrees).");

    // ---- Scripts ---------------------------------------------------------------------------------------------
    b.cls("BaseScript", "Instance", "C# code that runs in play mode.").abstract();
    b.group("Behavior").prop("Enabled", Value::boolean(true), "Runs when the game starts.");
    b.prop("Source", Value::string(""), "C# source code.", kPropHidden);
    b.prop("LuauSource", Value::string(""), "Original Luau code of an imported Roblox script (reference only).",
           kPropHidden);
    b.cls("Script", "BaseScript", "Server-side C# script (runs once per game).", "Scripts");
    b.group("Behavior").prop("RunContext", Value::enumeration(0), "Where the script runs.", 0, script_ctx);
    b.cls("LocalScript", "BaseScript", "Client-side C# script (input, camera, UI).", "Scripts");
    b.cls("ModuleScript", "Instance", "Shared C# code used by other scripts.", "Scripts");
    b.group("Behavior").prop("Source", Value::string(""), "C# source code.", kPropHidden);
    b.prop("LuauSource", Value::string(""), "Original Luau code of an imported Roblox module (reference only).",
           kPropHidden);

    // ---- In-game UI (positions/sizes: scale in X/Y of the parent + pixel offset) ----------------------------
    b.cls("GuiBase", "Instance", "Base class of 2D user interface objects.").abstract();
    b.group("Behavior").prop("Visible", Value::boolean(true), "Drawn and clickable.");
    b.prop("ZIndex", Value::integer(1), "Higher draws on top of siblings.");
    b.cls("ScreenGui", "GuiBase", "A layer of UI drawn over the 3D view.", "UI");
    b.group("Behavior").prop("Enabled", Value::boolean(true), "Drawn when true.");
    b.prop("DisplayOrder", Value::integer(0), "Higher ScreenGuis draw on top.");
    b.prop("ResetOnSpawn", Value::boolean(true), "Copied again from StarterGui when the character respawns.");
    b.cls("BillboardGui", "GuiBase", "UI drawn in 3D space above an Adornee and facing the camera.", "UI");
    b.group("Behavior").prop("Enabled", Value::boolean(true), "Drawn when true.");
    b.prop("Adornee", Value::reference(0), "Part the billboard follows (nil = the parent part).");
    b.prop("Size", Value::vector3({200, 50, 0}), "Size on screen (pixels, x and y).");
    b.prop("StudsOffset", Value::vector3({0, 0, 0}), "Offset from the Adornee in metres (world space).");
    b.prop("AlwaysOnTop", Value::boolean(false), "Draws through walls (no occlusion test is done yet).");
    b.prop("MaxDistance", Value::number(100.0), "Hidden beyond this distance from the camera (m).");
    b.cls("GuiObject", "GuiBase", "A rectangle of UI.").abstract();
    b.group("Layout").prop("Position", Value::vector3({0, 0, 0}), "Position: fraction of the parent (x, y).");
    b.prop("PositionOffset", Value::vector3({0, 0, 0}), "Position offset in pixels (x, y).");
    b.prop("Size", Value::vector3({0, 0, 0}), "Size: fraction of the parent (x, y).");
    b.prop("SizeOffset", Value::vector3({200, 50, 0}), "Size offset in pixels (x, y).");
    b.prop("AnchorPoint", Value::vector3({0, 0, 0}), "Pivot inside the object (0-1, 0-1).");
    b.prop("Rotation", Value::number(0.0), "Rotation (degrees, clockwise on screen) around the centre.");
    b.prop("LayoutOrder", Value::integer(0), "Order inside a UIListLayout / UIGridLayout (SortOrder LayoutOrder).");
    b.prop("ClipsDescendants", Value::boolean(false), "Children are clipped to this rectangle.");
    b.group("Appearance").prop("BackgroundColor", Value::color3({0.16f, 0.17f, 0.19f}), "Fill colour.");
    b.prop("BackgroundTransparency", Value::number(0.0), "0 = opaque, 1 = invisible.");
    b.prop("CornerRadius", Value::number(0.0), "Rounded corners (pixels). A UICorner child overrides it.");
    b.prop("BorderColor", Value::color3({0, 0, 0}), "Outline colour.");
    b.prop("BorderSize", Value::number(0.0), "Outline thickness (pixels). A UIStroke child overrides it.");
    b.cls("Frame", "GuiObject", "A plain rectangle that groups other UI.", "UI");
    b.cls("TextLabel", "GuiObject", "Shows text.", "UI");
    b.group("Text").prop("Text", Value::string("Label"), "The text.");
    b.prop("TextColor", Value::color3({1, 1, 1}), "Text colour.");
    b.prop("TextSize", Value::number(20.0), "Font size (pixels).");
    b.prop("TextScaled", Value::boolean(false), "Grows the text to fill the box.");
    b.prop("TextTransparency", Value::number(0.0), "0 = opaque.");
    b.prop("TextStrokeTransparency", Value::number(1.0), "Outline around the letters (1 = none).");
    b.prop("TextXAlignment", Value::enumeration(1), "Horizontal alignment.", 0, text_x);
    b.prop("TextYAlignment", Value::enumeration(1), "Vertical alignment.", 0, text_y);
    b.prop("Font", Value::enumeration(0), "Typeface.", 0, font);
    b.cls("TextButton", "TextLabel", "Clickable text: fires Activated.", "UI");
    b.group("Behavior").prop("AutoButtonColor", Value::boolean(true), "Darkens when hovered or pressed.");
    b.cls("TextBox", "TextLabel", "Editable text. Enter or a click elsewhere fires FocusLost.", "UI");
    b.group("Text").prop("PlaceholderText", Value::string(""), "Grey hint shown while the text is empty.");
    b.prop("ClearTextOnFocus", Value::boolean(true), "Empties the text when the box gets focus.");
    b.cls("ImageLabel", "GuiObject", "Shows an image (.png/.jpg file).", "UI");
    b.group("Image").prop("Image", Value::string(""), "Image file path.");
    b.prop("ImageColor", Value::color3({1, 1, 1}), "Tint.");
    b.prop("ImageTransparency", Value::number(0.0), "0 = opaque.");
    b.prop("ScaleType", Value::enumeration(0), "How the image fits the box.", 0, scale_type);
    b.cls("ImageButton", "ImageLabel", "Clickable image: fires Activated.", "UI");
    b.group("Behavior").prop("AutoButtonColor", Value::boolean(true), "Darkens when hovered or pressed.");

    // Layout and styling modifiers: children of a GuiObject (or of a ScreenGui / BillboardGui for the layouts).
    b.cls("UICorner", "Instance", "Rounds the corners of its parent (overrides CornerRadius).", "UI");
    b.group("Appearance").prop("CornerRadius", Value::number(8.0), "Corner radius (pixels).");
    b.cls("UIStroke", "Instance", "Outline around its parent (overrides BorderSize).", "UI");
    b.group("Appearance").prop("Color", Value::color3({1, 1, 1}), "Outline colour.");
    b.prop("Thickness", Value::number(1.0), "Outline thickness (pixels).");
    b.prop("Transparency", Value::number(0.0), "0 = opaque, 1 = invisible.");
    b.cls("UIPadding", "Instance", "Insets the children of its parent (pixels).", "UI");
    b.group("Layout").prop("PaddingLeft", Value::number(0.0), "Left inset (pixels).");
    b.prop("PaddingRight", Value::number(0.0), "Right inset (pixels).");
    b.prop("PaddingTop", Value::number(0.0), "Top inset (pixels).");
    b.prop("PaddingBottom", Value::number(0.0), "Bottom inset (pixels).");
    b.cls("UIListLayout", "Instance", "Stacks the children of its parent in a row or a column.", "UI");
    b.group("Layout").prop("FillDirection", Value::enumeration(0), "Vertical stacks down, Horizontal across.", 0,
                           fill_dir);
    b.prop("Padding", Value::number(0.0), "Gap between children (pixels).");
    b.prop("HorizontalAlignment", Value::enumeration(0), "Cross-axis (Vertical list) or main-axis alignment.", 0,
           h_align);
    b.prop("VerticalAlignment", Value::enumeration(0), "Main-axis (Vertical list) or cross-axis alignment.", 0,
           v_align);
    b.prop("SortOrder", Value::enumeration(0), "Order of the children: LayoutOrder, then Name.", 0, sort_order);
    b.cls("UIGridLayout", "Instance", "Places the children of its parent in equal cells.", "UI");
    b.group("Layout").prop("CellSize", Value::vector3({100, 100, 0}), "Cell size (pixels, x and y).");
    b.prop("CellPadding", Value::vector3({5, 5, 0}), "Gap between cells (pixels, x and y).");
    b.prop("FillDirection", Value::enumeration(1), "Horizontal fills rows, Vertical fills columns.", 0, fill_dir);
    b.prop("HorizontalAlignment", Value::enumeration(0), "Horizontal alignment of the grid block.", 0, h_align);
    b.prop("VerticalAlignment", Value::enumeration(0), "Vertical alignment of the grid block.", 0, v_align);
    b.prop("SortOrder", Value::enumeration(0), "Order of the children: LayoutOrder, then Name.", 0, sort_order);
    b.cls("UIGradient", "Instance", "Colour gradient over the background of its parent.", "UI");
    b.group("Appearance").prop("Color0", Value::color3({1, 1, 1}), "Colour at the start of the gradient.");
    b.prop("Color1", Value::color3({0.5f, 0.5f, 0.5f}), "Colour at the end of the gradient.");
    b.prop("Rotation", Value::number(0.0), "Direction (degrees): 0 = left to right, 90 = top to bottom.");
    b.cls("UIAspectRatioConstraint", "Instance", "Keeps the parent's width / height ratio.", "UI");
    b.group("Layout").prop("AspectRatio", Value::number(1.0), "Width divided by height.");

    // ---- Audio ---------------------------------------------------------------------------------------------
    b.cls("Sound", "Instance", "Plays an audio file; 3D when parented to a part.", "Audio");
    b.group("Data").prop("SoundId", Value::string(""), "Audio file path (.wav, .ogg, .mp3, .flac).");
    b.prop("Volume", Value::number(0.5), "Loudness (0-10).");
    b.prop("PlaybackSpeed", Value::number(1.0), "Speed and pitch multiplier.");
    b.prop("Looped", Value::boolean(false), "Restarts when it ends.");
    b.prop("Playing", Value::boolean(false), "True while playing; set it to start or stop the sound.");
    b.prop("PlayOnRemove", Value::boolean(false), "Plays once when destroyed.");
    b.prop("RollOffMinDistance", Value::number(3.0), "Full volume up to this distance (m).");
    b.prop("RollOffMaxDistance", Value::number(120.0), "Silent beyond this distance (m).");
    b.prop("TimePosition", Value::number(0.0), "Current position (s).", kPropNotSerialized);
    b.prop("TimeLength", Value::number(0.0), "Length of the file (s).", kPropReadOnly | kPropNotSerialized);

    // ---- Effects ---------------------------------------------------------------------------------------------
    b.cls("ParticleEmitter", "Instance",
          "Emits camera-facing particles (smoke, fire, sparks, dust, magic) from its parent part or attachment.",
          "Effects");
    b.group("Emission").prop("Enabled", Value::boolean(true), "Emits Rate particles per second while true.");
    b.prop("Rate", Value::number(20.0), "Particles per second.");
    b.prop("Lifetime", Value::number(2.0), "Seconds each particle lives.");
    b.prop("Speed", Value::number(5.0), "Initial speed (m/s) along EmissionDirection.");
    b.prop("SpreadAngle", Value::number(15.0), "Random deviation from EmissionDirection (degrees, 180 = all around).");
    b.prop("Variation", Value::number(0.3), "Random +- fraction applied to lifetime, speed and size (0-1).");
    b.prop("EmissionDirection", Value::enumeration(0), "Face of the parent part the particles leave from.", 0,
           normal_id);
    b.prop("Shape", Value::enumeration(0), "Volume particles spawn in: the parent's box, sphere, a disc, or a point.",
           0, particle_shape);
    b.group("Appearance")
        .prop("Texture", Value::string("builtin:circle"),
              "builtin:circle / smoke / spark / star / fire / snow / ring.");
    b.prop("Color", Value::color3({1, 1, 1}), "Colour at birth.");
    b.prop("ColorEnd", Value::color3({1, 1, 1}), "Colour at death (blended over the lifetime).");
    b.prop("Size", Value::number(1.0), "Size at birth (m).");
    b.prop("SizeEnd", Value::number(1.0), "Size at death (m).");
    b.prop("Transparency", Value::number(0.0), "Transparency at birth (0 opaque, 1 invisible).");
    b.prop("TransparencyEnd", Value::number(1.0), "Transparency at death.");
    b.prop("LightEmission", Value::number(0.0), "0 = blended, 1 = additive glow (fire, sparks, magic).");
    b.prop("Brightness", Value::number(1.0), "Glow strength of emissive particles (LightEmission > 0).");
    b.prop("Stretch", Value::number(0.0), "Stretches particles along their velocity (sparks, rain).");
    b.group("Motion").prop("Acceleration", Value::vector3({0, 0, 0}), "World acceleration (m/s^2), e.g. gravity.");
    b.prop("Drag", Value::number(0.0), "Velocity decay per second.");
    b.prop("RotSpeed", Value::number(0.0), "Spin (degrees per second, random sign).");
    b.prop("LockedToPart", Value::boolean(false), "Particles follow the parent part as it moves.");
    b.prop("TimeScale", Value::number(1.0), "Simulation speed (0 freezes).");
    b.prop("EmitCount", Value::integer(0), "Pending burst (Emit(n) adds to it; consumed by the engine).",
           kPropHidden | kPropNotSerialized);

    // ---- Characters ------------------------------------------------------------------------------------------
    b.cls("Humanoid", "Instance", "Makes a Model a character: health, walking, jumping, animation state.",
          "Characters");
    b.group("Data").prop("Health", Value::number(100.0), "Current health.");
    b.prop("MaxHealth", Value::number(100.0), "Maximum health.");
    b.prop("WalkSpeed", Value::number(6.0), "Walking speed (m/s).");
    b.prop("JumpHeight", Value::number(1.6), "Jump height (m).");
    b.prop("HipHeight", Value::number(0.9), "Height of the root part above the ground (m).");
    b.prop("AutoRotate", Value::boolean(true), "Turns to face the movement direction.");
    b.prop("RigType", Value::enumeration(0), "Body rig.", 0, rig);
    b.prop("DisplayName", Value::string(""), "Name shown above the character.");
    b.prop("DisplayDistanceType", Value::enumeration(0),
           "Who sees the name: Viewer (everyone), Subject (only the "
           "player's own character), None.",
           0, display_dist);
    b.prop("NameDisplayDistance", Value::number(100.0), "The name is hidden beyond this distance (m).");
    b.prop("State", Value::enumeration(0), "Current state.", kPropReadOnly | kPropNotSerialized, humanoid_state);
    b.prop("MoveDirection", Value::vector3({0, 0, 0}), "Desired movement (world, unit).", kPropNotSerialized);
    b.cls("Animator", "Instance", "Plays and blends animations on a Humanoid's rig.", "Characters");
    // Imported glTF rigs (RigType Custom): clip names for the automatic locomotion, matched case-insensitively
    // (exact name, then substring) among the clips of the MeshPart's file or of the Animation children.
    b.group("Imported rigs").prop("IdleClip", Value::string("idle"), "Clip played while standing.");
    b.prop("WalkClip", Value::string("walk"), "Clip played while walking (falls back to idle).");
    b.prop("RunClip", Value::string("run"), "Clip played while running (falls back to walk).");
    b.prop("JumpClip", Value::string("jump"), "Clip played while jumping.");
    b.prop("FallClip", Value::string("fall"), "Clip played while falling.");
    b.cls("Animation", "Instance", "Reference to a keyframe animation asset.", "Characters");
    b.group("Data").prop("AnimationId", Value::string(""), "Asset path (glTF animation or built-in name).");
    b.prop("Looped", Value::boolean(true), "Loops when played.");
    b.prop("Priority", Value::integer(1), "Higher priorities override lower ones.");
    b.cls("Motor6D", "Instance", "Joint between two parts of a rig, animated by the Animator.", "Characters");
    b.group("Data").prop("Part0", Value::reference(0), "Parent part.");
    b.prop("Part1", Value::reference(0), "Child part.");
    b.prop("C0", Value::vector3({0, 0, 0}), "Joint position in Part0's space (m).");
    b.prop("C1", Value::vector3({0, 0, 0}), "Joint position in Part1's space (m).");
    b.prop("C0Orientation", Value::vector3({0, 0, 0}), "Joint rotation in Part0's space (degrees, Orientation order).");
    b.prop("C1Orientation", Value::vector3({0, 0, 0}), "Joint rotation in Part1's space (degrees, Orientation order).");
    b.cls("Camera", "Instance", "A viewpoint.", "Gameplay");
    b.group("Data").prop("FieldOfView", Value::number(70.0), "Vertical field of view (degrees).");
    b.prop("Position", Value::vector3({0, 5, 10}), "Camera position (m).");
    b.prop("LookAt", Value::vector3({0, 0, 0}), "Point the camera looks at (m).");
}

} // namespace tefra::world
