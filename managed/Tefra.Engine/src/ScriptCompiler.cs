// In-memory compilation of user scripts with Roslyn. All scripts go into one assembly; a script with errors is
// disabled and the rest are rebuilt without it. Diagnostics are printed as "Workspace.Spinner#0(3,5): error CS...".
using System;
using System.Collections.Generic;
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
    // Collectible context: unloading it on every Play lets scripts be edited and reloaded without restarting Studio.
    internal sealed class ScriptLoadContext : AssemblyLoadContext
    {
        public ScriptLoadContext() : base("TefraScripts", isCollectible: true)
        {
        }

        // Tefra.Engine is already loaded by the native host, in whatever context it chose: share that instance.
        // The framework still resolves through the trusted platform list.
        protected override Assembly Load(AssemblyName assemblyName)
        {
            Assembly self = typeof(TefraScript).Assembly;
            if (assemblyName.Name == self.GetName().Name) return self;
            return null;
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

        // Compiles the slots that have not failed. On success returns true, sets context (null if there is nothing
        // to load) and fills slot.Type. Returns false only on a fatal error (for example, the usings failed).
        public static bool Compile(List<ScriptSlot> slots, out ScriptLoadContext context)
        {
            context = null;
            var parse = CSharpParseOptions.Default.WithLanguageVersion(LanguageVersion.Latest);
            var options = new CSharpCompilationOptions(
                OutputKind.DynamicallyLinkedLibrary,
                optimizationLevel: OptimizationLevel.Release,
                allowUnsafe: false,
                nullableContextOptions: NullableContextOptions.Disable);

            var active = slots.Where(s => !s.Failed).ToList();
            while (active.Count > 0)
            {
                var trees = new List<SyntaxTree> { CSharpSyntaxTree.ParseText(UsingsText, parse, UsingsPath, Encoding.UTF8) };
                foreach (ScriptSlot s in active)
                    trees.Add(CSharpSyntaxTree.ParseText(s.Source, parse, s.Path, Encoding.UTF8));

                string name = "TefraScripts" + System.Threading.Interlocked.Increment(ref _counter);
                CSharpCompilation compilation = CSharpCompilation.Create(name, trees, References(), options);

                // Find the TefraScript-derived class of each script (semantic analysis, before emitting).
                var bad = new List<ScriptSlot>();
                INamedTypeSymbol baseSymbol = compilation.GetTypeByMetadataName("Tefra.TefraScript");
                for (int i = 0; i < active.Count; i++)
                {
                    ScriptSlot s = active[i];
                    string typeName = FindScriptClass(compilation, trees[i + 1], baseSymbol);
                    if (typeName == null)
                    {
                        Report(s.DisplayName + ": error TEFRA01: the script must declare a public class that derives from TefraScript.");
                        s.Failed = true;
                        bad.Add(s);
                    }
                    else
                    {
                        s.TypeName = typeName;
                    }
                }
                if (bad.Count > 0)
                {
                    active.RemoveAll(bad.Contains);
                    continue;
                }

                using (var il = new MemoryStream())
                using (var pdb = new MemoryStream())
                {
                    EmitResult emit = compilation.Emit(il, pdb, options: new EmitOptions(debugInformationFormat: DebugInformationFormat.PortablePdb));
                    if (emit.Success)
                    {
                        il.Seek(0, SeekOrigin.Begin);
                        pdb.Seek(0, SeekOrigin.Begin);
                        var ctx = new ScriptLoadContext();
                        Assembly assembly = ctx.LoadFromStream(il, pdb);
                        foreach (ScriptSlot s in active)
                            s.Type = assembly.GetType(s.TypeName, throwOnError: false);
                        context = ctx;
                        return true;
                    }

                    bad.Clear();
                    foreach (Diagnostic d in emit.Diagnostics.Where(x => x.Severity == DiagnosticSeverity.Error))
                    {
                        string path = d.Location.SourceTree?.FilePath;
                        ScriptSlot owner = path == null ? null : active.FirstOrDefault(s => s.Path == path);
                        if (owner == null)
                        {
                            // Diagnostics outside any script (the usings themselves) cannot be recovered from.
                            Report("TefraScripts: " + d.ToString());
                            return false;
                        }
                        Report(owner.DisplayName + Location(d) + ": error " + d.Id + ": " + d.GetMessage());
                        owner.Failed = true;
                        if (!bad.Contains(owner)) bad.Add(owner);
                    }
                    if (bad.Count == 0) return false;
                    active.RemoveAll(bad.Contains);
                }
            }
            return true;
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
            var span = d.Location.GetLineSpan().StartLinePosition;
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
