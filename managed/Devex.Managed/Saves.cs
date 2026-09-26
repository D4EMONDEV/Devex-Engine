using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace Devex;

/// <summary>A save as a list of saves shows it.</summary>
public sealed class SaveSlot
{
    public required string Name { get; init; }
    /// <summary>What the game called it when saving: "Chapter 2", the name of a place.</summary>
    public required string Label { get; init; }
    /// <summary>When it was written, in the local time of the machine.</summary>
    public required DateTime Time { get; init; }
    /// <summary>Seconds played until then.</summary>
    public required double PlayTime { get; init; }
    public required AssetId Scene { get; init; }
    public required string SceneName { get; init; }
    /// <summary>The version the game gave what it saved.</summary>
    public required int Version { get; init; }
    /// <summary>Whether loading it brings back the scene it saved.</summary>
    public required bool HasScene { get; init; }
    public required bool HasThumbnail { get; init; }

    /// <summary>The picture of the save as a texture a UiImage shows, or an invalid asset while there is none.</summary>
    public AssetId Thumbnail => Saves.Thumbnail(Name);
}

/// <summary>
/// The saves of the game, in the folder of the player. A save is an object of the game, whose public
/// fields are written as those of a component are, with the scene that plays and a picture of it:
/// <code>
/// public class Progress { public int Level; public List&lt;string&gt; Items = []; }
/// Saves.Save("1", progress, label: "Level 2");
/// Progress? loaded = Saves.Load&lt;Progress&gt;("1");
/// </code>
/// Loading a save that kept its scene puts that scene back at the end of the frame; its Start runs
/// with <see cref="RestoredSlot"/> set, to leave alone what the save brought back. Fields the object no
/// longer has are skipped and new ones keep their defaults, so that older saves still load.
/// </summary>
public static unsafe class Saves
{
    private sealed class SaveType(int handle, GameRuntime.FieldBinding[] bindings)
    {
        public int Handle { get; } = handle;
        public GameRuntime.FieldBinding[] Bindings { get; } = bindings;
    }

    // Engine types are named with this flag, so that their lists are found apart from components'.
    private const nuint SaveTypeFlag = (nuint)1 << 30;
    private static readonly Dictionary<Type, SaveType> Types = [];

    /// <summary>Writes the object into the slot, keeping the save it held beside it.</summary>
    /// <param name="slot">Letters, digits, spaces, dashes, dots and underscores: "1", "auto", "quick".</param>
    /// <param name="data">An object whose public fields are saved as those of a component are.</param>
    /// <param name="label">Shown in the list of saves: "Chapter 2", the name of a place.</param>
    /// <param name="scene">Keeps the scene as it plays, which loading brings back.</param>
    /// <param name="thumbnail">Keeps a picture of the next frame, without the interface.</param>
    /// <param name="version">The version of what the game saves, to convert older saves once it changes.</param>
    public static void Save<T>(string slot, T data, string label = "", bool scene = true, bool thumbnail = true, int version = 0)
        where T : class
    {
        ArgumentNullException.ThrowIfNull(data);
        SaveType type = TypeOf(data.GetType());
        void* memory = Bootstrap.Native.CreateSaveObject(type.Handle);
        try
        {
            foreach (GameRuntime.FieldBinding binding in type.Bindings)
            {
                binding.Store(data, (nint)memory);
            }
            using var slotText = new Utf8Buffer(slot);
            using var labelText = new Utf8Buffer(label);
            byte* error = null;
            if (Bootstrap.Native.WriteSave(slotText.Pointer, type.Handle, memory, labelText.Pointer, scene ? 1 : 0,
                                           thumbnail ? 1 : 0, version, &error) == 0)
            {
                throw new InvalidOperationException($"cannot save '{slot}': {Utf8.ToString(error)}");
            }
        }
        finally
        {
            Bootstrap.Native.DestroySaveObject(type.Handle, memory);
        }
    }

    /// <summary>
    /// Reads the save of the slot, or null when it has none. With <paramref name="restoreScene"/>, the scene it
    /// kept replaces the one that plays at the end of the frame, and the time played carries on from it.
    /// </summary>
    public static T? Load<T>(string slot, bool restoreScene = true) where T : class, new()
    {
        SaveType type = TypeOf(typeof(T));
        var data = new T();
        void* memory = Bootstrap.Native.CreateSaveObject(type.Handle);
        try
        {
            // The fields the save does not hold keep the values the class gives them.
            foreach (GameRuntime.FieldBinding binding in type.Bindings)
            {
                binding.Store(data, (nint)memory);
            }
            using var slotText = new Utf8Buffer(slot);
            byte* error = null;
            int read = Bootstrap.Native.ReadSave(slotText.Pointer, type.Handle, memory, restoreScene ? 1 : 0, &error);
            if (read == 0)
            {
                return null;
            }
            if (read < 0)
            {
                throw new InvalidOperationException($"cannot load '{slot}': {Utf8.ToString(error)}");
            }
            foreach (GameRuntime.FieldBinding binding in type.Bindings)
            {
                binding.Load(data, (nint)memory);
            }
            return data;
        }
        finally
        {
            Bootstrap.Native.DestroySaveObject(type.Handle, memory);
        }
    }

    /// <summary>Every save, the most recent first.</summary>
    public static SaveSlot[] List()
    {
        int count = Bootstrap.Native.SaveSlots();
        var slots = new SaveSlot[Math.Max(count, 0)];
        for (int index = 0; index < slots.Length; ++index)
        {
            NativeSaveSlot native;
            Bootstrap.Native.SaveSlotAt(index, &native);
            slots[index] = Describe(native);
        }
        return slots;
    }

    /// <summary>The save of the slot, or null when it has none.</summary>
    public static SaveSlot? Find(string slot)
    {
        using var text = new Utf8Buffer(slot);
        NativeSaveSlot native;
        return Bootstrap.Native.FindSaveSlot(text.Pointer, &native) != 0 ? Describe(native) : null;
    }

    public static bool Exists(string slot) => Find(slot) != null;

    /// <summary>Removes the save of the slot, its picture and the save it kept.</summary>
    public static void Delete(string slot)
    {
        using var text = new Utf8Buffer(slot);
        Bootstrap.Native.DeleteSave(text.Pointer);
    }

    /// <summary>The slot the scene that plays came back from, while its Start runs; null otherwise.</summary>
    public static string? RestoredSlot
    {
        get
        {
            string? slot = Utf8.ToString(Bootstrap.Native.RestoredSlot());
            return string.IsNullOrEmpty(slot) ? null : slot;
        }
    }

    /// <summary>Seconds played, carried on from the save loaded last.</summary>
    public static double PlayTime => Bootstrap.Native.PlayTime();

    /// <summary>The picture of the save as a texture a UiImage shows, or an invalid asset while there is none.</summary>
    public static AssetId Thumbnail(string slot)
    {
        using var text = new Utf8Buffer(slot);
        Uuid texture;
        Bootstrap.Native.SaveThumbnail(text.Pointer, &texture);
        return new AssetId(texture);
    }

    /// <summary>Forgets the types of a game that goes away, so that its assembly can unload.</summary>
    internal static void Forget() => Types.Clear();

    private static SaveType TypeOf(Type type)
    {
        if (Types.TryGetValue(type, out SaveType? known))
        {
            return known;
        }
        FieldInfo[] fields = GameRuntime.SavedFields(type);
        var description = new StringBuilder();
        GameRuntime.DescribeType(description, type.Name, fields);
        nuint* offsets = stackalloc nuint[Math.Max(fields.Length, 1)];
        using var text = new Utf8Buffer(description.ToString());
        int handle = Bootstrap.Native.RegisterSaveType(text.Pointer, offsets, fields.Length);
        if (handle < 0)
        {
            throw new InvalidOperationException($"{type.Name} cannot be saved");
        }
        var bindings = new GameRuntime.FieldBinding[fields.Length];
        for (int index = 0; index < fields.Length; ++index)
        {
            bindings[index] = GameRuntime.BindField(type, SaveTypeFlag | (nuint)handle, fields[index], (int)offsets[index], index);
        }
        var saveType = new SaveType(handle, bindings);
        Types.Add(type, saveType);
        return saveType;
    }

    private static SaveSlot Describe(NativeSaveSlot native) => new()
    {
        Name = Utf8.ToString(native.Name) ?? string.Empty,
        Label = Utf8.ToString(native.Label) ?? string.Empty,
        SceneName = Utf8.ToString(native.SceneName) ?? string.Empty,
        Time = DateTimeOffset.FromUnixTimeSeconds(native.Time).LocalDateTime,
        PlayTime = native.PlayTime,
        Scene = new AssetId(native.Scene),
        Version = native.Version,
        HasScene = native.HasScene != 0,
        HasThumbnail = native.HasThumbnail != 0,
    };
}

/// <summary>A save as the engine lists it; its texts live until the next list.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct NativeSaveSlot
{
    public byte* Name;
    public byte* Label;
    public byte* SceneName;
    public byte* Type;
    public long Time;
    public double PlayTime;
    public Uuid Scene;
    public int Version;
    public int HasScene;
    public int HasThumbnail;
}

/// <summary>
/// What the player chose, kept from one game to the next in the folder of the player: the volumes, the
/// window, and the values the game keeps by name, such as a language or the sensitivity of the mouse.
/// The engine applies the volumes at once, full screen outside the editor, and vertical sync from the
/// next launch.
/// </summary>
public static unsafe class PlayerSettings
{
    /// <summary>Full screen, or the window; the window as it is while the player has not chosen.</summary>
    public static bool Fullscreen
    {
        get => Bootstrap.Native.SettingsFullscreen() != 0;
        set => Bootstrap.Native.SetSettingsFullscreen(value ? 1 : 0);
    }

    /// <summary>Waits for the display between frames; takes effect from the next launch of the game.</summary>
    public static bool VSync
    {
        get => Bootstrap.Native.SettingsVsync() != 0;
        set => Bootstrap.Native.SetSettingsVsync(value ? 1 : 0);
    }

    /// <summary>The volume the player chose for an audio group, or for every sound with <see cref="Audio.Master"/>: 1 by default.</summary>
    public static float GetVolume(string group)
    {
        using var text = new Utf8Buffer(group);
        float volume = Bootstrap.Native.SettingsVolume(text.Pointer);
        return volume >= 0.0f ? volume : throw new ArgumentException($"no audio group {group}");
    }

    /// <summary>Sets the volume the player chose, from 0 to 1; it multiplies the volumes the game sets.</summary>
    public static void SetVolume(string group, float volume)
    {
        using var text = new Utf8Buffer(group);
        if (Bootstrap.Native.SetSettingsVolume(text.Pointer, volume) == 0)
        {
            throw new ArgumentException($"no audio group {group}");
        }
    }

    public static bool GetBool(string key, bool fallback = false)
    {
        using var text = new Utf8Buffer(key);
        int value = 0;
        return Bootstrap.Native.SettingsBool(text.Pointer, &value) != 0 ? value != 0 : fallback;
    }

    public static int GetInt(string key, int fallback = 0)
    {
        using var text = new Utf8Buffer(key);
        long value = 0;
        return Bootstrap.Native.SettingsInteger(text.Pointer, &value) != 0 ? (int)Math.Clamp(value, int.MinValue, int.MaxValue) : fallback;
    }

    public static float GetFloat(string key, float fallback = 0.0f)
    {
        using var text = new Utf8Buffer(key);
        double value = 0.0;
        return Bootstrap.Native.SettingsNumber(text.Pointer, &value) != 0 ? (float)value : fallback;
    }

    public static string GetString(string key, string fallback = "")
    {
        using var text = new Utf8Buffer(key);
        return Utf8.ToString(Bootstrap.Native.SettingsString(text.Pointer)) ?? fallback;
    }

    public static void SetBool(string key, bool value)
    {
        using var text = new Utf8Buffer(key);
        Bootstrap.Native.SetSettingsBool(text.Pointer, value ? 1 : 0);
    }

    public static void SetInt(string key, int value)
    {
        using var text = new Utf8Buffer(key);
        Bootstrap.Native.SetSettingsInteger(text.Pointer, value);
    }

    public static void SetFloat(string key, float value)
    {
        using var text = new Utf8Buffer(key);
        Bootstrap.Native.SetSettingsNumber(text.Pointer, value);
    }

    public static void SetString(string key, string value)
    {
        using var text = new Utf8Buffer(key);
        using var valueText = new Utf8Buffer(value);
        Bootstrap.Native.SetSettingsString(text.Pointer, valueText.Pointer);
    }

    public static bool Has(string key)
    {
        using var text = new Utf8Buffer(key);
        double number = 0.0;
        int flag = 0;
        return Bootstrap.Native.SettingsNumber(text.Pointer, &number) != 0 || Bootstrap.Native.SettingsBool(text.Pointer, &flag) != 0 ||
               Bootstrap.Native.SettingsString(text.Pointer) != null;
    }

    public static void Remove(string key)
    {
        using var text = new Utf8Buffer(key);
        Bootstrap.Native.RemoveSettingsValue(text.Pointer);
    }
}
