using System.Reflection;
using System.Text;

namespace Devex;

/// <summary>
/// Adds a public static void method taking EditorContext to Editor > Addon Tools.
/// The addon id must match an installed addon with editor tools enabled in Project Settings.
/// Commands run on a copy of the edited scene and commit as one undoable change on success.
/// </summary>
[AttributeUsage(AttributeTargets.Method)]
public sealed class EditorCommandAttribute(string addonId, string name) : Attribute
{
    public string AddonId { get; } = addonId;
    public string Name { get; } = name;
}

/// <summary>The edited scene and selection, available only during an editor command.</summary>
public sealed class EditorContext
{
    private readonly Scene _scene;
    private readonly IReadOnlyList<Entity> _selection;
    private bool _active = true;

    internal EditorContext(Scene scene, Entity[] selection)
    {
        _scene = scene;
        _selection = Array.AsReadOnly(selection);
    }

    public Scene Scene { get { EnsureActive(); return _scene; } }
    public IReadOnlyList<Entity> Selection { get { EnsureActive(); return _selection; } }

    /// <summary>Creates a root entity with a default Transform, like the editor's Create Entity.</summary>
    public unsafe Entity CreateEntity(string name)
    {
        EnsureActive();
        using var type = new Utf8Buffer("Transform");
        long index = Bootstrap.Native.ComponentIndex(type.Pointer);
        Entity entity = _scene.CreateEntity(name);
        if (index < 0 || Bootstrap.Native.AddComponent(_scene.Pointer, (nuint)index, entity) == null)
            throw new InvalidOperationException("Cannot add the entity's Transform");
        return entity;
    }

    private void EnsureActive()
    {
        if (!_active)
            throw new InvalidOperationException("An EditorContext is valid only during its command");
    }

    internal void Close() => _active = false;
}

internal static unsafe partial class GameRuntime
{
    private sealed record EditorTool(string Id, string Addon, string Name, Action<EditorContext> Run);
    private static readonly Dictionary<string, EditorTool> EditorTools = [];

    private static string Quote(string value) => "\"" + value.Replace("\\", "\\\\")
        .Replace("\"", "\\\"").Replace("\n", "\\n").Replace("\r", "\\r").Replace("\t", "\\t") + "\"";

    private static void AddEditorCommands(Type type)
    {
        foreach (MethodInfo method in type.GetMethods(BindingFlags.Public | BindingFlags.Static | BindingFlags.DeclaredOnly))
        {
            var attribute = method.GetCustomAttribute<EditorCommandAttribute>();
            if (attribute == null)
                continue;
            if (string.IsNullOrWhiteSpace(attribute.AddonId) || string.IsNullOrWhiteSpace(attribute.Name) ||
                method.ContainsGenericParameters || method.ReturnType != typeof(void) ||
                method.GetParameters() is not [{ ParameterType: var parameter }] || parameter != typeof(EditorContext) ||
                method.GetCustomAttribute<System.Runtime.CompilerServices.AsyncStateMachineAttribute>() != null)
            {
                Log.Warning($"{type.FullName}.{method.Name} is skipped: an editor command must be a synchronous public static void method taking EditorContext");
                continue;
            }
            string id = $"{type.FullName}.{method.Name}";
            if (!EditorTools.TryAdd(id, new EditorTool(id, attribute.AddonId, attribute.Name, method.CreateDelegate<Action<EditorContext>>())))
                Log.Warning($"Duplicate editor command {id} is skipped");
        }
    }

    private static string DescribeEditorCommands()
    {
        var description = new StringBuilder();
        foreach (EditorTool tool in EditorTools.Values.OrderBy(tool => tool.Id, StringComparer.Ordinal))
        {
            description.Append("\n[editor_command id=").Append(Quote(tool.Id))
                .Append(" addon=").Append(Quote(tool.Addon)).Append(" name=").Append(Quote(tool.Name)).Append("]\n");
        }
        return description.ToString();
    }

    public static bool RunEditorCommand(void* scene, string id, Entity[] selection)
    {
        if (!EditorTools.TryGetValue(id, out EditorTool? tool))
            return false;
        SceneView.Bind(scene);
        var context = new EditorContext(SceneView, selection);
        _running = true;
        try
        {
            foreach (ComponentTypeInfo info in Types)
                info.Instances.Clear();
            LoadInstances();
            tool.Run(context);
            StoreInstances();
            return true;
        }
        finally
        {
            context.Close();
            foreach (ComponentTypeInfo info in Types)
            {
                info.Instances.Clear();
                info.Order.Clear();
            }
            _running = false;
            SceneView.Bind(null);
        }
    }
}
