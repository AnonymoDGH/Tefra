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

    // In-game UI enums (engine/world/src/classes.cpp): same order as the native lists.
    public enum TextXAlignment { Left, Center, Right }
    public enum TextYAlignment { Top, Center, Bottom }
    public enum GuiFont { Default, Bold, Mono }
    public enum ScaleType { Stretch, Fit, Crop }
    public enum HorizontalAlignment { Left, Center, Right }
    public enum VerticalAlignment { Top, Center, Bottom }
    public enum FillDirection { Vertical, Horizontal }
    public enum SortOrder { LayoutOrder, Name }
    public enum HumanoidDisplayDistanceType { Viewer, Subject, None }

    // ---- In-game UI -------------------------------------------------------------------------------------------

    /// <summary>Base of every 2D UI object (ScreenGui, BillboardGui, GuiObject).</summary>
    public class GuiBase : Instance
    {
        private static readonly PropRef VisibleP = new PropRef("GuiBase", "Visible");
        private static readonly PropRef ZIndexP = new PropRef("GuiBase", "ZIndex");

        internal GuiBase(ulong id) : base(id) { }

        public bool Visible { get => GetBool(VisibleP); set => SetBool(VisibleP, value); }
        public int ZIndex { get => GetInt(ZIndexP); set => SetInt(ZIndexP, value); }
    }

    public class ScreenGui : GuiBase
    {
        private static readonly PropRef EnabledP = new PropRef("ScreenGui", "Enabled");
        private static readonly PropRef DisplayOrderP = new PropRef("ScreenGui", "DisplayOrder");

        internal ScreenGui(ulong id) : base(id) { }

        public bool Enabled { get => GetBool(EnabledP); set => SetBool(EnabledP, value); }
        public int DisplayOrder { get => GetInt(DisplayOrderP); set => SetInt(DisplayOrderP, value); }
    }

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

    /// <summary>A rectangle of UI. Position and Size are scale (x, y) plus pixel offsets.</summary>
    public class GuiObject : GuiBase
    {
        private static readonly PropRef PositionP = new PropRef("GuiObject", "Position");
        private static readonly PropRef PositionOffsetP = new PropRef("GuiObject", "PositionOffset");
        private static readonly PropRef SizeP = new PropRef("GuiObject", "Size");
        private static readonly PropRef SizeOffsetP = new PropRef("GuiObject", "SizeOffset");
        private static readonly PropRef AnchorP = new PropRef("GuiObject", "AnchorPoint");
        private static readonly PropRef RotationP = new PropRef("GuiObject", "Rotation");
        private static readonly PropRef LayoutOrderP = new PropRef("GuiObject", "LayoutOrder");
        private static readonly PropRef ClipsP = new PropRef("GuiObject", "ClipsDescendants");
        private static readonly PropRef BackgroundColorP = new PropRef("GuiObject", "BackgroundColor");
        private static readonly PropRef BackgroundTransparencyP = new PropRef("GuiObject", "BackgroundTransparency");
        private static readonly PropRef CornerRadiusP = new PropRef("GuiObject", "CornerRadius");
        private static readonly PropRef BorderColorP = new PropRef("GuiObject", "BorderColor");
        private static readonly PropRef BorderSizeP = new PropRef("GuiObject", "BorderSize");

        internal GuiObject(ulong id) : base(id) { }

        public Vector3 Position { get => GetVector3(PositionP); set => SetVector3(PositionP, value); }
        public Vector3 PositionOffset { get => GetVector3(PositionOffsetP); set => SetVector3(PositionOffsetP, value); }
        public Vector3 Size { get => GetVector3(SizeP); set => SetVector3(SizeP, value); }
        public Vector3 SizeOffset { get => GetVector3(SizeOffsetP); set => SetVector3(SizeOffsetP, value); }
        public Vector3 AnchorPoint { get => GetVector3(AnchorP); set => SetVector3(AnchorP, value); }
        /// <summary>Degrees, clockwise on screen, around the centre.</summary>
        public float Rotation { get => GetFloat(RotationP); set => SetFloat(RotationP, value); }
        public int LayoutOrder { get => GetInt(LayoutOrderP); set => SetInt(LayoutOrderP, value); }
        public bool ClipsDescendants { get => GetBool(ClipsP); set => SetBool(ClipsP, value); }
        public Color3 BackgroundColor { get => GetColor3(BackgroundColorP); set => SetColor3(BackgroundColorP, value); }
        public float BackgroundTransparency { get => GetFloat(BackgroundTransparencyP); set => SetFloat(BackgroundTransparencyP, value); }
        public float CornerRadius { get => GetFloat(CornerRadiusP); set => SetFloat(CornerRadiusP, value); }
        public Color3 BorderColor { get => GetColor3(BorderColorP); set => SetColor3(BorderColorP, value); }
        public float BorderSize { get => GetFloat(BorderSizeP); set => SetFloat(BorderSizeP, value); }
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
        private static readonly PropRef StrokeP = new PropRef("TextLabel", "TextStrokeTransparency");
        private static readonly PropRef XAlignP = new PropRef("TextLabel", "TextXAlignment");
        private static readonly PropRef YAlignP = new PropRef("TextLabel", "TextYAlignment");
        private static readonly PropRef FontP = new PropRef("TextLabel", "Font");

        internal TextLabel(ulong id) : base(id) { }

        public string Text { get => GetString(TextP); set => SetString(TextP, value); }
        public Color3 TextColor { get => GetColor3(TextColorP); set => SetColor3(TextColorP, value); }
        /// <summary>Font size in pixels.</summary>
        public float TextSize { get => GetFloat(TextSizeP); set => SetFloat(TextSizeP, value); }
        public bool TextScaled { get => GetBool(TextScaledP); set => SetBool(TextScaledP, value); }
        public float TextTransparency { get => GetFloat(TextTransparencyP); set => SetFloat(TextTransparencyP, value); }
        public float TextStrokeTransparency { get => GetFloat(StrokeP); set => SetFloat(StrokeP, value); }
        public TextXAlignment TextXAlignment { get => (TextXAlignment)GetInt(XAlignP); set => SetEnum(XAlignP, (int)value); }
        public TextYAlignment TextYAlignment { get => (TextYAlignment)GetInt(YAlignP); set => SetEnum(YAlignP, (int)value); }
        public GuiFont Font { get => (GuiFont)GetInt(FontP); set => SetEnum(FontP, (int)value); }
    }

    public class TextButton : TextLabel
    {
        private static readonly PropRef AutoP = new PropRef("TextButton", "AutoButtonColor");

        internal TextButton(ulong id) : base(id) { }

        public bool AutoButtonColor { get => GetBool(AutoP); set => SetBool(AutoP, value); }
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

    public class ImageLabel : GuiObject
    {
        private static readonly PropRef ImageP = new PropRef("ImageLabel", "Image");
        private static readonly PropRef ImageColorP = new PropRef("ImageLabel", "ImageColor");
        private static readonly PropRef ImageTransparencyP = new PropRef("ImageLabel", "ImageTransparency");
        private static readonly PropRef ScaleP = new PropRef("ImageLabel", "ScaleType");

        internal ImageLabel(ulong id) : base(id) { }

        /// <summary>Image file path (relative to the project folder).</summary>
        public string Image { get => GetString(ImageP); set => SetString(ImageP, value); }
        public Color3 ImageColor { get => GetColor3(ImageColorP); set => SetColor3(ImageColorP, value); }
        public float ImageTransparency { get => GetFloat(ImageTransparencyP); set => SetFloat(ImageTransparencyP, value); }
        public ScaleType ScaleType { get => (ScaleType)GetInt(ScaleP); set => SetEnum(ScaleP, (int)value); }
    }

    public class ImageButton : ImageLabel
    {
        private static readonly PropRef AutoP = new PropRef("ImageButton", "AutoButtonColor");

        internal ImageButton(ulong id) : base(id) { }

        public bool AutoButtonColor { get => GetBool(AutoP); set => SetBool(AutoP, value); }
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

    public class Humanoid : Instance
    {
        private static readonly PropRef NameP = new PropRef("Humanoid", "DisplayName");
        private static readonly PropRef KindP = new PropRef("Humanoid", "DisplayDistanceType");
        private static readonly PropRef DistanceP = new PropRef("Humanoid", "NameDisplayDistance");

        internal Humanoid(ulong id) : base(id) { }

        public string DisplayName { get => GetString(NameP); set => SetString(NameP, value); }
        public HumanoidDisplayDistanceType DisplayDistanceType { get => (HumanoidDisplayDistanceType)GetInt(KindP); set => SetEnum(KindP, (int)value); }
        public float NameDisplayDistance { get => GetFloat(DistanceP); set => SetFloat(DistanceP, value); }
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
                ["GuiBase"] = id => new GuiBase(id),
                ["ScreenGui"] = id => new ScreenGui(id),
                ["BillboardGui"] = id => new BillboardGui(id),
                ["GuiObject"] = id => new GuiObject(id),
                ["Frame"] = id => new Frame(id),
                ["TextLabel"] = id => new TextLabel(id),
                ["TextButton"] = id => new TextButton(id),
                ["TextBox"] = id => new TextBox(id),
                ["ImageLabel"] = id => new ImageLabel(id),
                ["ImageButton"] = id => new ImageButton(id),
                ["UICorner"] = id => new UICorner(id),
                ["UIStroke"] = id => new UIStroke(id),
                ["UIPadding"] = id => new UIPadding(id),
                ["UIListLayout"] = id => new UIListLayout(id),
                ["UIGridLayout"] = id => new UIGridLayout(id),
                ["UIGradient"] = id => new UIGradient(id),
                ["UIAspectRatioConstraint"] = id => new UIAspectRatioConstraint(id),
                ["Humanoid"] = id => new Humanoid(id),
            };
    }
}
