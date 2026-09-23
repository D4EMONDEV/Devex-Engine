using System.Runtime.InteropServices;

namespace Devex;

/// <summary>
/// Measures where the time of a frame goes. The engine already measures its own phases, each C#
/// component type and each system; a zone of your own shows beside them in the Profiler panel of
/// the editor:
/// <code>
/// using (Profiler.Scope("Find a path"))
/// {
///     ...
/// }
/// </code>
/// </summary>
public static unsafe class Profiler
{
    // Each name crosses to the engine once, as UTF-8 text kept for the life of the game.
    private static readonly Dictionary<string, nint> Names = [];

    /// <summary>
    /// Whether the engine measures zones: while the Profiler panel of the editor, or of the tools
    /// shown over a game, is open. Otherwise a zone costs almost nothing.
    /// </summary>
    public static bool IsEnabled => Bootstrap.Native.ProfileEnabled() != 0;

    /// <summary>Measures until the returned scope is disposed.</summary>
    public static ProfileScope Scope(string name) => new(name);

    internal static void Begin(string name) => Bootstrap.Native.ProfileBegin((byte*)NameOf(name));

    internal static void End() => Bootstrap.Native.ProfileEnd();

    private static nint NameOf(string name)
    {
        lock (Names)
        {
            if (!Names.TryGetValue(name, out nint text))
            {
                text = Marshal.StringToCoTaskMemUTF8(name);
                Names.Add(name, text);
            }
            return text;
        }
    }
}

/// <summary>A zone of the profiler, which ends when it is disposed.</summary>
public readonly struct ProfileScope : IDisposable
{
    private readonly bool _active;

    internal ProfileScope(string name)
    {
        _active = Profiler.IsEnabled;
        if (_active)
        {
            Profiler.Begin(name);
        }
    }

    /// <summary>Ends the zone.</summary>
    public void Dispose()
    {
        if (_active)
        {
            Profiler.End();
        }
    }
}
