using System.Linq.Expressions;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

namespace Devex;

/// <summary>
/// Loads the assembly of a game, finds its components and systems, and runs them. The engine owns the
/// values of the components' public fields, so that the inspector, scene files and undo see the same
/// data as the game: at the start of each phase the runtime copies them into the C# objects, which
/// the game's code then changes freely, and copies them back at the end of the phase.
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
        // The entities of the instances, in the order of the engine, for this phase.
        public List<Entity> Order { get; } = [];
        public bool HandlesCollisions { get; init; }
        public bool HandlesTriggers { get; init; }
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

    // The fields of a component that its public fields do not cover, kept across a reload of the code.
    private sealed class PreservedState(bool started, Dictionary<string, object?> fields)
    {
        public bool Started { get; } = started;
        public Dictionary<string, object?> Fields { get; } = fields;
    }

    private static GameLoadContext? _context;
    private static string? _loadedCopy;
    private static int _loads;
    private static readonly List<SystemInfo> Systems = [];
    private static readonly List<ComponentTypeInfo> Types = [];
    private static readonly Dictionary<Type, ComponentTypeInfo> TypesByType = [];
    private static readonly Dictionary<(string Type, ulong Entity), PreservedState> Preserved = [];
    private static readonly Dictionary<string, int> Failures = [];
    private static readonly Scene SceneView = new(null);
    private static bool _running;

    /// <summary>The scene that plays; its pointer is null outside the phases.</summary>
    public static Scene CurrentScene => SceneView;

    /// <summary>Loads the game assembly and describes its components for the engine.</summary>
    public static string Load(string assemblyPath)
    {
        Unload();
        _context = new GameLoadContext(assemblyPath);
        // Loaded from a copy, so that the editor can build the game again while it runs, with its
        // symbols next to it for debuggers and for the lines of exceptions.
        string copy = CopyForLoading(assemblyPath);
        Assembly assembly = _context.LoadFromAssemblyPath(copy);
        _loadedCopy = Path.GetDirectoryName(copy);
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
            var fields = new List<FieldInfo>();
            foreach (FieldInfo field in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
            {
                if (field.IsInitOnly || field.GetCustomAttribute<HiddenAttribute>() != null)
                {
                    continue;
                }
                if (Describe(field.FieldType).Kind == null)
                {
                    Log.Warning($"{type.Name}.{field.Name} is not saved: its type {field.FieldType.Name} is not supported");
                    continue;
                }
                fields.Add(field);
            }
            var info = new ComponentTypeInfo(type, type.Name, [.. fields])
            {
                HandlesCollisions = Overrides(type, nameof(Component.OnCollisionEnter)) ||
                                    Overrides(type, nameof(Component.OnCollisionExit)),
                HandlesTriggers = Overrides(type, nameof(Component.OnTriggerEnter)) ||
                                  Overrides(type, nameof(Component.OnTriggerExit)),
            };
            if (Types.Any(other => other.Name == info.Name))
            {
                Log.Warning($"Two components are named {info.Name}: only the first one is used");
                continue;
            }
            Types.Add(info);
            TypesByType.Add(type, info);
        }
        Systems.Sort((first, second) => first.Order != second.Order ? first.Order.CompareTo(second.Order)
                                                                   : first.Found.CompareTo(second.Found));
        return DescribeTypes();
    }

    /// <summary>Forgets the game assembly and everything it defined, keeping the state of its components.</summary>
    public static void Unload()
    {
        PreserveInstances();
        Systems.Clear();
        Types.Clear();
        TypesByType.Clear();
        Failures.Clear();
        if (_context != null)
        {
            _context.Unload();
            _context = null;
            // The collectible context goes away once nothing refers to its types.
            GC.Collect();
            GC.WaitForPendingFinalizers();
        }
        DeleteCopy(_loadedCopy);
        _loadedCopy = null;
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
            info.Bindings[index] = BindField(info, info.Fields[index], offsets[index], index);
        }
        info.Bound = true;
    }

    /// <summary>Runs the components and systems of the scene for this phase.</summary>
    public static void RunPhase(void* scene, SystemPhase phase, float delta)
    {
        SceneView.Bind(scene);
        _running = true;
        try
        {
            if (phase == SystemPhase.Start)
            {
                // A new game, or a new scene: every component starts over.
                foreach (ComponentTypeInfo info in Types)
                {
                    info.Instances.Clear();
                }
                Preserved.Clear();
                Time.Elapsed = 0.0;
                Time.Frame = 0;
            }
            else if (phase == SystemPhase.Update)
            {
                Time.Elapsed += delta;
                ++Time.Frame;
            }
            Time.Delta = delta;

            LoadInstances();
            // Start runs before anything else happens to a component.
            foreach (ComponentTypeInfo info in Types)
            {
                foreach (Entity entity in info.Order)
                {
                    if (info.Instances.TryGetValue(entity.Key, out Component? instance) && !instance.Started)
                    {
                        instance.Started = true;
                        Call(info, instance, "Start", static (component, _) => component.Start(), default);
                    }
                }
            }
            if (phase == SystemPhase.Update)
            {
                DispatchContacts();
            }
            if (phase != SystemPhase.Start)
            {
                foreach (ComponentTypeInfo info in Types)
                {
                    foreach (Entity entity in info.Order)
                    {
                        if (!info.Instances.TryGetValue(entity.Key, out Component? instance))
                        {
                            continue;
                        }
                        if (phase == SystemPhase.Update)
                        {
                            Call(info, instance, "Update", static (component, time) => component.Update(time), delta);
                        }
                        else
                        {
                            Call(info, instance, "FixedUpdate", static (component, time) => component.FixedUpdate(time), delta);
                        }
                    }
                }
            }
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
                    Report($"The system {system.Name}", system.Name, exception);
                }
            }
            StoreInstances();
        }
        finally
        {
            // The reload's leftovers are for the first phase only.
            Preserved.Clear();
            _running = false;
            SceneView.Bind(null);
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
        Store(info, (Component)Activator.CreateInstance(info.Type)!, component);
    }

    /// <summary>The C# component of an entity, made when the entity has it but the phase has not reached it yet.</summary>
    public static Component? GetInstance(Type type, Entity entity)
    {
        if (!TypesByType.TryGetValue(type, out ComponentTypeInfo? info))
        {
            return null;
        }
        if (info.Instances.TryGetValue(entity.Key, out Component? instance))
        {
            return instance;
        }
        if (!_running || !info.Bound)
        {
            return null;
        }
        void* component = Bootstrap.Native.FindComponent(SceneView.Pointer, info.TypeIndex, entity);
        return component != null ? CreateInstance(info, entity, component) : null;
    }

    public static IEnumerable<T> Instances<T>() where T : Component
    {
        return TypesByType.TryGetValue(typeof(T), out ComponentTypeInfo? info)
            ? info.Instances.Values.OfType<T>().ToArray()
            : [];
    }

    public static Component AddComponent(Type type, Entity entity)
    {
        if (!TypesByType.TryGetValue(type, out ComponentTypeInfo? info) || !info.Bound || !_running)
        {
            throw new InvalidOperationException($"{type.Name} is not a component of this game, or the game is not playing");
        }
        if (info.Instances.TryGetValue(entity.Key, out Component? existing))
        {
            return existing;
        }
        // The engine gives the component the defaults of the class, which the instance then reads.
        void* component = Bootstrap.Native.AddComponent(SceneView.Pointer, info.TypeIndex, entity);
        if (component == null)
        {
            throw new InvalidOperationException($"cannot add {type.Name} to '{entity.Name}'");
        }
        return CreateInstance(info, entity, component);
    }

    public static void RemoveComponent(Type type, Entity entity)
    {
        if (!TypesByType.TryGetValue(type, out ComponentTypeInfo? info) || !info.Bound || !_running)
        {
            return;
        }
        Bootstrap.Native.RemoveComponent(SceneView.Pointer, info.TypeIndex, entity);
        info.Instances.Remove(entity.Key);
    }

    // The C# objects follow the components of the scene, and read their values.
    private static void LoadInstances()
    {
        foreach (ComponentTypeInfo info in Types)
        {
            info.Order.Clear();
            if (!info.Bound)
            {
                continue;
            }
            Entity* entities;
            int count = Bootstrap.Native.ComponentEntities(SceneView.Pointer, info.TypeIndex, &entities);
            var seen = new HashSet<ulong>(count);
            for (int index = 0; index < count; ++index)
            {
                Entity entity = entities[index];
                seen.Add(entity.Key);
                info.Order.Add(entity);
            }
            // Components removed since the previous phase lose their object.
            if (info.Instances.Count != 0)
            {
                foreach (ulong key in info.Instances.Keys.Where(key => !seen.Contains(key)).ToArray())
                {
                    info.Instances.Remove(key);
                }
            }
            foreach (Entity entity in info.Order)
            {
                void* component = Bootstrap.Native.FindComponent(SceneView.Pointer, info.TypeIndex, entity);
                if (component == null)
                {
                    continue;
                }
                if (info.Instances.TryGetValue(entity.Key, out Component? instance))
                {
                    Load(info, instance, component);
                }
                else
                {
                    CreateInstance(info, entity, component);
                }
            }
        }
    }

    // The values the code changed during the phase go back to the engine.
    private static void StoreInstances()
    {
        foreach (ComponentTypeInfo info in Types)
        {
            if (!info.Bound || info.Instances.Count == 0)
            {
                continue;
            }
            List<ulong>? gone = null;
            foreach ((ulong key, Component instance) in info.Instances)
            {
                // Components may have moved in memory, or gone, during the phase.
                void* component = Bootstrap.Native.FindComponent(SceneView.Pointer, info.TypeIndex, instance.Entity);
                if (component == null)
                {
                    (gone ??= []).Add(key);
                    continue;
                }
                Store(info, instance, component);
            }
            if (gone != null)
            {
                foreach (ulong key in gone)
                {
                    info.Instances.Remove(key);
                }
            }
        }
    }

    private static Component CreateInstance(ComponentTypeInfo info, Entity entity, void* component)
    {
        var instance = (Component)Activator.CreateInstance(info.Type)!;
        instance.Entity = entity;
        instance.Scene = SceneView;
        if (Preserved.Remove((info.Name, entity.Key), out PreservedState? state))
        {
            RestoreState(info, instance, state);
        }
        Load(info, instance, component);
        info.Instances[entity.Key] = instance;
        return instance;
    }

    // Collisions and triggers of the frame, for the C# components of the entities involved.
    private static void DispatchContacts()
    {
        ReadOnlySpan<Contact> contacts = Physics.Contacts;
        if (contacts.IsEmpty)
        {
            return;
        }
        // Copied, since the handlers may change the scene.
        Contact[] copy = contacts.ToArray();
        foreach (ComponentTypeInfo info in Types)
        {
            if (!info.HandlesCollisions && !info.HandlesTriggers)
            {
                continue;
            }
            foreach (Contact contact in copy)
            {
                if (contact.Trigger ? !info.HandlesTriggers : !info.HandlesCollisions)
                {
                    continue;
                }
                Notify(info, contact.First, contact.Second, contact);
                Notify(info, contact.Second, contact.First, contact);
            }
        }
    }

    private static void Notify(ComponentTypeInfo info, Entity self, Entity other, Contact contact)
    {
        if (!info.Instances.TryGetValue(self.Key, out Component? instance))
        {
            return;
        }
        try
        {
            switch (contact.Trigger, contact.Phase)
            {
                case (false, ContactPhase.Begin):
                    instance.OnCollisionEnter(other);
                    break;
                case (false, ContactPhase.End):
                    instance.OnCollisionExit(other);
                    break;
                case (true, ContactPhase.Begin):
                    instance.OnTriggerEnter(other);
                    break;
                case (true, ContactPhase.End):
                    instance.OnTriggerExit(other);
                    break;
            }
        }
        catch (Exception exception)
        {
            Report($"{info.Name} of '{self.Name}'", $"{info.Name}.collision", exception);
        }
    }

    private static void Call(ComponentTypeInfo info, Component instance, string method, Action<Component, float> action,
                             float delta)
    {
        try
        {
            action(instance, delta);
        }
        catch (Exception exception)
        {
            Report($"{info.Name}.{method} of '{instance.Entity.Name}'", $"{info.Name}.{method}", exception);
        }
    }

    // An error is written with its stack once, then counted, so that a failing Update, on one entity
    // or on many, does not flood the output.
    private static void Report(string where, string what, Exception exception)
    {
        if (exception is TargetInvocationException { InnerException: { } inner })
        {
            exception = inner;
        }
        string key = $"{what}|{exception.GetType().FullName}|{exception.Message}";
        int count = Failures.TryGetValue(key, out int previous) ? previous + 1 : 1;
        Failures[key] = count;
        if (count == 1)
        {
            Log.Error($"{where} failed: {exception}");
        }
        else if (count % 100 == 0)
        {
            Log.Error($"{where} failed again: {exception.GetType().Name}: {exception.Message} ({count} times)");
        }
    }

    private static bool Overrides(Type type, string method)
        => type.GetMethod(method, BindingFlags.Public | BindingFlags.Instance)?.DeclaringType != typeof(Component);

    // The fields of a component that the engine does not save, down to Component.
    private static IEnumerable<FieldInfo> StateFields(ComponentTypeInfo info)
    {
        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        for (Type? current = info.Type; current != null && current != typeof(Component); current = current.BaseType)
        {
            foreach (FieldInfo field in current.GetFields(flags))
            {
                if (Array.IndexOf(info.Fields, field) < 0)
                {
                    yield return field;
                }
            }
        }
    }

    // Keeps what the new code can take: values whose types do not come from the game's assembly.
    private static void PreserveInstances()
    {
        foreach (ComponentTypeInfo info in Types)
        {
            foreach ((ulong key, Component instance) in info.Instances)
            {
                var fields = new Dictionary<string, object?>();
                foreach (FieldInfo field in StateFields(info))
                {
                    object? value = field.GetValue(instance);
                    if (IsShared(field.FieldType) && (value == null || IsShared(value.GetType())))
                    {
                        fields[field.Name] = value;
                    }
                }
                Preserved[(info.Name, key)] = new PreservedState(instance.Started, fields);
            }
            info.Instances.Clear();
        }
    }

    private static void RestoreState(ComponentTypeInfo info, Component instance, PreservedState state)
    {
        instance.Started = state.Started;
        foreach (FieldInfo field in StateFields(info))
        {
            if (state.Fields.TryGetValue(field.Name, out object? value) &&
                (value == null ? !field.FieldType.IsValueType : field.FieldType.IsInstanceOfType(value)))
            {
                field.SetValue(instance, value);
            }
        }
    }

    private static bool IsShared(Type type)
    {
        if (type.Assembly.IsCollectible)
        {
            return false;
        }
        if (type.HasElementType && !IsShared(type.GetElementType()!))
        {
            return false;
        }
        return !type.IsGenericType || type.GetGenericArguments().All(IsShared);
    }

    private static string CopyForLoading(string assemblyPath)
    {
        string root = Path.Combine(Path.GetTempPath(), "devex-csharp", System.Environment.ProcessId.ToString());
        if (_loads == 0)
        {
            CleanCopies(Path.GetDirectoryName(root)!);
        }
        string directory = Path.Combine(root, (++_loads).ToString());
        Directory.CreateDirectory(directory);
        string copy = Path.Combine(directory, Path.GetFileName(assemblyPath));
        File.Copy(assemblyPath, copy, overwrite: true);
        string symbols = Path.ChangeExtension(assemblyPath, ".pdb");
        if (File.Exists(symbols))
        {
            File.Copy(symbols, Path.ChangeExtension(copy, ".pdb"), overwrite: true);
        }
        return copy;
    }

    private static void DeleteCopy(string? directory)
    {
        if (directory == null)
        {
            return;
        }
        try
        {
            Directory.Delete(directory, recursive: true);
        }
        catch (Exception)
        {
            // Still mapped: it goes when the process ends and the next one cleans up.
        }
    }

    // The copies of processes that are gone.
    private static void CleanCopies(string root)
    {
        if (!Directory.Exists(root))
        {
            return;
        }
        foreach (string directory in Directory.GetDirectories(root))
        {
            if (int.TryParse(Path.GetFileName(directory), out int process) && IsRunning(process))
            {
                continue;
            }
            DeleteCopy(directory);
        }
    }

    private static bool IsRunning(int process)
    {
        try
        {
            using var running = System.Diagnostics.Process.GetProcessById(process);
            return !running.HasExited;
        }
        catch (Exception)
        {
            return false;
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

    // What the engine stores a C# field type as.
    private readonly record struct FieldKind(string? Kind, bool List, Type? Element);

    private static FieldKind Describe(Type type)
    {
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>))
        {
            Type element = type.GetGenericArguments()[0];
            string? kind = KindOf(element);
            // std::vector<bool> has no elements to point to.
            return kind != null && kind != "bool" ? new FieldKind(kind, true, element) : new FieldKind(null, false, null);
        }
        return new FieldKind(KindOf(type), false, type);
    }

    /// <summary>The kind the engine stores a value type as, or null when the type is not supported.</summary>
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
        if (type == typeof(Uuid)) return "uuid";
        if (type == typeof(AssetId)) return "asset";
        if (type == typeof(Entity)) return "entity";
        return null;
    }

    // Reading and writing a field of the engine's memory, compiled once per field.
    private static FieldBinding BindField(ComponentTypeInfo info, FieldInfo field, int offset, int fieldIndex)
    {
        ParameterExpression instance = Expression.Parameter(typeof(Component), "instance");
        ParameterExpression memory = Expression.Parameter(typeof(nint), "memory");
        MemberExpression member = Expression.Field(Expression.Convert(instance, info.Type), field);
        ConstantExpression at = Expression.Constant(offset);
        FieldKind kind = Describe(field.FieldType);

        Expression read;
        Expression write;
        if (kind.List)
        {
            ConstantExpression type = Expression.Constant(info.TypeIndex);
            ConstantExpression index = Expression.Constant(fieldIndex);
            MethodInfo reader;
            MethodInfo writer;
            if (kind.Element == typeof(string))
            {
                reader = ReadStringListMethod;
                writer = WriteStringListMethod;
            }
            else if (kind.Element == typeof(Entity))
            {
                reader = ReadEntityListMethod;
                writer = WriteEntityListMethod;
            }
            else
            {
                reader = ReadListMethod.MakeGenericMethod(kind.Element!);
                writer = WriteListMethod.MakeGenericMethod(kind.Element!);
            }
            read = Expression.Call(reader, member, memory, type, index);
            write = Expression.Call(writer, member, memory, type, index);
        }
        else if (field.FieldType == typeof(string))
        {
            read = Expression.Call(ReadStringMethod, memory, at);
            write = Expression.Call(WriteStringMethod, memory, at, Expression.Coalesce(member, Expression.Constant(string.Empty)));
        }
        else if (field.FieldType == typeof(Entity))
        {
            read = Expression.Call(ReadEntityMethod, memory, at);
            write = Expression.Call(WriteEntityMethod, memory, at, member);
        }
        else if (field.FieldType.IsEnum)
        {
            read = Expression.Convert(Expression.Call(ReadMethod.MakeGenericMethod(typeof(int)), memory, at), field.FieldType);
            write = Expression.Call(WriteMethod.MakeGenericMethod(typeof(int)), memory, at, Expression.Convert(member, typeof(int)));
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

    private static MethodInfo Helper(string name)
        => typeof(GameRuntime).GetMethod(name, BindingFlags.NonPublic | BindingFlags.Static)!;

    private static readonly MethodInfo ReadMethod = Helper(nameof(ReadValue));
    private static readonly MethodInfo WriteMethod = Helper(nameof(WriteValue));
    private static readonly MethodInfo ReadStringMethod = Helper(nameof(ReadString));
    private static readonly MethodInfo WriteStringMethod = Helper(nameof(WriteString));
    private static readonly MethodInfo ReadEntityMethod = Helper(nameof(ReadEntity));
    private static readonly MethodInfo WriteEntityMethod = Helper(nameof(WriteEntity));
    private static readonly MethodInfo ReadListMethod = Helper(nameof(ReadList));
    private static readonly MethodInfo WriteListMethod = Helper(nameof(WriteList));
    private static readonly MethodInfo ReadStringListMethod = Helper(nameof(ReadStringList));
    private static readonly MethodInfo WriteStringListMethod = Helper(nameof(WriteStringList));
    private static readonly MethodInfo ReadEntityListMethod = Helper(nameof(ReadEntityList));
    private static readonly MethodInfo WriteEntityListMethod = Helper(nameof(WriteEntityList));

    private static T ReadValue<T>(nint memory, int offset) where T : unmanaged
        => Unsafe.ReadUnaligned<T>((void*)(memory + offset));

    private static void WriteValue<T>(nint memory, int offset, T value) where T : unmanaged
        => Unsafe.WriteUnaligned((void*)(memory + offset), value);

    private static string ReadString(nint memory, int offset)
        => Utf8.ToString(Bootstrap.Native.ReadString((void*)(memory + offset))) ?? string.Empty;

    private static void WriteString(nint memory, int offset, string value)
    {
        using var text = new Utf8Buffer(value);
        Bootstrap.Native.WriteString((void*)(memory + offset), text.Pointer);
    }

    // Entity fields hold the UUID of their entity in the engine.
    private static Entity ReadEntity(nint memory, int offset)
        => Bootstrap.Native.ResolveEntity(SceneView.Pointer, (Uuid*)(memory + offset));

    private static void WriteEntity(nint memory, int offset, Entity entity)
        => Bootstrap.Native.EntityUuid(SceneView.Pointer, entity, (Uuid*)(memory + offset));

    // Lists are std::vectors in the engine; the C# list is reused from phase to phase.
    private static List<T> ReadList<T>(List<T>? list, nint memory, nuint type, int field) where T : unmanaged
    {
        list ??= [];
        int count = (int)Bootstrap.Native.ListSize(type, field, (void*)memory);
        CollectionsMarshal.SetCount(list, count);
        if (count > 0)
        {
            new ReadOnlySpan<T>(Bootstrap.Native.ListElement(type, field, (void*)memory, 0), count)
                .CopyTo(CollectionsMarshal.AsSpan(list));
        }
        return list;
    }

    private static void WriteList<T>(List<T>? list, nint memory, nuint type, int field) where T : unmanaged
    {
        int count = list?.Count ?? 0;
        Bootstrap.Native.ListResize(type, field, (void*)memory, (nuint)count);
        if (count > 0)
        {
            CollectionsMarshal.AsSpan(list).CopyTo(new Span<T>(Bootstrap.Native.ListElement(type, field, (void*)memory, 0), count));
        }
    }

    private static List<string> ReadStringList(List<string>? list, nint memory, nuint type, int field)
    {
        list ??= [];
        list.Clear();
        int count = (int)Bootstrap.Native.ListSize(type, field, (void*)memory);
        for (int index = 0; index < count; ++index)
        {
            list.Add(Utf8.ToString(Bootstrap.Native.ReadString(Bootstrap.Native.ListElement(type, field, (void*)memory, (nuint)index)))
                     ?? string.Empty);
        }
        return list;
    }

    private static void WriteStringList(List<string>? list, nint memory, nuint type, int field)
    {
        int count = list?.Count ?? 0;
        Bootstrap.Native.ListResize(type, field, (void*)memory, (nuint)count);
        for (int index = 0; index < count; ++index)
        {
            using var text = new Utf8Buffer(list![index] ?? string.Empty);
            Bootstrap.Native.WriteString(Bootstrap.Native.ListElement(type, field, (void*)memory, (nuint)index), text.Pointer);
        }
    }

    private static List<Entity> ReadEntityList(List<Entity>? list, nint memory, nuint type, int field)
    {
        list ??= [];
        list.Clear();
        int count = (int)Bootstrap.Native.ListSize(type, field, (void*)memory);
        for (int index = 0; index < count; ++index)
        {
            var uuid = (Uuid*)Bootstrap.Native.ListElement(type, field, (void*)memory, (nuint)index);
            list.Add(Bootstrap.Native.ResolveEntity(SceneView.Pointer, uuid));
        }
        return list;
    }

    private static void WriteEntityList(List<Entity>? list, nint memory, nuint type, int field)
    {
        int count = list?.Count ?? 0;
        Bootstrap.Native.ListResize(type, field, (void*)memory, (nuint)count);
        for (int index = 0; index < count; ++index)
        {
            var uuid = (Uuid*)Bootstrap.Native.ListElement(type, field, (void*)memory, (nuint)index);
            Bootstrap.Native.EntityUuid(SceneView.Pointer, list![index], uuid);
        }
    }

    /// <summary>The components and their fields, in the text format the engine reads.</summary>
    private static string DescribeTypes()
    {
        var text = new StringBuilder();
        foreach (ComponentTypeInfo info in Types)
        {
            text.Append($"[component type=\"{info.Name}\"]\n\n");
            foreach (FieldInfo field in info.Fields)
            {
                FieldKind kind = Describe(field.FieldType);
                text.Append($"[field name=\"{FileName(field.Name)}\" kind=\"{kind.Kind}\"");
                if (kind.List)
                {
                    text.Append(" list=true");
                }
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
                if (field.GetCustomAttribute<AudioGroupAttribute>() != null)
                {
                    text.Append(" audio_group=true");
                }
                if (field.GetCustomAttribute<AssetTypeAttribute>() is { } asset)
                {
                    text.Append($" asset_type=\"{asset.Type}\"");
                }
                if (kind.Element is { IsEnum: true } enumeration)
                {
                    text.Append($" values=\"{string.Join(',', Enum.GetNames(enumeration).Select(FileName))}\"");
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
