// In-memory compilation of user scripts with Roslyn.
//
// Every distinct Source text is one compilation unit (a "script unit"): identical scripts (the Obby's many KillBrick
// Scripts, for example) share one compiled type, and each Script instance gets its own TefraScript object. The unit
// wraps its text in a namespace of its own (TefraS<n>), so two different scripts may both declare `class KillBrick`.
// The wrapper keeps the user's line numbers (`#line 1` after the opening brace), so diagnostics read
// "Workspace.Spinner(3,5): error CS...".
//
// ModuleScript sources are compiled as plain units in the global namespace. They are compiled together with the first
// batch of scripts, and every later batch references that assembly, so a module's public types and static members
// are visible to every script and their state is shared.
//
// A unit with errors is disabled and the batch is rebuilt without it. Each batch is one assembly, loaded into the
// session's collectible AssemblyLoadContext; the context is unloaded when Play stops.
using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Emit;

namespace Tefra
{
    // Collectible context of one Play session. Every batch assembly is loaded here, and a later batch resolves the
    // assemblies of earlier ones by name (the module library), so they all share one context.
    internal sealed class ScriptLoadContext : AssemblyLoadContext
    {
        private readonly Dictionary<string, Assembly> _loaded = new Dictionary<string, Assembly>();

        public ScriptLoadContext() : base("TefraScripts", isCollectible: true)
        {
        }

        public Assembly Add(MemoryStream il, MemoryStream pdb)
        {
            il.Seek(0, SeekOrigin.Begin);
            pdb.Seek(0, SeekOrigin.Begin);
            Assembly assembly = LoadFromStream(il, pdb);
            _loaded[assembly.GetName().Name] = assembly;
            return assembly;
        }

        // Tefra.Engine is already loaded by the native host, in whatever context it chose: share that instance.
        // The framework still resolves through the trusted platform list.
        protected override Assembly Load(AssemblyName assemblyName)
        {
            Assembly self = typeof(TefraScript).Assembly;
            if (assemblyName.Name == self.GetName().Name) return self;
            Assembly loaded;
            return _loaded.TryGetValue(assemblyName.Name, out loaded) ? loaded : null;
        }
    }

    // One compilation unit: a distinct script source (wrapped in its namespace) or a ModuleScript source.
    internal sealed class ScriptUnit
    {
        public string Source;          // the user's text, exactly as stored in the DataModel
        public string Path;            // syntax tree path, used to attribute diagnostics
        public string Namespace;       // wrapper namespace of a script unit; null for a module
        public bool IsModule;
        public string TypeName;        // metadata name of the TefraScript-derived class (script units)
        public Type Type;              // set after a successful compile
        public bool Failed;            // disabled by a compile error
        public string Label;           // names in diagnostics: "Workspace.A, Workspace.B" or "ModuleScript Workspace.M"
        public readonly List<ScriptSlot> Slots = new List<ScriptSlot>();

        public string Text
        {
            get
            {
                if (IsModule) return Source;
                // The wrapper opens on the same line as the namespace, and `#line 1` restarts the numbering so that
                // the first line of the user's text is line 1.
                return "namespace " + Namespace + "\n{\n#line 1\n" + Source + "\n}\n";
            }
        }
    }

    internal static class ScriptCompiler
    {
        // Implicit usings for scripts, so user code reads like a Roblox script.
        private const string UsingsPath = "<tefra-usings>";
        private const string UsingsText =
            "global using System;\n" +
            "global using System.Collections.Generic;\n" +
            "global using System.Linq;\n" +
            "global using System.Text;\n" +
            "global using Tefra;\n" +
            "global using static Tefra.Debug;\n";

        private static List<MetadataReference> _references;
        private static int _counter;

        // Compiles the units that have not failed into one assembly, loads it into `context` and sets the Type of every
        // script unit. `library` (may be null) is an earlier batch whose module types the units may use.
        // Returns false only on a fatal error (diagnostics outside every unit, for example the usings). Units that
        // fail are marked Failed and reported; their slots are disabled by the caller.
        public static bool Build(List<ScriptUnit> units, ScriptLoadContext context, MetadataReference library,
                                 out MetadataReference image)
        {
            image = null;
            var parse = CSharpParseOptions.Default.WithLanguageVersion(LanguageVersion.Latest);
            var options = new CSharpCompilationOptions(
                OutputKind.DynamicallyLinkedLibrary,
                optimizationLevel: OptimizationLevel.Release,
                allowUnsafe: false,
                nullableContextOptions: NullableContextOptions.Disable);

            while (true)
            {
                var active = units.Where(u => !u.Failed).ToList();
                if (active.Count == 0) return true;

                var trees = new List<SyntaxTree> { CSharpSyntaxTree.ParseText(UsingsText, parse, UsingsPath, Encoding.UTF8) };
                foreach (ScriptUnit u in active)
                    trees.Add(CSharpSyntaxTree.ParseText(u.Text, parse, u.Path, Encoding.UTF8));

                var refs = new List<MetadataReference>(References());
                if (library != null) refs.Add(library);
                string name = "TefraScripts" + System.Threading.Interlocked.Increment(ref _counter);
                CSharpCompilation compilation = CSharpCompilation.Create(name, trees, refs, options);

                // The TefraScript-derived class of each script unit (semantic analysis, before emitting).
                bool typeError = false;
                INamedTypeSymbol baseSymbol = compilation.GetTypeByMetadataName("Tefra.TefraScript");
                for (int i = 0; i < active.Count; i++)
                {
                    ScriptUnit u = active[i];
                    if (u.IsModule) continue;
                    string typeName = FindScriptClass(compilation, trees[i + 1], baseSymbol);
                    if (typeName == null)
                    {
                        Report(u.Label + ": error TEFRA01: the script must declare a public class that derives from TefraScript.");
                        u.Failed = true;
                        typeError = true;
                    }
                    else
                    {
                        u.TypeName = typeName;
                    }
                }
                if (typeError) continue;

                using (var il = new MemoryStream())
                using (var pdb = new MemoryStream())
                {
                    EmitResult emit = compilation.Emit(il, pdb, options: new EmitOptions(debugInformationFormat: DebugInformationFormat.PortablePdb));
                    if (emit.Success)
                    {
                        image = MetadataReference.CreateFromImage(ImmutableArray.Create(il.ToArray()));
                        Assembly assembly = context.Add(il, pdb);
                        foreach (ScriptUnit u in active)
                            if (!u.IsModule) u.Type = assembly.GetType(u.TypeName, throwOnError: false);
                        return true;
                    }

                    var bad = new List<ScriptUnit>();
                    foreach (Diagnostic d in emit.Diagnostics.Where(x => x.Severity == DiagnosticSeverity.Error))
                    {
                        string path = d.Location.SourceTree?.FilePath;
                        ScriptUnit owner = path == null ? null : active.FirstOrDefault(s => s.Path == path);
                        if (owner == null)
                        {
                            // Diagnostics outside any unit (the usings themselves) cannot be recovered from.
                            Report("TefraScripts: " + d.ToString());
                            return false;
                        }
                        Report(owner.Label + Location(d) + ": error " + d.Id + ": " + d.GetMessage());
                        owner.Failed = true;
                        if (!bad.Contains(owner)) bad.Add(owner);
                    }
                    if (bad.Count == 0) return false;
                }
            }
        }

        // Name of the first public, non-generic, top-level class in the tree that derives from TefraScript.
        private static string FindScriptClass(CSharpCompilation compilation, SyntaxTree tree, INamedTypeSymbol baseSymbol)
        {
            if (baseSymbol == null) return null;
            SemanticModel model = compilation.GetSemanticModel(tree);
            foreach (ClassDeclarationSyntax decl in tree.GetRoot().DescendantNodes().OfType<ClassDeclarationSyntax>())
            {
                var symbol = model.GetDeclaredSymbol(decl) as INamedTypeSymbol;
                if (symbol == null || symbol.ContainingType != null || symbol.IsAbstract || symbol.TypeParameters.Length > 0)
                    continue;
                if (symbol.DeclaredAccessibility != Accessibility.Public) continue;
                if (!Derives(symbol, baseSymbol)) continue;
                string ns = symbol.ContainingNamespace == null || symbol.ContainingNamespace.IsGlobalNamespace
                    ? ""
                    : symbol.ContainingNamespace.ToDisplayString();
                return ns.Length > 0 ? ns + "." + symbol.MetadataName : symbol.MetadataName;
            }
            return null;
        }

        private static bool Derives(INamedTypeSymbol type, INamedTypeSymbol baseSymbol)
        {
            for (INamedTypeSymbol b = type.BaseType; b != null; b = b.BaseType)
                if (SymbolEqualityComparer.Default.Equals(b, baseSymbol)) return true;
            return false;
        }

        private static string Location(Diagnostic d)
        {
            if (!d.Location.IsInSource) return "";
            var span = d.Location.GetMappedLineSpan().StartLinePosition; // mapped: honours the #line 1 of the wrapper
            return "(" + (span.Line + 1) + "," + (span.Character + 1) + ")";
        }

        private static void Report(string text)
        {
            Native.Log(Native.LogError, text);
        }

        // Metadata of every assembly in the host's trusted platform list (the framework and Tefra.Engine).
        private static List<MetadataReference> References()
        {
            if (_references != null) return _references;
            var refs = new List<MetadataReference>();
            var tpa = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string ?? "";
            foreach (string path in tpa.Split(Path.PathSeparator))
            {
                if (path.Length == 0 || !File.Exists(path)) continue;
                refs.Add(MetadataReference.CreateFromFile(path));
            }
            // Tefra.Engine itself is loaded by the native host (not part of the host's trusted list), so add it.
            string engine = typeof(TefraScript).Assembly.Location;
            if (!string.IsNullOrEmpty(engine) && !tpa.Contains(engine))
                refs.Add(MetadataReference.CreateFromFile(engine));
            _references = refs;
            return refs;
        }
    }
}
