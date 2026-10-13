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
        Marble, Slate, Sand, Grass, Ground, Rock, Ice, Glass, Neon, Fabric, Foil, Wallpaper, Carpet, CeilingTile
    }

    public enum PartType
    {
        Block, Ball, Cylinder, Wedge
    }

    public enum RunContext
    {
        Server, Client
    }

    // Enums below mirror the order of classes.cpp (add_enum).
    public enum HumanoidState
    {
        Idle, Walking, Running, Jumping, Freefall, Landed, Dead, Seated, Climbing, Swimming
    }

    public enum RigType
    {
        R15, Custom
    }

    public enum TextXAlignment
    {
        Left, Center, Right
    }

    public enum TextYAlignment
    {
        Top, Center, Bottom
    }

    public enum ScaleType
    {
        Stretch, Fit, Crop
    }

    public enum Font
    {
        Default, Bold, Mono
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
        private static readonly PropRef AngularVelocityP = new PropRef("BasePart", "AssemblyAngularVelocity");

        internal BasePart(ulong id) : base(id) { }

        /// <summary>Centre in world space (metres).</summary>
        public Vector3 Position { get => GetVector3(PositionP); set => SetVector3(PositionP, value); }
        /// <summary>Rotation in degrees, applied Y, then X, then Z (Roblox order).</summary>
        public Vector3 Orientation { get => GetVector3(OrientationP); set => SetVector3(OrientationP, value); }
        /// <summary>Position and rotation together (Position plus the rotation from Orientation).</summary>
        public CFrame CFrame
        {
            get => CFrame.FromOrientation(Position, Orientation);
            set
            {
                Position = value.Position;
                Orientation = value.ToOrientation();
            }
        }
        /// <summary>Moves the part so that its CFrame becomes target.</summary>
        public void PivotTo(CFrame target) => CFrame = target;
        /// <summary>The part's own CFrame, which is its pivot.</summary>
        public CFrame GetPivot() => CFrame;
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
        /// <summary>Velocity of an unanchored part (m/s). Ignored for anchored parts.</summary>
        public Vector3 AssemblyLinearVelocity { get => GetVector3(VelocityP); set => SetVector3(VelocityP, value); }
        /// <summary>Spin of an unanchored part (rad/s, world axes). Ignored for anchored parts.</summary>
        public Vector3 AssemblyAngularVelocity { get => GetVector3(AngularVelocityP); set => SetVector3(AngularVelocityP, value); }

        /// <summary>
        /// Adds a linear impulse (kg·m/s) to an unanchored part; it applies at the next physics step. Does nothing for
        /// anchored parts or outside Play mode.
        /// </summary>
        public void ApplyImpulse(Vector3 impulse) => Native.ApplyImpulse(Id, impulse, false);

        /// <summary>Adds an angular impulse (kg·m²/s, world axes) to an unanchored part.</summary>
        public void ApplyAngularImpulse(Vector3 impulse) => Native.ApplyImpulse(Id, impulse, true);

        /// <summary>Mass in kg: Density times the volume of the part's shape.</summary>
        public float GetMass() => (float)Native.Mass(Id);

        /// <summary>
        /// Parts in contact with this one right now (sensors included). For a character's HumanoidRootPart, the parts
        /// the character touches. Empty outside Play mode.
        /// </summary>
        public BasePart[] GetTouchingParts()
        {
            ulong[] buf = new ulong[32];
            uint total = Native.TouchingParts(Id, buf);
            if (total > buf.Length)
            {
                buf = new ulong[total];
                total = Native.TouchingParts(Id, buf);
            }
            return ToParts(buf, total);
        }

        // Runs an overlap query (kind 0 Radius, 1 Box, 2 Part) and returns the matching parts.
        internal static BasePart[] Overlap(int kind, Vector3 center, Vector3 orientation, Vector3 size, float radius,
            ulong partId, OverlapParams p)
        {
            ulong[] buf = new ulong[64];
            uint total = Native.Overlap(kind, center, orientation, size, radius, partId, p, p == null ? 0 : p.MaxParts, buf);
            if (total > buf.Length)
            {
                buf = new ulong[total];
                total = Native.Overlap(kind, center, orientation, size, radius, partId, p, p == null ? 0 : p.MaxParts, buf);
            }
            return ToParts(buf, total);
        }

        private static BasePart[] ToParts(ulong[] ids, uint total)
        {
            var list = new List<BasePart>((int)Math.Min(total, (uint)ids.Length));
            for (int i = 0; i < ids.Length && i < total; ++i)
                if (Instance.FromId(ids[i]) is BasePart part) list.Add(part);
            return list.ToArray();
        }

        /// <summary>Fires once when another part (or a character) starts touching this one. The argument is the other part.
        /// Characters report the touch with their HumanoidRootPart; sensors (CanCollide = false, CanTouch = true) report too.</summary>
        public event Action<Instance> Touched
        {
            add => AddSignal("Touched", value);
            remove => RemoveSignal("Touched", value);
        }

        /// <summary>Fires when a contact that was reported by Touched ends.</summary>
        public event Action<Instance> TouchEnded
        {
            add => AddSignal("TouchEnded", value);
            remove => RemoveSignal("TouchEnded", value);
        }
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

        /// <summary>PrimaryPart's CFrame if set, else the first BasePart descendant's; identity for an empty model.</summary>
        public CFrame GetPivot()
        {
            BasePart primary = PrimaryPart;
            if (primary != null) return primary.CFrame;
            foreach (Instance child in GetDescendants())
                if (child is BasePart part) return part.CFrame;
            return CFrame.Identity;
        }

        /// <summary>Moves every BasePart descendant rigidly so that the pivot ends at target.</summary>
        public void PivotTo(CFrame target)
        {
            var parts = new List<BasePart>();
            foreach (Instance child in GetDescendants())
                if (child is BasePart part) parts.Add(part);
            if (parts.Count == 0) return;
            CFrame delta = target * GetPivot().Inverse();
            foreach (BasePart part in parts) part.CFrame = delta * part.CFrame;
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

        /// <summary>
        /// Closest solid part along the ray from origin in direction (sensors are ignored, like CanCollide = false).
        /// Returns null on a miss or outside Play mode.
        /// </summary>
        public RaycastResult Raycast(Vector3 origin, Vector3 direction, float maxDistance = 1000f)
        {
            return Raycast(origin, direction, maxDistance, null);
        }

        /// <summary>Raycast that skips the parts that params filters out (FilterDescendantsInstances, FilterType).</summary>
        public RaycastResult Raycast(Vector3 origin, Vector3 direction, RaycastParams p)
        {
            return Raycast(origin, direction, 1000f, p);
        }

        public RaycastResult Raycast(Vector3 origin, Vector3 direction, float maxDistance, RaycastParams p)
        {
            double[] hit;
            ulong id = Native.Raycast(origin.X, origin.Y, origin.Z, direction.X, direction.Y, direction.Z, maxDistance, p, out hit);
            return ToResult(id, hit);
        }

        /// <summary>
        /// Closest solid part hit by a sphere of the given radius moving from origin along direction. The length of
        /// direction is the distance. Returns null on a miss. The contact point is on the hit part's surface.
        /// </summary>
        public RaycastResult Spherecast(Vector3 origin, float radius, Vector3 direction, RaycastParams p = null)
        {
            double[] hit;
            return ToResult(Native.Spherecast(origin, radius, direction, p, out hit), hit);
        }

        /// <summary>
        /// Closest solid part hit by a box (center, orientation in degrees Y, X, Z, size) moving along direction. The
        /// length of direction is the distance. Returns null on a miss.
        /// </summary>
        public RaycastResult Blockcast(Vector3 center, Vector3 orientationDegrees, Vector3 size, Vector3 direction,
            RaycastParams p = null)
        {
            double[] hit;
            return ToResult(Native.Blockcast(center, orientationDegrees, size, direction, p, out hit), hit);
        }

        /// <summary>Parts whose box touches the sphere (center, radius). A Ball counts as its inscribed sphere.</summary>
        public BasePart[] GetPartBoundsInRadius(Vector3 center, float radius, OverlapParams p = null)
        {
            return BasePart.Overlap(0, center, Vector3.Zero, Vector3.Zero, radius, 0, p);
        }

        /// <summary>Parts whose box overlaps the axis-aligned box centered at center with the given size.</summary>
        public BasePart[] GetPartBoundsInBox(Vector3 center, Vector3 size, OverlapParams p = null)
        {
            return BasePart.Overlap(1, center, Vector3.Zero, size, 0, 0, p);
        }

        /// <summary>Parts whose box overlaps the box centered at center, rotated by orientationDegrees (Y, X, Z).</summary>
        public BasePart[] GetPartBoundsInBox(Vector3 center, Vector3 orientationDegrees, Vector3 size, OverlapParams p = null)
        {
            return BasePart.Overlap(1, center, orientationDegrees, size, 0, 0, p);
        }

        /// <summary>
        /// Parts whose box overlaps the box of part. The part itself is not returned. Boxes only (a Ball counts as
        /// its box).
        /// </summary>
        public BasePart[] GetPartsInPart(BasePart part, OverlapParams p = null)
        {
            if (part == null) return new BasePart[0];
            return BasePart.Overlap(2, Vector3.Zero, Vector3.Zero, Vector3.Zero, 0, part.Id, p);
        }

        private static RaycastResult ToResult(ulong id, double[] hit)
        {
            if (id == 0 || hit == null) return null;
            return new RaycastResult(
                Instance.FromId(id),
                new Vector3((float)hit[0], (float)hit[1], (float)hit[2]),
                new Vector3((float)hit[3], (float)hit[4], (float)hit[5]),
                (float)hit[6]);
        }
    }

    /// <summary>Whether FilterDescendantsInstances lists the parts to ignore (Exclude) or the only ones to hit (Include).</summary>
    public enum FilterType
    {
        Exclude, Include
    }

    /// <summary>
    /// Filter shared by RaycastParams and OverlapParams (Roblox semantics). A part is filtered by its ancestry: the part
    /// or one of its ancestors must be listed. Exclude (default) ignores listed parts; Include hits only listed parts,
    /// so an Include filter with an empty list hits nothing.
    /// </summary>
    public abstract class QueryParams
    {
        /// <summary>Instances whose parts (and their descendants) are filtered by FilterType.</summary>
        public List<Instance> FilterDescendantsInstances { get; set; } = new List<Instance>();
        public FilterType FilterType { get; set; } = FilterType.Exclude;

        internal ulong[] FilterIds()
        {
            var ids = new List<ulong>();
            if (FilterDescendantsInstances != null)
                foreach (Instance i in FilterDescendantsInstances)
                    if (i != null) ids.Add(i.Id);
            return ids.ToArray();
        }
    }

    /// <summary>Parameters of Workspace.Raycast, Spherecast and Blockcast.</summary>
    public sealed class RaycastParams : QueryParams
    {
    }

    /// <summary>Parameters of Workspace.GetPartBoundsInRadius / GetPartBoundsInBox / GetPartsInPart.</summary>
    public sealed class OverlapParams : QueryParams
    {
        /// <summary>At most this many parts are returned; 0 = no limit.</summary>
        public int MaxParts { get; set; } = 0;
    }

    /// <summary>Answer of Workspace.Raycast, Spherecast and Blockcast.</summary>
    public sealed class RaycastResult
    {
        /// <summary>The part that was hit.</summary>
        public Instance Instance { get; }
        /// <summary>Hit point (m).</summary>
        public Vector3 Position { get; }
        /// <summary>Unit surface normal at the hit.</summary>
        public Vector3 Normal { get; }
        /// <summary>Distance from the origin (m).</summary>
        public float Distance { get; }

        internal RaycastResult(Instance instance, Vector3 position, Vector3 normal, float distance)
        {
            Instance = instance;
            Position = position;
            Normal = normal;
            Distance = distance;
        }
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

    // ---- Players and characters ------------------------------------------------------------------------------

    /// <summary>A character: health, walking and jumping (classes.cpp: Humanoid). Its Model is the character.</summary>
    public class Humanoid : Instance
    {
        private static readonly PropRef HealthP = new PropRef("Humanoid", "Health");
        private static readonly PropRef MaxHealthP = new PropRef("Humanoid", "MaxHealth");
        private static readonly PropRef WalkSpeedP = new PropRef("Humanoid", "WalkSpeed");
        private static readonly PropRef JumpHeightP = new PropRef("Humanoid", "JumpHeight");
        private static readonly PropRef HipHeightP = new PropRef("Humanoid", "HipHeight");
        private static readonly PropRef AutoRotateP = new PropRef("Humanoid", "AutoRotate");
        private static readonly PropRef RigTypeP = new PropRef("Humanoid", "RigType");
        private static readonly PropRef DisplayNameP = new PropRef("Humanoid", "DisplayName");
        private static readonly PropRef StateP = new PropRef("Humanoid", "State");
        private static readonly PropRef MoveDirectionP = new PropRef("Humanoid", "MoveDirection");
        private static readonly PropRef NameP = new PropRef("Humanoid", "DisplayName");
        private static readonly PropRef KindP = new PropRef("Humanoid", "DisplayDistanceType");
        private static readonly PropRef DistanceP = new PropRef("Humanoid", "NameDisplayDistance");

        internal Humanoid(ulong id) : base(id) { }

        /// <summary>Current health. Setting it to 0 or less kills the character (Died, then a respawn).</summary>
        public float Health { get => GetFloat(HealthP); set => SetFloat(HealthP, value); }
        public float MaxHealth { get => GetFloat(MaxHealthP); set => SetFloat(MaxHealthP, value); }
        /// <summary>Walking speed (m/s).</summary>
        public float WalkSpeed { get => GetFloat(WalkSpeedP); set => SetFloat(WalkSpeedP, value); }
        /// <summary>Jump height (m).</summary>
        public float JumpHeight { get => GetFloat(JumpHeightP); set => SetFloat(JumpHeightP, value); }
        public float HipHeight { get => GetFloat(HipHeightP); set => SetFloat(HipHeightP, value); }
        public bool AutoRotate { get => GetBool(AutoRotateP); set => SetBool(AutoRotateP, value); }
        public RigType RigType { get => (RigType)GetInt(RigTypeP); set => SetEnum(RigTypeP, (int)value); }
        public string DisplayName { get => GetString(DisplayNameP); set => SetString(DisplayNameP, value); }
        /// <summary>Read-only: maintained by the engine (Idle, Running, Jumping, Freefall, Landed, Dead...).</summary>
        public HumanoidState State => (HumanoidState)GetInt(StateP);
        public Vector3 MoveDirection { get => GetVector3(MoveDirectionP); set => SetVector3(MoveDirectionP, value); }

        /// <summary>Fires once when the health reaches zero.</summary>
        public event Action Died
        {
            add => AddSignal("Died", value);
            remove => RemoveSignal("Died", value);
        }

        /// <summary>Fires with the new health when it changes.</summary>
        public event Action<float> HealthChanged
        {
            add => AddSignal("HealthChanged", value);
            remove => RemoveSignal("HealthChanged", value);
        }

        /// <summary>Fires when this character starts a jump (each character has its own).</summary>
        public event Action Jumping
        {
            add => AddSignal("Jumping", value);
            remove => RemoveSignal("Jumping", value);
        }

        /// <summary>Fires when this character starts falling.</summary>
        public event Action FreeFalling
        {
            add => AddSignal("FreeFalling", value);
            remove => RemoveSignal("FreeFalling", value);
        }

        /// <summary>Fires when this character touches the ground after a jump or a fall.</summary>
        public event Action Landed
        {
            add => AddSignal("Landed", value);
            remove => RemoveSignal("Landed", value);
        }

        /// <summary>Fires on every state change with (old state, new state).</summary>
        public event Action<HumanoidState, HumanoidState> StateChanged
        {
            add => AddSignal("StateChanged", value);
            remove => RemoveSignal("StateChanged", value);
        }

        /// <summary>Removes health (never below zero). Use it for hits; it fires HealthChanged and, at zero, Died.</summary>
        public void TakeDamage(float amount)
        {
            if (amount <= 0f) return;
            Health = Math.Max(0f, Health - amount);
        }
        public HumanoidDisplayDistanceType DisplayDistanceType { get => (HumanoidDisplayDistanceType)GetInt(KindP); set => SetEnum(KindP, (int)value); }
        public float NameDisplayDistance { get => GetFloat(DistanceP); set => SetFloat(DistanceP, value); }
    }

    /// <summary>A person in the game (classes.cpp: Player). Created by the engine at Play start.</summary>
    public class Player : Instance
    {
        private static readonly PropRef DisplayNameP = new PropRef("Player", "DisplayName");
        private static readonly PropRef CharacterP = new PropRef("Player", "Character");
        private static readonly PropRef UserIdP = new PropRef("Player", "UserId");
        private static readonly PropRef RespawnP = new PropRef("Player", "RespawnLocation");

        internal Player(ulong id) : base(id) { }

        public string DisplayName { get => GetString(DisplayNameP); set => SetString(DisplayNameP, value); }
        /// <summary>The avatar Model; null while the character is dead.</summary>
        public Model Character => GetRef(CharacterP) as Model;
        /// <summary>Read-only: 1 for the local player.</summary>
        public int UserId => GetInt(UserIdP);
        /// <summary>Where the character appears after a respawn (set it at a checkpoint).</summary>
        public SpawnLocation RespawnLocation
        {
            get => GetRef(RespawnP) as SpawnLocation;
            set => SetRef(RespawnP, value);
        }

        /// <summary>Fires when the character is created (the argument is the Model).</summary>
        public event Action<Instance> CharacterAdded
        {
            add => AddSignal("CharacterAdded", value);
            remove => RemoveSignal("CharacterAdded", value);
        }

        /// <summary>Fires just before the character is destroyed (the argument is the Model).</summary>
        public event Action<Instance> CharacterRemoving
        {
            add => AddSignal("CharacterRemoving", value);
            remove => RemoveSignal("CharacterRemoving", value);
        }
    }

    /// <summary>The Players service: the players of the running game (one, the local player, in Studio).</summary>
    public class PlayersService : Instance
    {
        private static readonly PropRef RespawnTimeP = new PropRef("Players", "RespawnTime");

        internal PlayersService(ulong id) : base(id) { }

        /// <summary>Seconds before a dead character respawns.</summary>
        public float RespawnTime { get => GetFloat(RespawnTimeP); set => SetFloat(RespawnTimeP, value); }

        /// <summary>The local player (the first Player). Null outside Play mode.</summary>
        public Player LocalPlayer
        {
            get
            {
                foreach (Instance child in GetChildren())
                {
                    Player p = child as Player;
                    if (p != null) return p;
                }
                return null;
            }
        }

        public Player[] GetPlayers()
        {
            var list = new List<Player>();
            foreach (Instance child in GetChildren())
            {
                Player p = child as Player;
                if (p != null) list.Add(p);
            }
            return list.ToArray();
        }

        /// <summary>The player whose Character is the given model, or null (characters of NPCs have no player).</summary>
        public Player GetPlayerFromCharacter(Instance character)
        {
            if (character == null) return null;
            foreach (Player p in GetPlayers())
            {
                Model c = p.Character;
                if (c != null && c.Id == character.Id) return p;
            }
            return null;
        }
    }

    public class PlayerGui : Instance
    {
        internal PlayerGui(ulong id) : base(id) { }
    }

    public class PlayerScripts : Instance
    {
        internal PlayerScripts(ulong id) : base(id) { }
    }

    public class StarterPlayerScripts : Instance
    {
        internal StarterPlayerScripts(ulong id) : base(id) { }
    }

    public class StarterCharacterScripts : Instance
    {
        internal StarterCharacterScripts(ulong id) : base(id) { }
    }

    /// <summary>An object from another tool that Tefra does not support yet (read-only).</summary>
    public class ImportedInstance : Instance
    {
        private static readonly PropRef OriginalClassP = new PropRef("ImportedInstance", "OriginalClass");
        private static readonly PropRef OriginalPropertiesP = new PropRef("ImportedInstance", "OriginalProperties");

        internal ImportedInstance(ulong id) : base(id) { }

        public string OriginalClass => GetString(OriginalClassP);
        public string OriginalProperties => GetString(OriginalPropertiesP);
    }

    // ---- In-game UI (positions: fraction of the parent (X, Y) plus pixel offsets) ----------------------------

    public class GuiBase : Instance
    {
        private static readonly PropRef VisibleP = new PropRef("GuiBase", "Visible");
        private static readonly PropRef ZIndexP = new PropRef("GuiBase", "ZIndex");

        internal GuiBase(ulong id) : base(id) { }

        public bool Visible { get => GetBool(VisibleP); set => SetBool(VisibleP, value); }
        /// <summary>Higher draws on top of its siblings.</summary>
        public int ZIndex { get => GetInt(ZIndexP); set => SetInt(ZIndexP, value); }
    }

    /// <summary>A layer of UI drawn over the 3D view. StarterGui holds the templates; each player gets copies.</summary>
    public class ScreenGui : GuiBase
    {
        private static readonly PropRef EnabledP = new PropRef("ScreenGui", "Enabled");
        private static readonly PropRef DisplayOrderP = new PropRef("ScreenGui", "DisplayOrder");
        private static readonly PropRef ResetOnSpawnP = new PropRef("ScreenGui", "ResetOnSpawn");

        internal ScreenGui(ulong id) : base(id) { }

        public bool Enabled { get => GetBool(EnabledP); set => SetBool(EnabledP, value); }
        public int DisplayOrder { get => GetInt(DisplayOrderP); set => SetInt(DisplayOrderP, value); }
        public bool ResetOnSpawn { get => GetBool(ResetOnSpawnP); set => SetBool(ResetOnSpawnP, value); }
    }

    /// <summary>A rectangle of UI (base class of the visible elements).</summary>
    public class GuiObject : GuiBase
    {
        private static readonly PropRef PositionP = new PropRef("GuiObject", "Position");
        private static readonly PropRef PositionOffsetP = new PropRef("GuiObject", "PositionOffset");
        private static readonly PropRef SizeP = new PropRef("GuiObject", "Size");
        private static readonly PropRef SizeOffsetP = new PropRef("GuiObject", "SizeOffset");
        private static readonly PropRef AnchorPointP = new PropRef("GuiObject", "AnchorPoint");
        private static readonly PropRef RotationP = new PropRef("GuiObject", "Rotation");
        private static readonly PropRef BackgroundColorP = new PropRef("GuiObject", "BackgroundColor");
        private static readonly PropRef BackgroundTransparencyP = new PropRef("GuiObject", "BackgroundTransparency");
        private static readonly PropRef CornerRadiusP = new PropRef("GuiObject", "CornerRadius");
        private static readonly PropRef BorderColorP = new PropRef("GuiObject", "BorderColor");
        private static readonly PropRef BorderSizeP = new PropRef("GuiObject", "BorderSize");
        private static readonly PropRef AnchorP = new PropRef("GuiObject", "AnchorPoint");
        private static readonly PropRef LayoutOrderP = new PropRef("GuiObject", "LayoutOrder");
        private static readonly PropRef ClipsP = new PropRef("GuiObject", "ClipsDescendants");

        internal GuiObject(ulong id) : base(id) { }

        /// <summary>Fraction of the parent's size (X, Y, 0 to 1).</summary>
        public Vector2 Position { get => ToV2(GetVector3(PositionP)); set => SetVector3(PositionP, FromV2(value)); }
        /// <summary>Pixel offset added to Position.</summary>
        public Vector2 PositionOffset { get => ToV2(GetVector3(PositionOffsetP)); set => SetVector3(PositionOffsetP, FromV2(value)); }
        /// <summary>Fraction of the parent's size (X, Y).</summary>
        public Vector2 Size { get => ToV2(GetVector3(SizeP)); set => SetVector3(SizeP, FromV2(value)); }
        /// <summary>Pixel size added to Size.</summary>
        public Vector2 SizeOffset { get => ToV2(GetVector3(SizeOffsetP)); set => SetVector3(SizeOffsetP, FromV2(value)); }
        /// <summary>Pivot inside the object (0 to 1 on each axis).</summary>
        public Vector2 AnchorPoint { get => ToV2(GetVector3(AnchorPointP)); set => SetVector3(AnchorPointP, FromV2(value)); }
        /// <summary>Rotation (degrees).</summary>
        public float Rotation { get => GetFloat(RotationP); set => SetFloat(RotationP, value); }
        public Color3 BackgroundColor { get => GetColor3(BackgroundColorP); set => SetColor3(BackgroundColorP, value); }
        /// <summary>0 = opaque, 1 = invisible.</summary>
        public float BackgroundTransparency { get => GetFloat(BackgroundTransparencyP); set => SetFloat(BackgroundTransparencyP, value); }
        /// <summary>Rounded corners (pixels).</summary>
        public float CornerRadius { get => GetFloat(CornerRadiusP); set => SetFloat(CornerRadiusP, value); }
        public Color3 BorderColor { get => GetColor3(BorderColorP); set => SetColor3(BorderColorP, value); }
        /// <summary>Outline thickness (pixels).</summary>
        public float BorderSize { get => GetFloat(BorderSizeP); set => SetFloat(BorderSizeP, value); }

        internal static Vector2 ToV2(Vector3 v) => new Vector2(v.X, v.Y);
        internal static Vector3 FromV2(Vector2 v) => new Vector3(v.X, v.Y, 0f);
        public int LayoutOrder { get => GetInt(LayoutOrderP); set => SetInt(LayoutOrderP, value); }
        public bool ClipsDescendants { get => GetBool(ClipsP); set => SetBool(ClipsP, value); }
    }

    public class Frame : GuiObject
    {
        internal Frame(ulong id) : base(id) { }
    }

    public class TextLabel : GuiObject
    {
        private static readonly PropRef TextP = new PropRef("TextLabel", "Text");
        private static readonly PropRef TextColorP = new PropRef("TextLabel", "TextColor");
        private static readonly PropRef TextSizeP = new PropRef("TextLabel", "TextSize");
        private static readonly PropRef TextScaledP = new PropRef("TextLabel", "TextScaled");
        private static readonly PropRef TextTransparencyP = new PropRef("TextLabel", "TextTransparency");
        private static readonly PropRef TextStrokeTransparencyP = new PropRef("TextLabel", "TextStrokeTransparency");
        private static readonly PropRef TextXAlignmentP = new PropRef("TextLabel", "TextXAlignment");
        private static readonly PropRef TextYAlignmentP = new PropRef("TextLabel", "TextYAlignment");
        private static readonly PropRef FontP = new PropRef("TextLabel", "Font");
        private static readonly PropRef StrokeP = new PropRef("TextLabel", "TextStrokeTransparency");
        private static readonly PropRef XAlignP = new PropRef("TextLabel", "TextXAlignment");
        private static readonly PropRef YAlignP = new PropRef("TextLabel", "TextYAlignment");

        internal TextLabel(ulong id) : base(id) { }

        public string Text { get => GetString(TextP); set => SetString(TextP, value); }
        public Color3 TextColor { get => GetColor3(TextColorP); set => SetColor3(TextColorP, value); }
        /// <summary>Font size (pixels).</summary>
        public float TextSize { get => GetFloat(TextSizeP); set => SetFloat(TextSizeP, value); }
        /// <summary>Grows the text to fill the box.</summary>
        public bool TextScaled { get => GetBool(TextScaledP); set => SetBool(TextScaledP, value); }
        public float TextTransparency { get => GetFloat(TextTransparencyP); set => SetFloat(TextTransparencyP, value); }
        /// <summary>Outline around the letters (1 = none).</summary>
        public float TextStrokeTransparency { get => GetFloat(TextStrokeTransparencyP); set => SetFloat(TextStrokeTransparencyP, value); }
        public TextXAlignment TextXAlignment { get => (TextXAlignment)GetInt(TextXAlignmentP); set => SetEnum(TextXAlignmentP, (int)value); }
        public TextYAlignment TextYAlignment { get => (TextYAlignment)GetInt(TextYAlignmentP); set => SetEnum(TextYAlignmentP, (int)value); }
        public Font Font { get => (Font)GetInt(FontP); set => SetEnum(FontP, (int)value); }
    }

    public class TextButton : TextLabel
    {
        private static readonly PropRef AutoButtonColorP = new PropRef("TextButton", "AutoButtonColor");
        private static readonly PropRef AutoP = new PropRef("TextButton", "AutoButtonColor");

        internal TextButton(ulong id) : base(id) { }

        /// <summary>Darkens when hovered or pressed.</summary>
        public bool AutoButtonColor { get => GetBool(AutoButtonColorP); set => SetBool(AutoButtonColorP, value); }

        /// <summary>Fires when the button is clicked (after MouseButton1Click).</summary>
        public event Action Activated
        {
            add => AddSignal("Activated", value);
            remove => RemoveSignal("Activated", value);
        }

        public event Action MouseButton1Click
        {
            add => AddSignal("MouseButton1Click", value);
            remove => RemoveSignal("MouseButton1Click", value);
        }

        public event Action MouseEnter
        {
            add => AddSignal("MouseEnter", value);
            remove => RemoveSignal("MouseEnter", value);
        }

        public event Action MouseLeave
        {
            add => AddSignal("MouseLeave", value);
            remove => RemoveSignal("MouseLeave", value);
        }
    }

    public class ImageLabel : GuiObject
    {
        private static readonly PropRef ImageP = new PropRef("ImageLabel", "Image");
        private static readonly PropRef ImageColorP = new PropRef("ImageLabel", "ImageColor");
        private static readonly PropRef ImageTransparencyP = new PropRef("ImageLabel", "ImageTransparency");
        private static readonly PropRef ScaleTypeP = new PropRef("ImageLabel", "ScaleType");
        private static readonly PropRef ScaleP = new PropRef("ImageLabel", "ScaleType");

        internal ImageLabel(ulong id) : base(id) { }

        /// <summary>Image file path (.png or .jpg), relative to the project.</summary>
        public string Image { get => GetString(ImageP); set => SetString(ImageP, value); }
        public Color3 ImageColor { get => GetColor3(ImageColorP); set => SetColor3(ImageColorP, value); }
        public float ImageTransparency { get => GetFloat(ImageTransparencyP); set => SetFloat(ImageTransparencyP, value); }
        public ScaleType ScaleType { get => (ScaleType)GetInt(ScaleTypeP); set => SetEnum(ScaleTypeP, (int)value); }
    }

    public class ImageButton : ImageLabel
    {
        private static readonly PropRef AutoButtonColorP = new PropRef("ImageButton", "AutoButtonColor");
        private static readonly PropRef AutoP = new PropRef("ImageButton", "AutoButtonColor");

        internal ImageButton(ulong id) : base(id) { }

        public bool AutoButtonColor { get => GetBool(AutoButtonColorP); set => SetBool(AutoButtonColorP, value); }

        public event Action Activated
        {
            add => AddSignal("Activated", value);
            remove => RemoveSignal("Activated", value);
        }

        public event Action MouseButton1Click
        {
            add => AddSignal("MouseButton1Click", value);
            remove => RemoveSignal("MouseButton1Click", value);
        }

        public event Action MouseEnter
        {
            add => AddSignal("MouseEnter", value);
            remove => RemoveSignal("MouseEnter", value);
        }

        public event Action MouseLeave
        {
            add => AddSignal("MouseLeave", value);
            remove => RemoveSignal("MouseLeave", value);
        }
    }

    // ---- Audio -------------------------------------------------------------------------------------------------

    /// <summary>Global sound settings.</summary>
    public class SoundService : Instance
    {
        private static readonly PropRef VolumeP = new PropRef("SoundService", "Volume");
        private static readonly PropRef RolloffP = new PropRef("SoundService", "RolloffScale");

        internal SoundService(ulong id) : base(id) { }

        /// <summary>Master volume (0 to 1).</summary>
        public float Volume { get => GetFloat(VolumeP); set => SetFloat(VolumeP, value); }
        /// <summary>Scales the distance attenuation of every 3D sound.</summary>
        public float RolloffScale { get => GetFloat(RolloffP); set => SetFloat(RolloffP, value); }
    }

    /// <summary>Plays an audio file; 3D when parented to a part. Play and Stop set Playing.</summary>
    public class Sound : Instance
    {
        private static readonly PropRef SoundIdP = new PropRef("Sound", "SoundId");
        private static readonly PropRef VolumeP = new PropRef("Sound", "Volume");
        private static readonly PropRef PlaybackSpeedP = new PropRef("Sound", "PlaybackSpeed");
        private static readonly PropRef LoopedP = new PropRef("Sound", "Looped");
        private static readonly PropRef PlayingP = new PropRef("Sound", "Playing");
        private static readonly PropRef PlayOnRemoveP = new PropRef("Sound", "PlayOnRemove");
        private static readonly PropRef RollOffMinP = new PropRef("Sound", "RollOffMinDistance");
        private static readonly PropRef RollOffMaxP = new PropRef("Sound", "RollOffMaxDistance");
        private static readonly PropRef TimePositionP = new PropRef("Sound", "TimePosition");
        private static readonly PropRef TimeLengthP = new PropRef("Sound", "TimeLength");

        internal Sound(ulong id) : base(id) { }

        /// <summary>Audio file path (.wav, .ogg, .mp3, .flac), relative to the project.</summary>
        public string SoundId { get => GetString(SoundIdP); set => SetString(SoundIdP, value); }
        /// <summary>Loudness (0 to 10).</summary>
        public float Volume { get => GetFloat(VolumeP); set => SetFloat(VolumeP, value); }
        public float PlaybackSpeed { get => GetFloat(PlaybackSpeedP); set => SetFloat(PlaybackSpeedP, value); }
        public bool Looped { get => GetBool(LoopedP); set => SetBool(LoopedP, value); }
        /// <summary>True while playing. Setting it starts or stops the sound.</summary>
        public bool Playing { get => GetBool(PlayingP); set => SetBool(PlayingP, value); }
        public bool PlayOnRemove { get => GetBool(PlayOnRemoveP); set => SetBool(PlayOnRemoveP, value); }
        public float RollOffMinDistance { get => GetFloat(RollOffMinP); set => SetFloat(RollOffMinP, value); }
        public float RollOffMaxDistance { get => GetFloat(RollOffMaxP); set => SetFloat(RollOffMaxP, value); }
        /// <summary>Current position (s).</summary>
        public float TimePosition { get => GetFloat(TimePositionP); set => SetFloat(TimePositionP, value); }
        /// <summary>Length of the file (s). Read-only.</summary>
        public float TimeLength => GetFloat(TimeLengthP);

        public void Play() => Playing = true;

        public void Stop() => Playing = false;

        public bool IsPlaying => Playing;

        /// <summary>Fires when a non-looped sound reaches its end.</summary>
        public event Action Ended
        {
            add => AddSignal("Ended", value);
            remove => RemoveSignal("Ended", value);
        }
    }

    /// <summary>Emits camera-facing particles from its parent part (smoke, fire, sparks, dust, magic).</summary>
    public class ParticleEmitter : Instance
    {
        private static readonly PropRef EnabledP = new PropRef("ParticleEmitter", "Enabled");
        private static readonly PropRef RateP = new PropRef("ParticleEmitter", "Rate");
        private static readonly PropRef LifetimeP = new PropRef("ParticleEmitter", "Lifetime");
        private static readonly PropRef SpeedP = new PropRef("ParticleEmitter", "Speed");
        private static readonly PropRef SpreadAngleP = new PropRef("ParticleEmitter", "SpreadAngle");
        private static readonly PropRef VariationP = new PropRef("ParticleEmitter", "Variation");
        private static readonly PropRef TextureP = new PropRef("ParticleEmitter", "Texture");
        private static readonly PropRef ColorP = new PropRef("ParticleEmitter", "Color");
        private static readonly PropRef ColorEndP = new PropRef("ParticleEmitter", "ColorEnd");
        private static readonly PropRef SizeP = new PropRef("ParticleEmitter", "Size");
        private static readonly PropRef SizeEndP = new PropRef("ParticleEmitter", "SizeEnd");
        private static readonly PropRef TransparencyP = new PropRef("ParticleEmitter", "Transparency");
        private static readonly PropRef TransparencyEndP = new PropRef("ParticleEmitter", "TransparencyEnd");
        private static readonly PropRef LightEmissionP = new PropRef("ParticleEmitter", "LightEmission");
        private static readonly PropRef BrightnessP = new PropRef("ParticleEmitter", "Brightness");
        private static readonly PropRef StretchP = new PropRef("ParticleEmitter", "Stretch");
        private static readonly PropRef AccelerationP = new PropRef("ParticleEmitter", "Acceleration");
        private static readonly PropRef DragP = new PropRef("ParticleEmitter", "Drag");
        private static readonly PropRef RotSpeedP = new PropRef("ParticleEmitter", "RotSpeed");
        private static readonly PropRef LockedToPartP = new PropRef("ParticleEmitter", "LockedToPart");
        private static readonly PropRef TimeScaleP = new PropRef("ParticleEmitter", "TimeScale");
        private static readonly PropRef EmissionDirectionP = new PropRef("ParticleEmitter", "EmissionDirection");
        private static readonly PropRef ShapeP = new PropRef("ParticleEmitter", "Shape");
        private static readonly PropRef EmitCountP = new PropRef("ParticleEmitter", "EmitCount");

        internal ParticleEmitter(ulong id) : base(id) { }

        /// <summary>Emits Rate particles per second while true.</summary>
        public bool Enabled { get => GetBool(EnabledP); set => SetBool(EnabledP, value); }
        /// <summary>Particles per second.</summary>
        public float Rate { get => GetFloat(RateP); set => SetFloat(RateP, value); }
        /// <summary>Seconds each particle lives.</summary>
        public float Lifetime { get => GetFloat(LifetimeP); set => SetFloat(LifetimeP, value); }
        /// <summary>Initial speed (m/s) along EmissionDirection.</summary>
        public float Speed { get => GetFloat(SpeedP); set => SetFloat(SpeedP, value); }
        /// <summary>Random deviation from EmissionDirection (degrees).</summary>
        public float SpreadAngle { get => GetFloat(SpreadAngleP); set => SetFloat(SpreadAngleP, value); }
        /// <summary>Random +- fraction applied to lifetime, speed and size.</summary>
        public float Variation { get => GetFloat(VariationP); set => SetFloat(VariationP, value); }
        /// <summary>builtin:circle / smoke / spark / star / fire / snow / ring.</summary>
        public string Texture { get => GetString(TextureP); set => SetString(TextureP, value); }
        /// <summary>Colour at birth.</summary>
        public Color3 Color { get => GetColor3(ColorP); set => SetColor3(ColorP, value); }
        /// <summary>Colour at death.</summary>
        public Color3 ColorEnd { get => GetColor3(ColorEndP); set => SetColor3(ColorEndP, value); }
        /// <summary>Size at birth (m).</summary>
        public float Size { get => GetFloat(SizeP); set => SetFloat(SizeP, value); }
        /// <summary>Size at death (m).</summary>
        public float SizeEnd { get => GetFloat(SizeEndP); set => SetFloat(SizeEndP, value); }
        /// <summary>Transparency at birth.</summary>
        public float Transparency { get => GetFloat(TransparencyP); set => SetFloat(TransparencyP, value); }
        /// <summary>Transparency at death.</summary>
        public float TransparencyEnd { get => GetFloat(TransparencyEndP); set => SetFloat(TransparencyEndP, value); }
        /// <summary>0 = blended, 1 = additive glow.</summary>
        public float LightEmission { get => GetFloat(LightEmissionP); set => SetFloat(LightEmissionP, value); }
        /// <summary>Glow strength of emissive particles.</summary>
        public float Brightness { get => GetFloat(BrightnessP); set => SetFloat(BrightnessP, value); }
        /// <summary>Stretch along the velocity (sparks, rain).</summary>
        public float Stretch { get => GetFloat(StretchP); set => SetFloat(StretchP, value); }
        /// <summary>World acceleration (m/s^2).</summary>
        public Vector3 Acceleration { get => GetVector3(AccelerationP); set => SetVector3(AccelerationP, value); }
        /// <summary>Velocity decay per second.</summary>
        public float Drag { get => GetFloat(DragP); set => SetFloat(DragP, value); }
        /// <summary>Spin (degrees per second).</summary>
        public float RotSpeed { get => GetFloat(RotSpeedP); set => SetFloat(RotSpeedP, value); }
        /// <summary>Particles follow the parent part.</summary>
        public bool LockedToPart { get => GetBool(LockedToPartP); set => SetBool(LockedToPartP, value); }
        /// <summary>Simulation speed (0 freezes).</summary>
        public float TimeScale { get => GetFloat(TimeScaleP); set => SetFloat(TimeScaleP, value); }
        public NormalId EmissionDirection { get => (NormalId)GetInt(EmissionDirectionP); set => SetEnum(EmissionDirectionP, (int)value); }
        public ParticleEmitterShape Shape { get => (ParticleEmitterShape)GetInt(ShapeP); set => SetEnum(ShapeP, (int)value); }

        /// <summary>Emits a burst of count particles now (in addition to Rate).</summary>
        public void Emit(int count = 16)
        {
            if (count > 0) SetInt(EmitCountP, GetInt(EmitCountP) + count);
        }
    }

    // ---- Messaging (Events category). The handlers are in EventTables (Events.cs), keyed by the instance id. ----

    /// <summary>Fires an event to the scripts connected to it. Handlers run inside Fire, with its arguments.</summary>
    public class BindableEvent : Instance
    {
        internal BindableEvent(ulong id) : base(id) { }

        /// <summary>Called by Fire with its arguments, in the order the handlers were added.</summary>
        public event Action<object[]> Event
        {
            add => EventTables.BindableEvents.Add(Id, value);
            remove => EventTables.BindableEvents.Remove(Id, value);
        }

        /// <summary>Same as adding a handler to Event.</summary>
        public void Connect(Action<object[]> handler) => EventTables.BindableEvents.Add(Id, handler);

        /// <summary>Runs every handler now. A destroyed BindableEvent fires nothing.</summary>
        public void Fire(params object[] args)
        {
            if (!Native.Exists(Id)) return;
            EventTables.BindableEvents.Each(Id, "BindableEvent.Event", fn => ((Action<object[]>)fn)(args));
        }
    }

    /// <summary>A function other scripts call with Invoke. Set OnInvoke to answer.</summary>
    public class BindableFunction : Instance
    {
        internal BindableFunction(ulong id) : base(id) { }

        /// <summary>Receives the arguments of Invoke and returns the result.</summary>
        public Func<object[], object> OnInvoke
        {
            get => EventTables.BindableFunctions.Get(Id).Fn as Func<object[], object>;
            set => EventTables.BindableFunctions.Set(Id, value);
        }

        /// <summary>Calls OnInvoke now and returns its result (null when OnInvoke is not set).</summary>
        public object Invoke(params object[] args) =>
            EventTables.BindableFunctions.Call(Id, fn => ((Func<object[], object>)fn)(args));
    }

    /// <summary>Events between the server and the client. Both run in this process: the message is queued and
    /// delivered at the next script step, as it would be over a network.</summary>
    public class RemoteEvent : Instance
    {
        internal RemoteEvent(ulong id) : base(id) { }

        /// <summary>Server side. Gets the sending player (the local player) and the arguments of FireServer.</summary>
        public event Action<Player, object[]> OnServerEvent
        {
            add => EventTables.ServerRemotes.Add(Id, value);
            remove => EventTables.ServerRemotes.Remove(Id, value);
        }

        /// <summary>Client side. Gets the arguments of FireClient or FireAllClients.</summary>
        public event Action<object[]> OnClientEvent
        {
            add => EventTables.ClientRemotes.Add(Id, value);
            remove => EventTables.ClientRemotes.Remove(Id, value);
        }

        /// <summary>Queues the arguments for OnServerEvent, with the local player as the sender.</summary>
        public void FireServer(params object[] args) => Remoting.Enqueue(Id, true, Players.LocalPlayer, args);

        /// <summary>Queues the arguments for OnClientEvent. A single process has one client, so every player
        /// receives it.</summary>
        public void FireClient(Player player, params object[] args)
        {
            if (player == null)
            {
                Native.Log(Native.LogWarn, "RemoteEvent.FireClient: no player given, the event was dropped");
                return;
            }
            Remoting.Enqueue(Id, false, null, args);
        }

        /// <summary>Queues the arguments for OnClientEvent of the client.</summary>
        public void FireAllClients(params object[] args) => Remoting.Enqueue(Id, false, null, args);
    }

    /// <summary>A call with an answer. Set OnServerInvoke / OnClientInvoke to answer the other side.</summary>
    public class RemoteFunction : Instance
    {
        internal RemoteFunction(ulong id) : base(id) { }

        /// <summary>Answers InvokeServer. Gets the calling player (the local player) and the arguments.</summary>
        public Func<Player, object[], object> OnServerInvoke
        {
            get => EventTables.ServerInvokers.Get(Id).Fn as Func<Player, object[], object>;
            set => EventTables.ServerInvokers.Set(Id, value);
        }

        /// <summary>Answers InvokeClient. Gets the arguments.</summary>
        public Func<object[], object> OnClientInvoke
        {
            get => EventTables.ClientInvokers.Get(Id).Fn as Func<object[], object>;
            set => EventTables.ClientInvokers.Set(Id, value);
        }

        /// <summary>Calls OnServerInvoke now, as the local player, and returns its result (null when unset).</summary>
        public object InvokeServer(params object[] args) =>
            EventTables.ServerInvokers.Call(Id, fn => ((Func<Player, object[], object>)fn)(Players.LocalPlayer, args));

        /// <summary>Calls OnClientInvoke now and returns its result (null when unset or when no player is given).</summary>
        public object InvokeClient(Player player, params object[] args)
        {
            if (player == null)
            {
                Native.Log(Native.LogWarn, "RemoteFunction.InvokeClient: no player given");
                return null;
            }
            return EventTables.ClientInvokers.Call(Id, fn => ((Func<object[], object>)fn)(args));
        }
    }

    // Factory table: the most derived wrapper for each class name. Classes without a wrapper use Instance.

    public enum HorizontalAlignment { Left, Center, Right }

    public enum VerticalAlignment { Top, Center, Bottom }

    public enum FillDirection { Vertical, Horizontal }

    public enum SortOrder { LayoutOrder, Name }

    public enum HumanoidDisplayDistanceType { Viewer, Subject, None }

    public enum NormalId { Top, Bottom, Front, Back, Left, Right }

    public enum ParticleEmitterShape { Box, Sphere, Disc, Point }

    /// <summary>A UI layer drawn over the world, positioned at an Adornee (or the parent part).</summary>
    public class BillboardGui : GuiBase
    {
        private static readonly PropRef EnabledP = new PropRef("BillboardGui", "Enabled");
        private static readonly PropRef AdorneeP = new PropRef("BillboardGui", "Adornee");
        private static readonly PropRef SizeP = new PropRef("BillboardGui", "Size");
        private static readonly PropRef OffsetP = new PropRef("BillboardGui", "StudsOffset");
        private static readonly PropRef AlwaysOnTopP = new PropRef("BillboardGui", "AlwaysOnTop");
        private static readonly PropRef MaxDistanceP = new PropRef("BillboardGui", "MaxDistance");

        internal BillboardGui(ulong id) : base(id) { }

        public bool Enabled { get => GetBool(EnabledP); set => SetBool(EnabledP, value); }
        public Instance Adornee { get => GetRef(AdorneeP); set => SetRef(AdorneeP, value); }
        /// <summary>Size on screen in pixels (x, y).</summary>
        public Vector3 Size { get => GetVector3(SizeP); set => SetVector3(SizeP, value); }
        /// <summary>Offset from the Adornee in metres.</summary>
        public Vector3 StudsOffset { get => GetVector3(OffsetP); set => SetVector3(OffsetP, value); }
        public bool AlwaysOnTop { get => GetBool(AlwaysOnTopP); set => SetBool(AlwaysOnTopP, value); }
        public float MaxDistance { get => GetFloat(MaxDistanceP); set => SetFloat(MaxDistanceP, value); }
    }

    /// <summary>Editable text. FocusLost fires with number 1 when Enter was pressed.</summary>
    public class TextBox : TextLabel
    {
        private static readonly PropRef PlaceholderP = new PropRef("TextBox", "PlaceholderText");
        private static readonly PropRef ClearP = new PropRef("TextBox", "ClearTextOnFocus");

        internal TextBox(ulong id) : base(id) { }

        public string PlaceholderText { get => GetString(PlaceholderP); set => SetString(PlaceholderP, value); }
        public bool ClearTextOnFocus { get => GetBool(ClearP); set => SetBool(ClearP, value); }
    }

    public class UICorner : Instance
    {
        private static readonly PropRef RadiusP = new PropRef("UICorner", "CornerRadius");

        internal UICorner(ulong id) : base(id) { }

        /// <summary>Corner radius in pixels; overrides the parent's CornerRadius.</summary>
        public float CornerRadius { get => GetFloat(RadiusP); set => SetFloat(RadiusP, value); }
    }

    public class UIStroke : Instance
    {
        private static readonly PropRef ColorP = new PropRef("UIStroke", "Color");
        private static readonly PropRef ThicknessP = new PropRef("UIStroke", "Thickness");
        private static readonly PropRef TransparencyP = new PropRef("UIStroke", "Transparency");

        internal UIStroke(ulong id) : base(id) { }

        public Color3 Color { get => GetColor3(ColorP); set => SetColor3(ColorP, value); }
        public float Thickness { get => GetFloat(ThicknessP); set => SetFloat(ThicknessP, value); }
        public float Transparency { get => GetFloat(TransparencyP); set => SetFloat(TransparencyP, value); }
    }

    public class UIPadding : Instance
    {
        private static readonly PropRef LeftP = new PropRef("UIPadding", "PaddingLeft");
        private static readonly PropRef RightP = new PropRef("UIPadding", "PaddingRight");
        private static readonly PropRef TopP = new PropRef("UIPadding", "PaddingTop");
        private static readonly PropRef BottomP = new PropRef("UIPadding", "PaddingBottom");

        internal UIPadding(ulong id) : base(id) { }

        public float PaddingLeft { get => GetFloat(LeftP); set => SetFloat(LeftP, value); }
        public float PaddingRight { get => GetFloat(RightP); set => SetFloat(RightP, value); }
        public float PaddingTop { get => GetFloat(TopP); set => SetFloat(TopP, value); }
        public float PaddingBottom { get => GetFloat(BottomP); set => SetFloat(BottomP, value); }
    }

    public class UIListLayout : Instance
    {
        private static readonly PropRef FillP = new PropRef("UIListLayout", "FillDirection");
        private static readonly PropRef PaddingP = new PropRef("UIListLayout", "Padding");
        private static readonly PropRef HAlignP = new PropRef("UIListLayout", "HorizontalAlignment");
        private static readonly PropRef VAlignP = new PropRef("UIListLayout", "VerticalAlignment");
        private static readonly PropRef SortP = new PropRef("UIListLayout", "SortOrder");

        internal UIListLayout(ulong id) : base(id) { }

        public FillDirection FillDirection { get => (FillDirection)GetInt(FillP); set => SetEnum(FillP, (int)value); }
        /// <summary>Gap between children in pixels.</summary>
        public float Padding { get => GetFloat(PaddingP); set => SetFloat(PaddingP, value); }
        public HorizontalAlignment HorizontalAlignment { get => (HorizontalAlignment)GetInt(HAlignP); set => SetEnum(HAlignP, (int)value); }
        public VerticalAlignment VerticalAlignment { get => (VerticalAlignment)GetInt(VAlignP); set => SetEnum(VAlignP, (int)value); }
        public SortOrder SortOrder { get => (SortOrder)GetInt(SortP); set => SetEnum(SortP, (int)value); }
    }

    public class UIGridLayout : Instance
    {
        private static readonly PropRef CellSizeP = new PropRef("UIGridLayout", "CellSize");
        private static readonly PropRef CellPaddingP = new PropRef("UIGridLayout", "CellPadding");
        private static readonly PropRef FillP = new PropRef("UIGridLayout", "FillDirection");
        private static readonly PropRef HAlignP = new PropRef("UIGridLayout", "HorizontalAlignment");
        private static readonly PropRef VAlignP = new PropRef("UIGridLayout", "VerticalAlignment");
        private static readonly PropRef SortP = new PropRef("UIGridLayout", "SortOrder");

        internal UIGridLayout(ulong id) : base(id) { }

        /// <summary>Cell size in pixels (x, y).</summary>
        public Vector3 CellSize { get => GetVector3(CellSizeP); set => SetVector3(CellSizeP, value); }
        public Vector3 CellPadding { get => GetVector3(CellPaddingP); set => SetVector3(CellPaddingP, value); }
        public FillDirection FillDirection { get => (FillDirection)GetInt(FillP); set => SetEnum(FillP, (int)value); }
        public HorizontalAlignment HorizontalAlignment { get => (HorizontalAlignment)GetInt(HAlignP); set => SetEnum(HAlignP, (int)value); }
        public VerticalAlignment VerticalAlignment { get => (VerticalAlignment)GetInt(VAlignP); set => SetEnum(VAlignP, (int)value); }
        public SortOrder SortOrder { get => (SortOrder)GetInt(SortP); set => SetEnum(SortP, (int)value); }
    }

    public class UIGradient : Instance
    {
        private static readonly PropRef Color0P = new PropRef("UIGradient", "Color0");
        private static readonly PropRef Color1P = new PropRef("UIGradient", "Color1");
        private static readonly PropRef RotationP = new PropRef("UIGradient", "Rotation");

        internal UIGradient(ulong id) : base(id) { }

        public Color3 Color0 { get => GetColor3(Color0P); set => SetColor3(Color0P, value); }
        public Color3 Color1 { get => GetColor3(Color1P); set => SetColor3(Color1P, value); }
        /// <summary>Degrees: 0 = left to right, 90 = top to bottom.</summary>
        public float Rotation { get => GetFloat(RotationP); set => SetFloat(RotationP, value); }
    }

    public class UIAspectRatioConstraint : Instance
    {
        private static readonly PropRef RatioP = new PropRef("UIAspectRatioConstraint", "AspectRatio");

        internal UIAspectRatioConstraint(ulong id) : base(id) { }

        /// <summary>Width divided by height.</summary>
        public float AspectRatio { get => GetFloat(RatioP); set => SetFloat(RatioP, value); }
    }

    // ---- Value objects (Roblox *Value; engine/world/src/classes.cpp) -----------------------------------------------

    /// <summary>Base of the value objects: one typed value each (`Value` property).</summary>
    public abstract class ValueBase : Instance
    {
        internal ValueBase(ulong id) : base(id) { }
    }

    public class StringValue : ValueBase
    {
        private static readonly PropRef ValueP = new PropRef("StringValue", "Value");

        internal StringValue(ulong id) : base(id) { }

        public string Value { get => GetString(ValueP); set => SetString(ValueP, value); }
    }

    public class IntValue : ValueBase
    {
        private static readonly PropRef ValueP = new PropRef("IntValue", "Value");

        internal IntValue(ulong id) : base(id) { }

        public int Value { get => GetInt(ValueP); set => SetInt(ValueP, value); }
    }

    public class NumberValue : ValueBase
    {
        private static readonly PropRef ValueP = new PropRef("NumberValue", "Value");

        internal NumberValue(ulong id) : base(id) { }

        public float Value { get => GetFloat(ValueP); set => SetFloat(ValueP, value); }
    }

    public class BoolValue : ValueBase
    {
        private static readonly PropRef ValueP = new PropRef("BoolValue", "Value");

        internal BoolValue(ulong id) : base(id) { }

        public bool Value { get => GetBool(ValueP); set => SetBool(ValueP, value); }
    }

    public class Vector3Value : ValueBase
    {
        private static readonly PropRef ValueP = new PropRef("Vector3Value", "Value");

        internal Vector3Value(ulong id) : base(id) { }

        public Vector3 Value { get => GetVector3(ValueP); set => SetVector3(ValueP, value); }
    }

    public class Color3Value : ValueBase
    {
        private static readonly PropRef ValueP = new PropRef("Color3Value", "Value");

        internal Color3Value(ulong id) : base(id) { }

        public Color3 Value { get => GetColor3(ValueP); set => SetColor3(ValueP, value); }
    }

    /// <summary>Holds a reference to another instance (nil = null).</summary>
    public class ObjectValue : ValueBase
    {
        private static readonly PropRef ValueP = new PropRef("ObjectValue", "Value");

        internal ObjectValue(ulong id) : base(id) { }

        public Instance Value { get => GetRef(ValueP); set => SetRef(ValueP, value); }
    }

    /// <summary>A position and an orientation (Roblox CFrame, kept as Position + Orientation in degrees).</summary>
    public class CFrameValue : ValueBase
    {
        private static readonly PropRef PositionP = new PropRef("CFrameValue", "Position");
        private static readonly PropRef OrientationP = new PropRef("CFrameValue", "Orientation");

        internal CFrameValue(ulong id) : base(id) { }

        public Vector3 Position { get => GetVector3(PositionP); set => SetVector3(PositionP, value); }
        public Vector3 Orientation { get => GetVector3(OrientationP); set => SetVector3(OrientationP, value); }
    }

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
                                ["BillboardGui"] = id => new BillboardGui(id),
                ["TextBox"] = id => new TextBox(id),
                ["UICorner"] = id => new UICorner(id),
                ["UIStroke"] = id => new UIStroke(id),
                ["UIPadding"] = id => new UIPadding(id),
                ["UIListLayout"] = id => new UIListLayout(id),
                ["UIGridLayout"] = id => new UIGridLayout(id),
                ["UIGradient"] = id => new UIGradient(id),
                ["UIAspectRatioConstraint"] = id => new UIAspectRatioConstraint(id),
["Humanoid"] = id => new Humanoid(id),
                ["Player"] = id => new Player(id),
                ["Players"] = id => new PlayersService(id),
                ["PlayerGui"] = id => new PlayerGui(id),
                ["PlayerScripts"] = id => new PlayerScripts(id),
                ["StarterPlayerScripts"] = id => new StarterPlayerScripts(id),
                ["StarterCharacterScripts"] = id => new StarterCharacterScripts(id),
                ["ImportedInstance"] = id => new ImportedInstance(id),
                ["GuiBase"] = id => new GuiBase(id),
                ["ScreenGui"] = id => new ScreenGui(id),
                ["GuiObject"] = id => new GuiObject(id),
                ["Frame"] = id => new Frame(id),
                ["TextLabel"] = id => new TextLabel(id),
                ["TextButton"] = id => new TextButton(id),
                ["ImageLabel"] = id => new ImageLabel(id),
                ["ImageButton"] = id => new ImageButton(id),
                ["SoundService"] = id => new SoundService(id),
                ["Sound"] = id => new Sound(id),
                ["ParticleEmitter"] = id => new ParticleEmitter(id),
                ["BindableEvent"] = id => new BindableEvent(id),
                ["BindableFunction"] = id => new BindableFunction(id),
                ["RemoteEvent"] = id => new RemoteEvent(id),
                ["RemoteFunction"] = id => new RemoteFunction(id),
                ["StringValue"] = id => new StringValue(id),
                ["IntValue"] = id => new IntValue(id),
                ["NumberValue"] = id => new NumberValue(id),
                ["BoolValue"] = id => new BoolValue(id),
                ["Vector3Value"] = id => new Vector3Value(id),
                ["Color3Value"] = id => new Color3Value(id),
                ["ObjectValue"] = id => new ObjectValue(id),
                ["CFrameValue"] = id => new CFrameValue(id),
            };
    }
}
