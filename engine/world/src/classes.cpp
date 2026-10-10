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
    const EnumInfo* terrain_kind = r.add_enum("TerrainKind", {"None", "Valley", "Coast"});
    const EnumInfo* script_ctx = r.add_enum("RunContext", {"Server", "Client"});
    const EnumInfo* humanoid_state = r.add_enum("HumanoidState", {"Idle", "Walking", "Running", "Jumping", "Freefall",
                                                                  "Landed", "Dead", "Seated", "Climbing", "Swimming"});
    const EnumInfo* rig = r.add_enum("RigType", {"R15", "Custom"});

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
    b.prop("CameraMaxZoomDistance", Value::number(40.0), "Maximum third-person camera distance (m).");
    b.cls("StarterGui", "Instance", "UI cloned into each player's screen.").service();

    // ---- Containers ----------------------------------------------------------------------------------------
    b.cls("Folder", "Instance", "Groups instances without any behaviour.", "Containers");
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
    b.cls("Script", "BaseScript", "Server-side C# script (runs once per game).", "Scripts");
    b.group("Behavior").prop("RunContext", Value::enumeration(0), "Where the script runs.", 0, script_ctx);
    b.cls("LocalScript", "BaseScript", "Client-side C# script (input, camera, UI).", "Scripts");
    b.cls("ModuleScript", "Instance", "Shared C# code used by other scripts.", "Scripts");
    b.group("Behavior").prop("Source", Value::string(""), "C# source code.", kPropHidden);

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
    b.prop("State", Value::enumeration(0), "Current state.", kPropReadOnly | kPropNotSerialized, humanoid_state);
    b.prop("MoveDirection", Value::vector3({0, 0, 0}), "Desired movement (world, unit).", kPropNotSerialized);
    b.cls("Animator", "Instance", "Plays and blends animations on a Humanoid's rig.", "Characters");
    b.cls("Animation", "Instance", "Reference to a keyframe animation asset.", "Characters");
    b.group("Data").prop("AnimationId", Value::string(""), "Asset path (glTF animation or built-in name).");
    b.prop("Looped", Value::boolean(true), "Loops when played.");
    b.prop("Priority", Value::integer(1), "Higher priorities override lower ones.");
    b.cls("Motor6D", "Instance", "Joint between two parts of a rig, animated by the Animator.", "Characters");
    b.group("Data").prop("Part0", Value::reference(0), "Parent part.");
    b.prop("Part1", Value::reference(0), "Child part.");
    b.prop("C0", Value::vector3({0, 0, 0}), "Joint position in Part0's space (m).");
    b.prop("C1", Value::vector3({0, 0, 0}), "Joint position in Part1's space (m).");
    b.cls("Camera", "Instance", "A viewpoint.", "Gameplay");
    b.group("Data").prop("FieldOfView", Value::number(70.0), "Vertical field of view (degrees).");
    b.prop("Position", Value::vector3({0, 5, 10}), "Camera position (m).");
    b.prop("LookAt", Value::vector3({0, 0, 0}), "Point the camera looks at (m).");
}

} // namespace tefra::world
