// What games can call of Devex.Managed only grows: a member that leaves breaks the games written against it. This
// compares it with the list in tests/data/formats, and DEVEX_UPDATE_FORMATS=1 records it after a change that is meant.
using System.Reflection;
using System.Runtime.CompilerServices;

if (args.Length != 2)
{
    Console.Error.WriteLine("usage: ApiProbe <Devex.Managed.dll> <api list>");
    return 2;
}

const BindingFlags Declared = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;

static bool Visible(MethodBase? method) => method != null && (method.IsPublic || method.IsFamily || method.IsFamilyOrAssembly);

static bool Listed(MemberInfo member) => member switch
{
    ConstructorInfo constructor => Visible(constructor),
    // Properties and events say so themselves, and operators are what they are.
    MethodInfo method => Visible(method) && (!method.IsSpecialName || method.Name.StartsWith("op_", StringComparison.Ordinal)),
    FieldInfo field => !field.IsSpecialName && (field.IsPublic || field.IsFamily || field.IsFamilyOrAssembly),
    PropertyInfo property => Visible(property.GetMethod) || Visible(property.SetMethod),
    EventInfo @event => Visible(@event.AddMethod),
    _ => false,
};

static void AddAccessor(SortedSet<string> api, Type type, MethodInfo? accessor)
{
    if (accessor == null || !Visible(accessor))
        return;

    string access = accessor.IsPublic ? "public" : "protected";
    string storage = accessor.IsStatic ? " static" : "";
    string init = accessor.ReturnParameter.GetRequiredCustomModifiers().Contains(typeof(IsExternalInit)) ? " init" : "";
    // Record each accessor independently. A property can gain a setter, but games may already use one it has.
    api.Add($"{type.FullName}: {access}{storage}{init} {accessor}");
}

SortedSet<string> api = new(StringComparer.Ordinal);
foreach (Type type in Assembly.LoadFrom(Path.GetFullPath(args[0])).GetExportedTypes())
{
    api.Add($"type {type.FullName}");
    foreach (MemberInfo member in type.GetMembers(Declared))
    {
        if (Listed(member))
        {
            api.Add($"{type.FullName}: {member}");
            if (member is PropertyInfo property)
            {
                AddAccessor(api, type, property.GetMethod);
                AddAccessor(api, type, property.SetMethod);
            }
            else if (member is EventInfo @event)
            {
                AddAccessor(api, type, @event.AddMethod);
                AddAccessor(api, type, @event.RemoveMethod);
            }
        }
    }
}

bool updating = !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("DEVEX_UPDATE_FORMATS"));
HashSet<string> recorded = updating && !File.Exists(args[1])
    ? new(StringComparer.Ordinal)
    : File.ReadLines(args[1]).Where(line => line.Length != 0).ToHashSet(StringComparer.Ordinal);
List<string> gone = recorded.Where(line => !api.Contains(line)).Order(StringComparer.Ordinal).ToList();
foreach (string line in gone)
{
    Console.Error.WriteLine($"Gone from the C# API: {line}");
}
if (gone.Count != 0)
{
    Console.Error.WriteLine("Games use what is gone: keep it, marked [Obsolete], until 2.0 (see docs/compatibilite.md).");
    return 1;
}
if (updating)
{
    File.WriteAllText(args[1], string.Concat(api.Select(line => line + "\n")));
    Console.WriteLine($"Recorded {api.Count} lines in {args[1]}");
    return 0;
}
List<string> added = api.Where(line => !recorded.Contains(line)).ToList();
foreach (string line in added)
{
    Console.Error.WriteLine($"New in the C# API, to record with DEVEX_UPDATE_FORMATS=1: {line}");
}
return added.Count == 0 ? 0 : 1;
