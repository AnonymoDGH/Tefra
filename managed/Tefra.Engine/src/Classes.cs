// Typed wrappers for the built-in DataModel classes (engine/world/src/classes.cpp). Enum values are listed in the
// same order as the native enums: changing one side without the other breaks the mapping. Props are resolved by
// name once and cached (PropRef), so each access is one native call.
using System;
using System.Collections.Generic;

namespace Tefra
{
    public enum Material
    {
        Plastic, SmoothPlastic, Wood, WoodPlanks, Metal, DiamondPlate, Concrete, Brick, Cobblestone, Granite,
        Marble, Slate, Sand, Grass, Ground, Rock, Ice, Glass, Neon, Fabric, Foil
    }

    public enum PartType
    {
        Block, Ball, Cylinder, Wedge
    }

    public enum RunContext
    {
        Server, Client
    }

    public class BasePart : Instance
    {
        private static readonly PropRef PositionP = new PropRef("BasePart", "Position");
        private static readonly PropRef OrientationP = new PropRef("BasePart", "Orientation");
        private static readonly PropRef SizeP = new PropRef("BasePart", "Size");
        private static readonly PropRef ColorP = new PropRef("BasePart", "Color");
        private static readonly PropRef MaterialP = new PropRef("BasePart", "Material");
        private static readonly PropRef TransparencyP = new PropRef("BasePart", "Transparency");
        private static readonly PropRef ReflectanceP = new PropRef("BasePart", "Reflectance");
        private static readonly PropRef CastShadowP = new PropRef("BasePart", "CastShadow");
        private static readonly PropRef AnchoredP = new PropRef("BasePart", "Anchored");
        private static readonly PropRef CanCollideP = new PropRef("BasePart", "CanCollide");
        private static readonly PropRef CanTouchP = new PropRef("BasePart", "CanTouch");
        private static readonly PropRef MasslessP = new PropRef("BasePart", "Massless");
        private static readonly PropRef DensityP = new PropRef("BasePart", "Density");
        private static readonly PropRef FrictionP = new PropRef("BasePart", "Friction");
        private static readonly PropRef ElasticityP = new PropRef("BasePart", "Elasticity");
        private static readonly PropRef VelocityP = new PropRef("BasePart", "AssemblyLinearVelocity");

        internal BasePart(ulong id) : base(id) { }

        /// <summary>Centre in world space (metres).</summary>
        public Vector3 Position { get => GetVector3(PositionP); set => SetVector3(PositionP, value); }
        /// <summary>Rotation in degrees, applied Y, then X, then Z (Roblox order).</summary>
        public Vector3 Orientation { get => GetVector3(OrientationP); set => SetVector3(OrientationP, value); }
        /// <summary>Dimensions in metres.</summary>
        public Vector3 Size { get => GetVector3(SizeP); set => SetVector3(SizeP, value); }
        public Color3 Color { get => GetColor3(ColorP); set => SetColor3(ColorP, value); }
        public Material Material { get => (Material)GetInt(MaterialP); set => SetEnum(MaterialP, (int)value); }
        /// <summary>0 = opaque, 1 = invisible.</summary>
        public float Transparency { get => GetFloat(TransparencyP); set => SetFloat(TransparencyP, value); }
        public float Reflectance { get => GetFloat(ReflectanceP); set => SetFloat(ReflectanceP, value); }
        public bool CastShadow { get => GetBool(CastShadowP); set => SetBool(CastShadowP, value); }
        /// <summary>Anchored parts are fixed in place; unanchored ones fall under gravity.</summary>
        public bool Anchored { get => GetBool(AnchoredP); set => SetBool(AnchoredP, value); }
        public bool CanCollide { get => GetBool(CanCollideP); set => SetBool(CanCollideP, value); }
        public bool CanTouch { get => GetBool(CanTouchP); set => SetBool(CanTouchP, value); }
        public bool Massless { get => GetBool(MasslessP); set => SetBool(MasslessP, value); }
        public float Density { get => GetFloat(DensityP); set => SetFloat(DensityP, value); }
        public float Friction { get => GetFloat(FrictionP); set => SetFloat(FrictionP, value); }
        public float Elasticity { get => GetFloat(ElasticityP); set => SetFloat(ElasticityP, value); }
        public Vector3 AssemblyLinearVelocity { get => GetVector3(VelocityP); set => SetVector3(VelocityP, value); }
    }

    public class Part : BasePart
    {
        private static readonly PropRef ShapeP = new PropRef("Part", "Shape");

        internal Part(ulong id) : base(id) { }

        public PartType Shape { get => (PartType)GetInt(ShapeP); set => SetEnum(ShapeP, (int)value); }
    }

    public class MeshPart : BasePart
    {
        private static readonly PropRef MeshIdP = new PropRef("MeshPart", "MeshId");
        private static readonly PropRef TextureIdP = new PropRef("MeshPart", "TextureId");

        internal MeshPart(ulong id) : base(id) { }

        public string MeshId { get => GetString(MeshIdP); set => SetString(MeshIdP, value); }
        public string TextureId { get => GetString(TextureIdP); set => SetString(TextureIdP, value); }
    }

    public class SpawnLocation : Part
    {
        private static readonly PropRef EnabledP = new PropRef("SpawnLocation", "Enabled");
        private static readonly PropRef NeutralP = new PropRef("SpawnLocation", "Neutral");

        internal SpawnLocation(ulong id) : base(id) { }

        public bool Enabled { get => GetBool(EnabledP); set => SetBool(EnabledP, value); }
        public bool Neutral { get => GetBool(NeutralP); set => SetBool(NeutralP, value); }
    }

    public class Model : Instance
    {
        private static readonly PropRef PrimaryPartP = new PropRef("Model", "PrimaryPart");

        internal Model(ulong id) : base(id) { }

        public BasePart PrimaryPart
        {
            get => GetRef(PrimaryPartP) as BasePart;
            set => SetRef(PrimaryPartP, value);
        }
    }

    public class Folder : Instance
    {
        internal Folder(ulong id) : base(id) { }
    }

    public class Workspace : Instance
    {
        private static readonly PropRef GravityP = new PropRef("Workspace", "Gravity");
        private static readonly PropRef FallenP = new PropRef("Workspace", "FallenPartsDestroyHeight");

        internal Workspace(ulong id) : base(id) { }

        /// <summary>Gravitational acceleration (m/s^2).</summary>
        public float Gravity { get => GetFloat(GravityP); set => SetFloat(GravityP, value); }
        public float FallenPartsDestroyHeight { get => GetFloat(FallenP); set => SetFloat(FallenP, value); }
    }

    public class Lighting : Instance
    {
        private static readonly PropRef ClockP = new PropRef("Lighting", "ClockTime");
        private static readonly PropRef FogP = new PropRef("Lighting", "FogDensity");
        private static readonly PropRef ExposureP = new PropRef("Lighting", "ExposureCompensation");

        internal Lighting(ulong id) : base(id) { }

        /// <summary>Hour of the day, 0-24.</summary>
        public float ClockTime { get => GetFloat(ClockP); set => SetFloat(ClockP, value); }
        public float FogDensity { get => GetFloat(FogP); set => SetFloat(FogP, value); }
        public float ExposureCompensation { get => GetFloat(ExposureP); set => SetFloat(ExposureP, value); }
    }

    public class BaseScript : Instance
    {
        private static readonly PropRef EnabledP = new PropRef("BaseScript", "Enabled");
        private static readonly PropRef SourceP = new PropRef("BaseScript", "Source");

        internal BaseScript(ulong id) : base(id) { }

        /// <summary>When false the script is not compiled or started.</summary>
        public bool Enabled { get => GetBool(EnabledP); set => SetBool(EnabledP, value); }
        /// <summary>C# source: one class deriving from TefraScript.</summary>
        public string Source { get => GetString(SourceP); set => SetString(SourceP, value); }
    }

    /// <summary>Server script: compiled on Play and run once per game.</summary>
    public class Script : BaseScript
    {
        private static readonly PropRef RunContextP = new PropRef("Script", "RunContext");

        internal Script(ulong id) : base(id) { }

        public RunContext RunContext { get => (RunContext)GetInt(RunContextP); set => SetEnum(RunContextP, (int)value); }
    }

    public class LocalScript : BaseScript
    {
        internal LocalScript(ulong id) : base(id) { }
    }

    public class ModuleScript : Instance
    {
        private static readonly PropRef SourceP = new PropRef("ModuleScript", "Source");

        internal ModuleScript(ulong id) : base(id) { }

        public string Source { get => GetString(SourceP); set => SetString(SourceP, value); }
    }

    // Factory table: the most derived wrapper for each class name. Classes without a wrapper use Instance.
    internal static class ClassMap
    {
        internal static readonly Dictionary<string, Func<ulong, Instance>> Map =
            new Dictionary<string, Func<ulong, Instance>>
            {
                ["Workspace"] = id => new Workspace(id),
                ["Lighting"] = id => new Lighting(id),
                ["BasePart"] = id => new BasePart(id),
                ["Part"] = id => new Part(id),
                ["MeshPart"] = id => new MeshPart(id),
                ["SpawnLocation"] = id => new SpawnLocation(id),
                ["Model"] = id => new Model(id),
                ["Folder"] = id => new Folder(id),
                ["BaseScript"] = id => new BaseScript(id),
                ["Script"] = id => new Script(id),
                ["LocalScript"] = id => new LocalScript(id),
                ["ModuleScript"] = id => new ModuleScript(id),
            };
    }
}
