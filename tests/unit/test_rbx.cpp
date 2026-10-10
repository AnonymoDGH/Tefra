#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/rbx/document.h"
#include "tefra/rbx/importer.h"
#include "tefra/world/datamodel.h"

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

// Roblox importer (ADR-017). The sample files are built in code: a minimal binary writer (uncompressed chunks, an
// LZ4 literal/match chunk and a hand-made zstd raw block) and XML string literals. No Roblox file is involved.

using namespace tefra;
using namespace tefra::rbx;
using tefra::world::ClassRegistry;
using tefra::world::DataModel;
using tefra::world::Instance;

namespace {

using Bytes = std::vector<u8>;

// ---- Binary writer ---------------------------------------------------------------------------------------------

void put8(Bytes& o, u32 v) { o.push_back(static_cast<u8>(v & 0xffu)); }
void put16le(Bytes& o, u32 v) {
    put8(o, v);
    put8(o, v >> 8);
}
void put32le(Bytes& o, u32 v) {
    for (u32 s = 0; s < 32; s += 8) put8(o, v >> s);
}
void putstr(Bytes& o, const std::string& s) {
    put32le(o, static_cast<u32>(s.size()));
    o.insert(o.end(), s.begin(), s.end());
}
u32 zigzag(i32 v) {
    const u32 u = static_cast<u32>(v);
    return (u << 1) ^ (v < 0 ? 0xffffffffu : 0u);
}
void put_be_interleaved(Bytes& o, const std::vector<u32>& w) {
    for (u32 j = 0; j < 4; ++j)
        for (u32 x : w) put8(o, x >> (24 - 8 * j));
}
void put_i32s(Bytes& o, const std::vector<i32>& v) {
    std::vector<u32> w;
    for (i32 x : v) w.push_back(zigzag(x));
    put_be_interleaved(o, w);
}
void put_refs(Bytes& o, const std::vector<i32>& v) {
    std::vector<u32> w;
    u32 prev = 0;
    for (i32 x : v) {
        const u32 u = static_cast<u32>(x);
        w.push_back(zigzag(static_cast<i32>(u - prev)));
        prev = u;
    }
    put_be_interleaved(o, w);
}
u32 rot_float(f32 f) {
    u32 b = 0;
    std::memcpy(&b, &f, sizeof b);
    return (b << 1) | (b >> 31);
}
void put_f32s(Bytes& o, const std::vector<f32>& v) {
    std::vector<u32> w;
    for (f32 x : v) w.push_back(rot_float(x));
    put_be_interleaved(o, w);
}
void put_f32le(Bytes& o, f32 f) {
    u32 b = 0;
    std::memcpy(&b, &f, sizeof b);
    put32le(o, b);
}

// LZ4 block with one literal-only sequence.
Bytes lz4_literal(const Bytes& in) {
    Bytes o;
    const usize n = in.size();
    if (n < 15) {
        o.push_back(static_cast<u8>(n << 4));
    } else {
        o.push_back(0xF0);
        usize rest = n - 15;
        while (rest >= 255) {
            o.push_back(255);
            rest -= 255;
        }
        o.push_back(static_cast<u8>(rest));
    }
    o.insert(o.end(), in.begin(), in.end());
    return o;
}

// zstd frame with one raw block (single-segment frame, 4-byte content size).
Bytes zstd_raw(const Bytes& in) {
    Bytes o = {0x28, 0xB5, 0x2F, 0xFD, 0xA0};
    put32le(o, static_cast<u32>(in.size()));
    const u32 block = 1u | (static_cast<u32>(in.size()) << 3); // last block, raw type
    put8(o, block);
    put8(o, block >> 8);
    put8(o, block >> 16);
    o.insert(o.end(), in.begin(), in.end());
    return o;
}

enum class Comp { None, Lz4, Zstd, Mixed };

void put_chunk(Bytes& file, const char* name4, const Bytes& payload, Comp comp, u32& counter) {
    Comp c = comp;
    if (comp == Comp::Mixed) {
        const u32 k = counter++ % 3;
        c = k == 0 ? Comp::None : (k == 1 ? Comp::Lz4 : Comp::Zstd);
    }
    Bytes body;
    if (c == Comp::Lz4)
        body = lz4_literal(payload);
    else if (c == Comp::Zstd)
        body = zstd_raw(payload);
    else
        body = payload;
    for (int i = 0; i < 4; ++i) file.push_back(static_cast<u8>(name4[i]));
    put32le(file, c == Comp::None ? 0u : static_cast<u32>(body.size()));
    put32le(file, static_cast<u32>(payload.size()));
    put32le(file, 0);
    file.insert(file.end(), body.begin(), body.end());
}

// A value of one property, for one instance. Defaults: identity CFrame, zero everything else.
struct V {
    std::string s;
    bool b = false;
    i32 i = 0;
    u32 e = 0;
    int cf = 2; // rotation id (0 = explicit matrix in f[3..11], 2 = identity)
    f32 f[12] = {};
    i32 iv[2] = {};
    u8 c[3] = {};
    i32 ref = -1;
};

struct Obj {
    std::string cls;
    std::string name;
    i32 ref;
    i32 parent;
    bool service = false;
};

struct PropDef {
    std::string cls;
    std::string name;
    u8 type;
    std::vector<std::pair<i32, V>> vals; // by referent; missing instances get the default V
};

V fv(f32 x) {
    V v;
    v.f[0] = x;
    return v;
}
V vec(f32 x, f32 y, f32 z) {
    V v;
    v.f[0] = x;
    v.f[1] = y;
    v.f[2] = z;
    return v;
}
V cframe(f32 x, f32 y, f32 z) {
    V v = vec(x, y, z);
    v.cf = 2;
    return v;
}
V en(u32 e) {
    V v;
    v.e = e;
    return v;
}
V str(const std::string& s) {
    V v;
    v.s = s;
    return v;
}
V bl(bool b) {
    V v;
    v.b = b;
    return v;
}
V ref(i32 r) {
    V v;
    v.ref = r;
    return v;
}
V ud2(f32 xs, f32 ys, i32 xo, i32 yo) {
    V v;
    v.f[0] = xs;
    v.f[1] = ys;
    v.iv[0] = xo;
    v.iv[1] = yo;
    return v;
}
V rgb(u8 r, u8 g, u8 b) {
    V v;
    v.c[0] = r;
    v.c[1] = g;
    v.c[2] = b;
    return v;
}

// Encodes the values of one property (type ids of the format: 01 string, 02 bool, 03 int32, 04 float32, 07 UDim2,
// 0D Vector2, 0E Vector3, 10 CFrame, 12 enum, 13 referent, 1A Color3uint8).
void encode_prop(Bytes& o, u8 type, const std::vector<V>& vals) {
    switch (type) {
    case 0x01:
        for (const V& x : vals) putstr(o, x.s);
        break;
    case 0x02:
        for (const V& x : vals) put8(o, x.b ? 1u : 0u);
        break;
    case 0x03: {
        std::vector<i32> a;
        for (const V& x : vals) a.push_back(x.i);
        put_i32s(o, a);
        break;
    }
    case 0x04: {
        std::vector<f32> a;
        for (const V& x : vals) a.push_back(x.f[0]);
        put_f32s(o, a);
        break;
    }
    case 0x0E:
    case 0x0D: {
        const int axes = type == 0x0E ? 3 : 2;
        for (int k = 0; k < axes; ++k) {
            std::vector<f32> a;
            for (const V& x : vals) a.push_back(x.f[k]);
            put_f32s(o, a);
        }
        break;
    }
    case 0x10:
        for (const V& x : vals) {
            put8(o, static_cast<u32>(x.cf));
            if (x.cf == 0)
                for (int j = 0; j < 9; ++j) put_f32le(o, x.f[3 + j]);
        }
        for (int k = 0; k < 3; ++k) {
            std::vector<f32> a;
            for (const V& x : vals) a.push_back(x.f[k]);
            put_f32s(o, a);
        }
        break;
    case 0x07:
        for (int k = 0; k < 2; ++k) {
            std::vector<f32> a;
            for (const V& x : vals) a.push_back(x.f[k]);
            put_f32s(o, a);
        }
        for (int k = 0; k < 2; ++k) {
            std::vector<i32> a;
            for (const V& x : vals) a.push_back(x.iv[k]);
            put_i32s(o, a);
        }
        break;
    case 0x12: {
        std::vector<u32> w;
        for (const V& x : vals) w.push_back(x.e);
        put_be_interleaved(o, w);
        break;
    }
    case 0x13: {
        std::vector<i32> a;
        for (const V& x : vals) a.push_back(x.ref);
        put_refs(o, a);
        break;
    }
    case 0x1A:
        for (int ch = 0; ch < 3; ++ch)
            for (const V& x : vals) put8(o, x.c[ch]);
        break;
    default: break;
    }
}

// A binary .rbxl / .rbxm with the given objects (in file order) and properties.
Bytes build_binary(const std::vector<Obj>& objs, const std::vector<PropDef>& props, Comp comp) {
    std::vector<std::string> classes;
    for (const Obj& o : objs)
        if (std::find(classes.begin(), classes.end(), o.cls) == classes.end()) classes.push_back(o.cls);
    auto cid = [&](const std::string& c) {
        return static_cast<u32>(std::find(classes.begin(), classes.end(), c) - classes.begin()) + 1u;
    };
    auto instances_of = [&](const std::string& c) {
        std::vector<const Obj*> r;
        for (const Obj& o : objs)
            if (o.cls == c) r.push_back(&o);
        return r;
    };

    Bytes file = {'<', 'r', 'o', 'b', 'l', 'o', 'x', '!', 0x89, 0xff, 0x0d, 0x0a, 0x1a, 0x0a};
    put16le(file, 0);
    put32le(file, static_cast<u32>(classes.size()));
    put32le(file, static_cast<u32>(objs.size()));
    for (int k = 0; k < 8; ++k) put8(file, 0);

    u32 counter = 0;
    for (const std::string& c : classes) {
        const auto inst = instances_of(c);
        Bytes p;
        put32le(p, cid(c));
        putstr(p, c);
        const bool svc = inst[0]->service;
        put8(p, svc ? 1u : 0u);
        put32le(p, static_cast<u32>(inst.size()));
        std::vector<i32> refs;
        for (const Obj* o : inst) refs.push_back(o->ref);
        put_refs(p, refs);
        if (svc)
            for (usize k = 0; k < inst.size(); ++k) put8(p, 1);
        put_chunk(file, "INST", p, comp, counter);
    }
    for (const std::string& c : classes) {
        std::vector<V> names;
        for (const Obj* o : instances_of(c)) names.push_back(str(o->name));
        Bytes p;
        put32le(p, cid(c));
        putstr(p, "Name");
        put8(p, 0x01);
        encode_prop(p, 0x01, names);
        put_chunk(file, "PROP", p, comp, counter);
    }
    for (const PropDef& d : props) {
        std::vector<V> vals;
        for (const Obj* o : instances_of(d.cls)) {
            V v;
            for (const auto& [r, val] : d.vals)
                if (r == o->ref) v = val;
            vals.push_back(v);
        }
        Bytes p;
        put32le(p, cid(d.cls));
        putstr(p, d.name);
        put8(p, d.type);
        encode_prop(p, d.type, vals);
        put_chunk(file, "PROP", p, comp, counter);
    }
    Bytes prnt;
    put8(prnt, 0);
    put32le(prnt, static_cast<u32>(objs.size()));
    std::vector<i32> children, parents;
    for (const Obj& o : objs) {
        children.push_back(o.ref);
        parents.push_back(o.parent);
    }
    put_refs(prnt, children);
    put_refs(prnt, parents);
    put_chunk(file, "PRNT", prnt, comp, counter);
    const std::string end = "</roblox>";
    put_chunk(file, "END\0", Bytes(end.begin(), end.end()), Comp::None, counter);
    return file;
}

// The demo place: a Model with a PrimaryPart, a block, a cylinder, a wedge, GUI, a script, an unknown class with a
// child part, and a folder under ReplicatedStorage.
std::vector<Obj> demo_objs() {
    return {
        {"Workspace", "Workspace", 1, -1, true},
        {"Model", "Car", 2, 1},
        {"Part", "Block", 3, 2},
        {"Part", "Cyl", 4, 1},
        {"WedgePart", "Wedge", 5, 1},
        {"StarterGui", "StarterGui", 6, -1, true},
        {"ScreenGui", "HUD", 7, 6},
        {"Frame", "Panel", 8, 7},
        {"ServerScriptService", "ServerScriptService", 9, -1, true},
        {"Script", "Spinner", 10, 9},
        {"Lighting", "Lighting", 11, -1, true},
        {"BloomEffect", "Bloom", 12, 11},
        {"Part", "Inner", 13, 12},
        {"ReplicatedStorage", "ReplicatedStorage", 14, -1, true},
        {"Folder", "Stuff", 15, 14},
    };
}

std::vector<PropDef> demo_props() {
    return {
        {"Workspace", "Gravity", 0x04, {{1, fv(196.2f)}}},
        {"Model", "PrimaryPart", 0x13, {{2, ref(3)}}},
        {"Part", "shape", 0x12, {{3, en(1)}, {4, en(2)}}},
        {"Part", "Material", 0x12, {{3, en(256)}, {4, en(256)}, {13, en(256)}}},
        {"Part", "CFrame", 0x10, {{3, cframe(10, 5, -4)}, {4, cframe(0, 0, 0)}}},
        {"Part", "size", 0x0E, {{3, vec(4, 1, 2)}, {4, vec(4, 2, 2)}}},
        {"Part", "Color3uint8", 0x1A, {{3, rgb(255, 0, 0)}}},
        {"Part", "Anchored", 0x02, {{3, bl(true)}}},
        {"Part", "Transparency", 0x04, {{3, fv(0.25f)}}},
        {"WedgePart", "size", 0x0E, {{5, vec(4, 2, 2)}}},
        {"Frame", "Position", 0x07, {{8, ud2(0.5f, 0.25f, 10, -20)}}},
        {"Frame", "Size", 0x07, {{8, ud2(0, 0, 200, 50)}}},
        {"Frame", "Visible", 0x02, {{8, bl(true)}}},
        {"ScreenGui", "ResetOnSpawn", 0x02, {{7, bl(false)}}},
        {"Script", "Source", 0x01, {{10, str("print('hi')\n-- end")}}},
        {"Lighting", "ClockTime", 0x04, {{11, fv(8.0f)}}},
        {"BloomEffect", "Intensity", 0x04, {{12, fv(0.5f)}}},
    };
}

u32 enum_of(const char* enum_name, const char* item) {
    return static_cast<u32>(ClassRegistry::get().find_enum(enum_name)->index_of(item));
}

// Checks the conversion of the demo place (shared by the binary variants).
void check_demo(DataModel& dm, const ImportReport& rep) {
    Instance* block = dm.find_path("Workspace.Car.Block");
    REQUIRE(block != nullptr);
    CHECK(block->class_name() == "Part");
    const vec3 pos = block->get_vec3("Position");
    CHECK(pos.x == doctest::Approx(2.8f));
    CHECK(pos.y == doctest::Approx(1.4f));
    CHECK(pos.z == doctest::Approx(-1.12f));
    const vec3 size = block->get_vec3("Size");
    CHECK(size.x == doctest::Approx(4 * 0.28f));
    CHECK(size.y == doctest::Approx(1 * 0.28f));
    CHECK(size.z == doctest::Approx(2 * 0.28f));
    CHECK(block->get("Shape")->as_enum() == enum_of("PartType", "Block"));
    const vec3 color = block->get_vec3("Color");
    CHECK(color.x == doctest::Approx(1.0f));
    CHECK(color.y == doctest::Approx(0.0f));
    CHECK(block->get("Material")->as_enum() == enum_of("Material", "Plastic"));
    CHECK(block->get_float("Transparency") == doctest::Approx(0.25f));
    CHECK(block->get_bool("Anchored"));

    Instance* cyl = dm.find_path("Workspace.Cyl");
    REQUIRE(cyl != nullptr);
    CHECK(cyl->get("Shape")->as_enum() == enum_of("PartType", "Cylinder"));
    // Roblox cylinders run along X: the size swaps x and y, and the axis turns by Rz(-90 deg).
    const vec3 csize = cyl->get_vec3("Size");
    CHECK(csize.x == doctest::Approx(2 * 0.28f));
    CHECK(csize.y == doctest::Approx(4 * 0.28f));
    CHECK(csize.z == doctest::Approx(2 * 0.28f));
    CHECK(cyl->get_vec3("Orientation").z == doctest::Approx(-90.0f).epsilon(0.01));

    Instance* car = dm.find_path("Workspace.Car");
    REQUIRE(car != nullptr);
    CHECK(car->get("PrimaryPart")->ref == block->id());

    Instance* wedge = dm.find_path("Workspace.Wedge");
    REQUIRE(wedge != nullptr);
    CHECK(wedge->get("Shape")->as_enum() == enum_of("PartType", "Wedge"));

    Instance* panel = dm.find_path("StarterGui.HUD.Panel");
    REQUIRE(panel != nullptr);
    CHECK(panel->get_vec3("Position").x == doctest::Approx(0.5f));
    CHECK(panel->get_vec3("Position").y == doctest::Approx(0.25f));
    CHECK(panel->get_vec3("PositionOffset").x == doctest::Approx(10.0f));
    CHECK(panel->get_vec3("PositionOffset").y == doctest::Approx(-20.0f));
    CHECK(panel->get_vec3("SizeOffset").x == doctest::Approx(200.0f));
    CHECK(panel->get_vec3("SizeOffset").y == doctest::Approx(50.0f));
    CHECK(panel->get_bool("Visible"));

    Instance* spin = dm.find_path("ServerScriptService.Spinner");
    REQUIRE(spin != nullptr);
    CHECK(spin->get_string("LuauSource") == "print('hi')\n-- end");
    CHECK(spin->get_string("Source").find("public class Spinner_") != std::string::npos);
    CHECK(spin->get_string("Source").find(": TefraScript") != std::string::npos);
    CHECK_FALSE(spin->get_bool("Enabled", true));

    CHECK(dm.find_path("Lighting")->get_float("ClockTime") == doctest::Approx(8.0f));

    Instance* bloom = dm.find_path("Lighting.Bloom");
    REQUIRE(bloom != nullptr);
    CHECK(bloom->class_name() == "ImportedInstance");
    CHECK(bloom->get_string("OriginalClass") == "BloomEffect");
    CHECK(bloom->get_string("OriginalProperties").find("Intensity") != std::string::npos);
    Instance* inner = dm.find_path("Lighting.Bloom.Inner");
    REQUIRE(inner != nullptr);
    CHECK(inner->class_name() == "Part");

    Instance* stuff = dm.find_path("ReplicatedStorage.Stuff");
    REQUIRE(stuff != nullptr);
    CHECK(stuff->class_name() == "Folder");

    CHECK(rep.instances == 15);
    CHECK(rep.imported_as_unsupported == 1);
    CHECK(rep.unsupported_classes.at("BloomEffect") == 1);
    CHECK(rep.scripts == 1);
}

// ---- XML place ----------------------------------------------------------------------------------------------------

const char* kXmlPlace = R"XML(<?xml version="1.0" encoding="utf-8"?>
<roblox xmlns:xmime="http://www.w3.org/2005/05/xmlmime" version="4">
  <Meta name="ExplicitAutoJoints">true</Meta>
  <Item class="Workspace" referent="RBX0">
    <Properties>
      <string name="Name">Workspace</string>
      <float name="Gravity">196.2</float>
    </Properties>
    <Item class="Model" referent="RBX2">
      <Properties>
        <string name="Name">Car</string>
        <Ref name="PrimaryPart">RBX1</Ref>
      </Properties>
      <Item class="Part" referent="RBX1">
        <Properties>
          <string name="Name">Tilted</string>
          <CoordinateFrame name="CFrame">
            <X>10</X><Y>5</Y><Z>-4</Z>
            <R00>0</R00><R01>0</R01><R02>1</R02>
            <R10>0</R10><R11>1</R11><R12>0</R12>
            <R20>-1</R20><R21>0</R21><R22>0</R22>
          </CoordinateFrame>
          <Vector3 name="size"><X>4</X><Y>1</Y><Z>2</Z></Vector3>
          <token name="shape">1</token>
          <Color3uint8 name="Color3uint8">16711680</Color3uint8>
          <token name="Material">256</token>
          <bool name="Anchored">true</bool>
          <float name="Transparency">0.25</float>
        </Properties>
      </Item>
    </Item>
    <Item class="Part" referent="RBX16">
      <Properties>
        <string name="Name">Red</string>
        <int name="BrickColor">21</int>
      </Properties>
    </Item>
    <Item class="Terrain" referent="RBX13">
      <Properties><string name="Name">Terrain</string></Properties>
    </Item>
  </Item>
  <Item class="StarterGui" referent="RBX5">
    <Properties><string name="Name">StarterGui</string></Properties>
    <Item class="ScreenGui" referent="RBX6">
      <Properties>
        <string name="Name">HUD</string>
        <bool name="ResetOnSpawn">false</bool>
      </Properties>
      <Item class="TextLabel" referent="RBX7">
        <Properties>
          <string name="Name">Title</string>
          <UDim2 name="Position"><XS>0.5</XS><XO>10</XO><YS>0.25</YS><YO>-20</YO></UDim2>
          <UDim2 name="Size"><XS>0</XS><XO>200</XO><YS>0</YS><YO>50</YO></UDim2>
          <string name="Text">Hola &amp; adios</string>
          <Color3uint8 name="TextColor3">16777215</Color3uint8>
          <token name="TextXAlignment">1</token>
          <int name="TextSize">24</int>
          <bool name="Visible">true</bool>
        </Properties>
        <Item class="UICorner" referent="RBX8">
          <Properties><UDim name="CornerRadius"><S>0</S><O>8</O></UDim></Properties>
        </Item>
      </Item>
    </Item>
  </Item>
  <Item class="ServerScriptService" referent="RBX3">
    <Properties><string name="Name">ServerScriptService</string></Properties>
    <Item class="Script" referent="RBX4">
      <Properties>
        <string name="Name">Spin</string>
        <ProtectedString name="Source"><![CDATA[print("a & b") -- */ done]]></ProtectedString>
        <bool name="Disabled">false</bool>
      </Properties>
    </Item>
  </Item>
  <Item class="Lighting" referent="RBX9">
    <Properties>
      <string name="Name">Lighting</string>
      <float name="ClockTime">8</float>
    </Properties>
    <Item class="Clouds" referent="RBX10">
      <Properties><float name="Cover">0.7</float></Properties>
    </Item>
    <Item class="BloomEffect" referent="RBX11">
      <Properties><float name="Intensity">0.5</float></Properties>
      <Item class="Part" referent="RBX12">
        <Properties><string name="Name">Inner</string></Properties>
      </Item>
    </Item>
  </Item>
  <Item class="ReplicatedStorage" referent="RBX14">
    <Properties><string name="Name">ReplicatedStorage</string></Properties>
  </Item>
  <Item class="ServerStorage" referent="RBX17">
    <Properties><string name="Name">ServerStorage</string></Properties>
    <Item class="Folder" referent="RBX15">
      <Properties><string name="Name">Stuff</string></Properties>
    </Item>
  </Item>
</roblox>
)XML";

// Length of the document up to its closing tag (trailing whitespace excluded).
usize xml_core_length(const std::string& text) { return text.rfind('>') + 1; }

// ---- Helpers --------------------------------------------------------------------------------------------------------

} // namespace

TEST_CASE("rbx: LZ4 and zstd block decoders") {
    // Literal "abc", then a 9-byte match at offset 3: "abcabcabcabc".
    const u8 lz[] = {0x35, 'a', 'b', 'c', 0x03, 0x00};
    u8 out[12] = {};
    CHECK(lz4_decompress_block(lz, sizeof lz, out, 12));
    CHECK(std::string(reinterpret_cast<const char*>(out), 12) == "abcabcabcabc");
    CHECK_FALSE(lz4_decompress_block(lz, sizeof lz, out, 11)); // output size must match exactly

    const u8 bad_offset0[] = {0x10, 'a', 0x00, 0x00};
    u8 small[8] = {};
    CHECK_FALSE(lz4_decompress_block(bad_offset0, sizeof bad_offset0, small, 5));
    const u8 bad_offset_far[] = {0x10, 'a', 0x05, 0x00};
    CHECK_FALSE(lz4_decompress_block(bad_offset_far, sizeof bad_offset_far, small, 5));
    const u8 literal_only[] = {0x20, 'a', 'b'};
    CHECK(lz4_decompress_block(literal_only, sizeof literal_only, small, 2));
    CHECK_FALSE(lz4_decompress_block(literal_only, sizeof literal_only, small, 3));
    const u8 truncated_length[] = {0xF0, 255};
    CHECK_FALSE(lz4_decompress_block(truncated_length, sizeof truncated_length, small, 8));

    const Bytes frame = zstd_raw(Bytes{'x', 'y', 'z'});
    u8 zout[3] = {};
    CHECK(zstd_decompress_frame(frame.data(), frame.size(), zout, 3));
    CHECK(std::string(reinterpret_cast<const char*>(zout), 3) == "xyz");
    CHECK_FALSE(zstd_decompress_frame(frame.data(), frame.size(), zout, 2));
    Bytes broken = frame;
    broken[0] = 0x00;
    CHECK_FALSE(zstd_decompress_frame(broken.data(), broken.size(), zout, 3));
    CHECK_FALSE(zstd_decompress_frame(frame.data(), 6, zout, 3));
}

TEST_CASE("rbx: base64 and CFrame rotation ids") {
    CHECK(base64_encode(reinterpret_cast<const u8*>("Man"), 3) == "TWFu");
    CHECK(base64_encode(reinterpret_cast<const u8*>("Ma"), 2) == "TWE=");
    std::string out;
    REQUIRE(base64_decode("TWE=", out));
    CHECK(out == "Ma");
    REQUIRE(base64_decode("  TW\nFu ", out));
    CHECK(out == "Man");

    f32 r[9] = {};
    REQUIRE(cframe_rotation_from_id(0x02, r));
    CHECK(r[0] == doctest::Approx(1.0f));
    CHECK(r[4] == doctest::Approx(1.0f));
    CHECK(r[8] == doctest::Approx(1.0f));
    REQUIRE(cframe_rotation_from_id(0x03, r)); // (90, 0, 0): a quarter turn about X
    CHECK(r[5] == doctest::Approx(-1.0f));
    CHECK(r[7] == doctest::Approx(1.0f));
    CHECK_FALSE(cframe_rotation_from_id(0xEE, r));
}

TEST_CASE("rbx: binary place converts to the DataModel") {
    const Bytes file = build_binary(demo_objs(), demo_props(), Comp::None);
    DataModel dm;
    ImportReport rep;
    std::string err;
    REQUIRE(import_place_memory(file.data(), file.size(), dm, {}, &rep, &err));
    check_demo(dm, rep);
}

TEST_CASE("rbx: binary place with LZ4 and zstd chunks") {
    const Bytes file = build_binary(demo_objs(), demo_props(), Comp::Mixed);
    DataModel dm;
    ImportReport rep;
    std::string err;
    REQUIRE(import_place_memory(file.data(), file.size(), dm, {}, &rep, &err));
    check_demo(dm, rep);
}

TEST_CASE("rbx: XML place converts to the DataModel") {
    const std::string text = kXmlPlace;
    DataModel dm;
    ImportReport rep;
    std::string err;
    REQUIRE(import_place_memory(reinterpret_cast<const u8*>(text.data()), text.size(), dm, {}, &rep, &err));

    Instance* tilted = dm.find_path("Workspace.Car.Tilted");
    REQUIRE(tilted != nullptr);
    const vec3 pos = tilted->get_vec3("Position");
    CHECK(pos.x == doctest::Approx(2.8f));
    CHECK(pos.y == doctest::Approx(1.4f));
    CHECK(pos.z == doctest::Approx(-1.12f));
    // R02 = 1 and R20 = -1: a quarter turn about Y (Roblox CFrame.Angles(0, 90 deg, 0)).
    CHECK(tilted->get_vec3("Orientation").y == doctest::Approx(90.0f).epsilon(0.01));
    CHECK(tilted->get_vec3("Color").x == doctest::Approx(1.0f)); // 0xFF0000 -> red
    CHECK(tilted->get_vec3("Color").y == doctest::Approx(0.0f));
    CHECK(dm.find_path("Workspace.Car")->get("PrimaryPart")->ref == tilted->id());

    Instance* red = dm.find_path("Workspace.Red");
    REQUIRE(red != nullptr);
    CHECK(red->get_vec3("Color").x == doctest::Approx(196.0f / 255.0f));
    CHECK(red->get_vec3("Color").y == doctest::Approx(40.0f / 255.0f));

    Instance* title = dm.find_path("StarterGui.HUD.Title");
    REQUIRE(title != nullptr);
    CHECK(title->class_name() == "TextLabel");
    CHECK(title->get_vec3("Position").x == doctest::Approx(0.5f));
    CHECK(title->get_vec3("PositionOffset").y == doctest::Approx(-20.0f));
    CHECK(title->get_string("Text") == "Hola & adios");
    CHECK(title->get("TextXAlignment")->as_enum() == enum_of("TextXAlignment", "Right"));
    CHECK(title->get_float("TextSize") == doctest::Approx(24.0f));
    CHECK(title->get_float("CornerRadius") == doctest::Approx(8.0f));

    Instance* spin = dm.find_path("ServerScriptService.Spin");
    REQUIRE(spin != nullptr);
    CHECK(spin->get_string("LuauSource") == R"(print("a & b") -- */ done)");
    CHECK_FALSE(spin->get_bool("Enabled", true));

    CHECK(dm.find_path("Lighting")->get_float("ClockTime") == doctest::Approx(8.0f));
    Instance* bloom = dm.find_path("Lighting.BloomEffect");
    REQUIRE(bloom != nullptr);
    CHECK(bloom->class_name() == "ImportedInstance");
    CHECK(dm.find_path("Lighting.BloomEffect.Inner") != nullptr);
    CHECK(dm.find_path("Lighting.Clouds") == nullptr); // consumed into CloudCover
    CHECK(dm.find_path("Lighting")->get_float("CloudCover") == doctest::Approx(0.7f));

    CHECK(dm.find_path("ReplicatedStorage.ServerStorage.Stuff") != nullptr);
    CHECK(dm.find_path("Workspace.Terrain")->class_name() == "ImportedInstance");

    CHECK(rep.unsupported_classes.count("Terrain") == 1);
    CHECK(rep.unsupported_classes.count("BloomEffect") == 1);
    CHECK(rep.scripts == 1);
    CHECK(std::any_of(rep.warnings.begin(), rep.warnings.end(),
                      [](const std::string& w) { return w.find("Terrain") != std::string::npos; }));
}

TEST_CASE("rbx: XML document layer: meta, shared strings, CDATA, entities") {
    const std::string text = R"(<roblox version="4"><Meta name="K">v&lt;1&gt;</Meta>
<SharedStrings><SharedString md5="abc">aGVsbG8=</SharedString></SharedStrings>
<Item class="Folder" referent="A"><Properties><string name="Name">F &amp; G &#65;&#x42;</string>
<SharedString name="Sh">abc</SharedString>
<ProtectedString name="Code"><![CDATA[a < b && c]]></ProtectedString>
<BinaryString name="Blob">AQID</BinaryString>
<Ref name="Self">A</Ref>
</Properties></Item></roblox>)";
    Document doc;
    std::string err;
    REQUIRE(parse_xml(reinterpret_cast<const u8*>(text.data()), text.size(), doc, &err));
    REQUIRE(doc.meta.size() == 1);
    CHECK(doc.meta[0].first == "K");
    CHECK(doc.meta[0].second == "v<1>");
    REQUIRE(doc.shared_strings.size() == 1);
    CHECK(doc.shared_strings[0] == "hello");
    REQUIRE(doc.instances.size() == 1);
    const InstanceData& f = doc.instances[0];
    CHECK(f.name() == "F & G AB");
    const Value* sh = f.find("Sh");
    REQUIRE(sh != nullptr);
    CHECK(sh->type == Type::SharedString);
    CHECK(sh->i == 0); // the md5 "abc" names the first SharedStrings entry
    CHECK(f.find("Code")->s == "a < b && c");
    CHECK(f.find("Blob")->s == std::string("\x01\x02\x03", 3));
    CHECK(f.find("Self")->i == 0);
    CHECK(doc.roots.size() == 1);
}

TEST_CASE("rbx: truncated input fails and leaves the model untouched") {
    DataModel dm;
    const Bytes good = build_binary(demo_objs(), demo_props(), Comp::Mixed);
    REQUIRE(import_place_memory(good.data(), good.size(), dm, {}));
    const std::string spinner_before = dm.find_path("ServerScriptService.Spinner")->get_string("LuauSource");
    for (usize n = 0; n < good.size(); ++n) {
        std::string err;
        CHECK_FALSE(import_place_memory(good.data(), n, dm, {}, nullptr, &err));
    }
    CHECK(dm.find_path("ServerScriptService.Spinner")->get_string("LuauSource") == spinner_before);

    const std::string text = kXmlPlace;
    const usize core = xml_core_length(text);
    for (usize n = 0; n < core; ++n) {
        std::string err;
        CHECK_FALSE(import_place_memory(reinterpret_cast<const u8*>(text.data()), n, dm, {}, nullptr, &err));
    }
    CHECK(dm.find_path("ServerScriptService.Spinner")->get_string("LuauSource") == spinner_before);
}

TEST_CASE("rbx: corrupted input never crashes") {
    const Bytes good = build_binary(demo_objs(), demo_props(), Comp::None);
    const std::string xml = kXmlPlace;
    std::mt19937 rng(12345);
    for (int iter = 0; iter < 1500; ++iter) {
        DataModel dm;
        Bytes b = good;
        const int edits = 1 + static_cast<int>(rng() % 4);
        for (int e = 0; e < edits && !b.empty(); ++e) b[rng() % b.size()] = static_cast<u8>(rng());
        if (iter % 5 == 0) b.resize(rng() % (b.size() + 1));
        ImportReport rep;
        (void)import_place_memory(b.data(), b.size(), dm, {}, &rep);
        Bytes garbage(rng() % 200);
        for (u8& g : garbage) g = static_cast<u8>(rng());
        (void)import_place_memory(garbage.data(), garbage.size(), dm, {});
        std::string x = xml;
        for (int e = 0; e < edits; ++e) x[rng() % x.size()] = static_cast<char>(rng() % 96 + 32);
        if (iter % 7 == 0) x.resize(rng() % (x.size() + 1));
        (void)import_place_memory(reinterpret_cast<const u8*>(x.data()), x.size(), dm, {});
    }
    CHECK(true);
}

TEST_CASE("rbx: model import under a parent") {
    const Bytes file = build_binary({{"Model", "Tree", 1, -1}, {"Part", "Trunk", 2, 1}}, {}, Comp::None);
    DataModel dm;
    Instance* folder = dm.create("Folder", dm.workspace(), "Holder");
    REQUIRE(folder != nullptr);
    ImportReport rep;
    std::vector<Instance*> roots;
    std::string err;
    REQUIRE(import_model_memory(file.data(), file.size(), folder, {}, &rep, &err, &roots));
    REQUIRE(roots.size() == 1);
    CHECK(roots[0]->name() == "Tree");
    CHECK(roots[0]->parent() == folder);
    CHECK(dm.find_path("Workspace.Holder.Tree.Trunk") != nullptr);
    CHECK(rep.converted == 2);

    std::vector<Instance*> none;
    CHECK_FALSE(import_model_memory(file.data(), 0, folder, {}, nullptr, &err, &none));
    CHECK_FALSE(import_model_memory(file.data(), file.size(), nullptr, {}, nullptr, &err, &none));
}

TEST_CASE("rbx: options scale lengths and enable scripts") {
    const Bytes file = build_binary(demo_objs(), demo_props(), Comp::None);
    DataModel dm;
    ImportOptions opt;
    opt.stud_to_meters = 1.0f;
    opt.convert_scripts_disabled = false;
    REQUIRE(import_place_memory(file.data(), file.size(), dm, opt));
    CHECK(dm.find_path("Workspace.Car.Block")->get_vec3("Position").x == doctest::Approx(10.0f));
    CHECK(dm.find_path("ServerScriptService.Spinner")->get_bool("Enabled", false));
}

TEST_CASE("rbx: report JSON and summary") {
    const Bytes file = build_binary(demo_objs(), demo_props(), Comp::None);
    DataModel dm;
    ImportReport rep;
    REQUIRE(import_place_memory(file.data(), file.size(), dm, {}, &rep));
    const std::string json = report_to_json(rep).dump();
    CHECK(json.find("\"imported_as_unsupported\":1") != std::string::npos);
    CHECK(json.find("\"BloomEffect\":1") != std::string::npos);
    CHECK(report_summary(rep).find("15 instances") != std::string::npos);
}

TEST_CASE("rbx: missing or empty files fail cleanly") {
    DataModel dm;
    std::string err;
    CHECK_FALSE(import_place("does/not/exist.rbxl", dm, {}, nullptr, &err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(import_place_memory(nullptr, 0, dm, {}, nullptr, &err));
    const std::string not_roblox = "hello world";
    CHECK_FALSE(
        import_place_memory(reinterpret_cast<const u8*>(not_roblox.data()), not_roblox.size(), dm, {}, nullptr, &err));
}
