using System.Linq.Expressions;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;

namespace Devex;

/// <summary>
/// Loads the assembly of a game, finds its components, and runs them. The engine owns the values of
/// the components; this runtime copies them into the C# objects before a method runs and back after,
/// so that the inspector, the scene files and undo see the same data as the game.
/// </summary>
internal static unsafe class GameRuntime
{
    private sealed class GameLoadContext(string path) : AssemblyLoadContext(isCollectible: true)
    {
        private readonly AssemblyDependencyResolver _resolver = new(path);

        protected override Assembly? Load(AssemblyName name)
        {
            // The engine's own assembly is shared, so that a game's Component is the engine's Component.
            if (name.Name!.Equals("Devex.Managed", StringComparison.Ordinal))
            {
                return typeof(Component).Assembly;
            }
            string? path = _resolver.ResolveAssemblyToPath(name);
            return path != null ? LoadFromAssemblyPath(path) : null;
        }
    }

    private sealed class FieldBinding
    {
        public required Action<Component, nint> Load;
        public required Action<Component, nint> Store;
    }

    private sealed class ComponentTypeInfo(Type type, string name, FieldInfo[] fields)
    {
        public Type Type { get; } = type;
        public string Name { get; } = name;
        public FieldInfo[] Fields { get; } = fields;
        public nuint TypeIndex { get; set; }
        public bool Bound { get; set; }
        public FieldBinding[] Bindings { get; set; } = [];
        public Dictionary<ulong, Component> Instances { get; } = [];
        public HashSet<ulong> Seen { get; } = [];
    }

    private sealed class SystemInfo(SystemPhase phase, int order, int found, string name, Action<Scene> run)
    {
        public SystemPhase Phase { get; } = phase;
        public int Order { get; } = order;
        // Where it was found, so that systems of the same order keep that order.
        public int Found { get; } = found;
        public string Name { get; } = name;
        public Action<Scene> Run { get; } = run;
    }

    private static GameLoadContext? _context;
    private static readonly List<SystemInfo> Systems = [];
    private static readonly List<ComponentTypeInfo> Types = [];
    private static readonly Dictionary<Type, ComponentTypeInfo> TypesByType = [];
    private static readonly Scene SceneView = new(null);

    /// <summary>Loads the game assembly and describes its components for the engine.</summary>
    public static string Load(string assemblyPath)
    {
        Unload();
        _context = new GameLoadContext(assemblyPath);
        // Loaded from memory, so that the editor can build the game again while it runs.
        using var code = new MemoryStream(File.ReadAllBytes(assemblyPath));
        string symbols = Path.ChangeExtension(assemblyPath, ".pdb");
        using MemoryStream? debugInfo = File.Exists(symbols) ? new MemoryStream(File.ReadAllBytes(symbols)) : null;
        Assembly assembly = _context.LoadFromStream(code, debugInfo);
        foreach (Type type in assembly.GetTypes())
        {
            AddSystems(type);
            if (type.IsAbstract || !type.IsSubclassOf(typeof(Component)))
            {
                continue;
            }
            if (type.GetConstructor(Type.EmptyTypes) == null)
            {
                Log.Warning($"{type.Name} is skipped: a component needs a constructor without arguments");
                continue;
            }
            FieldInfo[] fields = [.. type.GetFields(BindingFlags.Public | BindingFlags.Instance)
                .Where(field => !field.IsInitOnly && field.GetCustomAttribute<HiddenAttribute>() == null &&
                                KindOf(field.FieldType) != null)];
            var info = new ComponentTypeInfo(type, type.Name, fields);
            if (TypesByType.ContainsKey(type) || Types.Any(other => other.Name == info.Name))
            {
                Log.Warning($"Two components are named {info.Name}: only the first one is used");
                continue;
            }
            Types.Add(info);
            TypesByType.Add(type, info);
        }
        Systems.Sort((first, second) => first.Order != second.Order ? first.Order.CompareTo(second.Order)
                                                                   : first.Found.CompareTo(second.Found));
        return Describe();
    }

    /// <summary>Forgets the game assembly and everything it defined.</summary>
    public static void Unload()
    {
        Systems.Clear();
        Types.Clear();
        TypesByType.Clear();
        if (_context != null)
        {
            _context.Unload();
            _context = null;
            // The collectible context goes away once nothing refers to its types.
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }
    }

    /// <summary>The engine gives each component type its index and where its fields are in memory.</summary>
    public static void SetTypeLayout(string typeName, nuint typeIndex, int[] offsets)
    {
        ComponentTypeInfo? info = Types.FirstOrDefault(type => type.Name == typeName);
        if (info == null || offsets.Length != info.Fields.Length)
        {
            return;
        }
        info.TypeIndex = typeIndex;
        info.Bindings = new FieldBinding[info.Fields.Length];
        for (int index = 0; index < info.Fields.Length; ++index)
        {
            info.Bindings[index] = BindField(info.Type, info.Fields[index], offsets[index]);
        }
        info.Bound = true;
    }

    /// <summary>Runs the behaviours of every component of the scene for this phase.</summary>
    public static void RunPhase(void* scene, SystemPhase phase, float delta)
    {
        SceneView.Bind(scene);
        Time.Delta = delta;
        foreach (ComponentTypeInfo info in Types)
        {
            if (!info.Bound)
            {
                continue;
            }
            Entity* entities;
            int count = Bootstrap.Native.ComponentEntities(scene, info.TypeIndex, &entities);
            info.Seen.Clear();
            for (int index = 0; index < count; ++index)
            {
                Entity entity = entities[index];
                info.Seen.Add(entity.Key);
                void* component = Bootstrap.Native.FindComponent(scene, info.TypeIndex, entity);
                if (component == null)
                {
                    continue;
                }
                bool created = !info.Instances.TryGetValue(entity.Key, out Component? instance);
                if (created)
                {
                    instance = (Component)Activator.CreateInstance(info.Type)!;
                    instance.Entity = entity;
                    instance.Scene = SceneView;
                    info.Instances[entity.Key] = instance;
                }
                Run(info, instance!, component, phase, delta, created);
            }
            // Components removed since the previous frame lose their instance.
            if (info.Instances.Count != info.Seen.Count)
            {
                foreach (ulong key in info.Instances.Keys.Where(key => !info.Seen.Contains(key)).ToArray())
                {
                    info.Instances.Remove(key);
                }
            }
        }
        // Then the systems of the phase, which see the scene the components have just changed.
        foreach (SystemInfo system in Systems)
        {
            if (system.Phase != phase)
            {
                continue;
            }
            try
            {
                system.Run(SceneView);
            }
            catch (Exception exception)
            {
                Log.Error($"The system {system.Name} failed: {exception}");
            }
        }
    }

    /// <summary>Finds the static methods of a type marked [GameSystem].</summary>
    private static void AddSystems(Type type)
    {
        foreach (MethodInfo method in type.GetMethods(BindingFlags.Public | BindingFlags.NonPublic |
                                                      BindingFlags.Static | BindingFlags.DeclaredOnly))
        {
            if (method.GetCustomAttribute<GameSystemAttribute>() is not { } attribute)
            {
                continue;
            }
            string name = $"{type.Name}.{method.Name}";
            ParameterInfo[] parameters = method.GetParameters();
            bool takesScene = parameters.Length == 1 && parameters[0].ParameterType == typeof(Scene);
            if (method.ReturnType != typeof(void) || method.IsGenericMethod ||
                (parameters.Length != 0 && !takesScene))
            {
                Log.Warning($"{name} is skipped: a system is a static void method taking the scene, or nothing");
                continue;
            }
            Action<Scene> run;
            if (takesScene)
            {
                run = method.CreateDelegate<Action<Scene>>();
            }
            else
            {
                Action alone = method.CreateDelegate<Action>();
                run = _ => alone();
            }
            Systems.Add(new SystemInfo(attribute.Phase, attribute.Order, Systems.Count, name, run));
        }
    }

    /// <summary>Writes the values a fresh C# object has into a component the engine has just made.</summary>
    public static void ApplyDefaults(string typeName, void* component)
    {
        ComponentTypeInfo? info = Types.FirstOrDefault(type => type.Name == typeName);
        if (info == null || !info.Bound)
        {
            return;
        }
        try
        {
            Store(info, (Component)Activator.CreateInstance(info.Type)!, component);
        }
        catch (Exception exception)
        {
            Log.Error($"The default values of {typeName} cannot be read: {exception}");
        }
    }

    public static Component? FindInstance(Type type, Entity entity)
    {
        return TypesByType.TryGetValue(type, out ComponentTypeInfo? info) &&
               info.Instances.TryGetValue(entity.Key, out Component? instance)
            ? instance
            : null;
    }

    public static IEnumerable<T> Instances<T>() where T : Component
    {
        return TypesByType.TryGetValue(typeof(T), out ComponentTypeInfo? info)
            ? info.Instances.Values.OfType<T>()
            : [];
    }

    public static Component AddComponent(Scene sceneView, void* scene, Type type, Entity entity)
    {
        if (!TypesByType.TryGetValue(type, out ComponentTypeInfo? info) || !info.Bound)
        {
            throw new InvalidOperationException($"{type.Name} is not a component of this game");
        }
        void* component = Bootstrap.Native.AddComponent(scene, info.TypeIndex, entity);
        if (component == null)
        {
            throw new InvalidOperationException($"cannot add {type.Name} to the entity");
        }
        var instance = (Component)Activator.CreateInstance(type)!;
        instance.Entity = entity;
        instance.Scene = sceneView;
        info.Instances[entity.Key] = instance;
        // The engine's defaults become the values of the new instance.
        Load(info, instance, component);
        return instance;
    }

    public static void RemoveComponent(void* scene, Type type, Entity entity)
    {
        if (!TypesByType.TryGetValue(type, out ComponentTypeInfo? info) || !info.Bound)
        {
            return;
        }
        Bootstrap.Native.RemoveComponent(scene, info.TypeIndex, entity);
        info.Instances.Remove(entity.Key);
    }

    private static void Run(ComponentTypeInfo info, Component instance, void* component, SystemPhase phase, float delta,
                            bool created)
    {
        Load(info, instance, component);
        try
        {
            if (created || phase == SystemPhase.Start)
            {
                instance.Start();
            }
            switch (phase)
            {
                case SystemPhase.Update:
                    instance.Update(delta);
                    break;
                case SystemPhase.FixedUpdate:
                    instance.FixedUpdate(delta);
                    break;
                case SystemPhase.Start:
                    break;
            }
        }
        catch (Exception exception)
        {
            Log.Error($"{info.Name} of '{instance.Scene.Name(instance.Entity)}' failed: {exception}");
        }
        Store(info, instance, component);
    }

    private static void Load(ComponentTypeInfo info, Component instance, void* component)
    {
        foreach (FieldBinding binding in info.Bindings)
        {
            binding.Load(instance, (nint)component);
        }
    }

    private static void Store(ComponentTypeInfo info, Component instance, void* component)
    {
        foreach (FieldBinding binding in info.Bindings)
        {
            binding.Store(instance, (nint)component);
        }
    }

    // Reading and writing a field of the engine's memory, compiled once per field.
    private static FieldBinding BindField(Type type, FieldInfo field, int offset)
    {
        ParameterExpression instance = Expression.Parameter(typeof(Component), "instance");
        ParameterExpression memory = Expression.Parameter(typeof(nint), "memory");
        MemberExpression member = Expression.Field(Expression.Convert(instance, type), field);
        ConstantExpression at = Expression.Constant(offset);

        Expression read;
        Expression write;
        if (field.FieldType == typeof(string))
        {
            read = Expression.Call(ReadStringMethod, memory, at);
            write = Expression.Call(WriteStringMethod, memory, at,
                                    Expression.Coalesce(member, Expression.Constant(string.Empty)));
        }
        else if (field.FieldType.IsEnum)
        {
            read = Expression.Convert(Expression.Call(ReadMethod.MakeGenericMethod(typeof(int)), memory, at), field.FieldType);
            write = Expression.Call(WriteMethod.MakeGenericMethod(typeof(int)), memory, at,
                                    Expression.Convert(member, typeof(int)));
        }
        else
        {
            read = Expression.Call(ReadMethod.MakeGenericMethod(field.FieldType), memory, at);
            write = Expression.Call(WriteMethod.MakeGenericMethod(field.FieldType), memory, at, member);
        }
        return new FieldBinding
        {
            Load = Expression.Lambda<Action<Component, nint>>(Expression.Assign(member, read), instance, memory).Compile(),
            Store = Expression.Lambda<Action<Component, nint>>(write, instance, memory).Compile(),
        };
    }

    private static readonly MethodInfo ReadMethod =
        typeof(GameRuntime).GetMethod(nameof(ReadValue), BindingFlags.NonPublic | BindingFlags.Static)!;

    private static readonly MethodInfo WriteMethod =
        typeof(GameRuntime).GetMethod(nameof(WriteValue), BindingFlags.NonPublic | BindingFlags.Static)!;

    private static readonly MethodInfo ReadStringMethod =
        typeof(GameRuntime).GetMethod(nameof(ReadString), BindingFlags.NonPublic | BindingFlags.Static)!;

    private static readonly MethodInfo WriteStringMethod =
        typeof(GameRuntime).GetMethod(nameof(WriteString), BindingFlags.NonPublic | BindingFlags.Static)!;

    private static T ReadValue<T>(nint memory, int offset) where T : unmanaged
        => Unsafe.ReadUnaligned<T>((void*)(memory + offset));

    private static void WriteValue<T>(nint memory, int offset, T value) where T : unmanaged
        => Unsafe.WriteUnaligned((void*)(memory + offset), value);

    private static string ReadString(nint memory, int offset)
        => Utf8.ToString(Bootstrap.Native.ReadStringField((void*)memory, (nuint)offset)) ?? string.Empty;

    private static void WriteString(nint memory, int offset, string value)
    {
        using var text = new Utf8Buffer(value);
        Bootstrap.Native.WriteStringField((void*)memory, (nuint)offset, text.Pointer);
    }

    /// <summary>The kind the engine stores a field type as, or null when the type is not supported.</summary>
    private static string? KindOf(Type type)
    {
        if (type.IsEnum)
        {
            return Enum.GetUnderlyingType(type) == typeof(int) ? "enum" : null;
        }
        if (type == typeof(bool)) return "bool";
        if (type == typeof(int)) return "int";
        if (type == typeof(uint)) return "uint";
        if (type == typeof(float)) return "float";
        if (type == typeof(string)) return "string";
        if (type == typeof(Vec2)) return "vec2";
        if (type == typeof(Vec3)) return "vec3";
        if (type == typeof(Vec4)) return "vec4";
        if (type == typeof(Quat)) return "quat";
        if (type == typeof(AssetId)) return "asset";
        return null;
    }

    /// <summary>The components and their fields, in the text format the engine reads.</summary>
    private static string Describe()
    {
        var text = new StringBuilder();
        foreach (ComponentTypeInfo info in Types)
        {
            text.Append($"[component type=\"{info.Name}\"]\n\n");
            foreach (FieldInfo field in info.Fields)
            {
                text.Append($"[field name=\"{FileName(field.Name)}\" kind=\"{KindOf(field.FieldType)}\"");
                if (field.GetCustomAttribute<AngleAttribute>() != null)
                {
                    text.Append(" angle=true");
                }
                if (field.GetCustomAttribute<ColorAttribute>() != null)
                {
                    text.Append(" color=true");
                }
                if (field.GetCustomAttribute<PhysicsLayerAttribute>() != null)
                {
                    text.Append(" physics_layer=true");
                }
                if (field.GetCustomAttribute<AssetTypeAttribute>() is { } asset)
                {
                    text.Append($" asset_type=\"{asset.Type}\"");
                }
                if (field.FieldType.IsEnum)
                {
                    text.Append($" values=\"{string.Join(',', Enum.GetNames(field.FieldType).Select(FileName))}\"");
                }
                text.Append("]\n\n");
            }
        }
        return text.ToString();
    }

    /// <summary>WalkSpeed becomes walk_speed, as fields are named in scene files.</summary>
    internal static string FileName(string name)
    {
        var text = new StringBuilder(name.Length + 4);
        for (int index = 0; index < name.Length; ++index)
        {
            char character = name[index];
            if (char.IsUpper(character))
            {
                if (index > 0 && (!char.IsUpper(name[index - 1]) || (index + 1 < name.Length && !char.IsUpper(name[index + 1]))))
                {
                    text.Append('_');
                }
                text.Append(char.ToLowerInvariant(character));
            }
            else
            {
                text.Append(character);
            }
        }
        return text.ToString();
    }
}

/// <summary>Time of the frame being updated.</summary>
public static class Time
{
    /// <summary>Seconds since the previous frame, or the fixed step during FixedUpdate.</summary>
    public static float Delta { get; internal set; }
}
