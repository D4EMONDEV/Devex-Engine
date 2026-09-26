using System.Runtime.InteropServices;

namespace Devex;

/// <summary>How a tween moves between its ends, as the Tweener component's ease names them.</summary>
public enum Ease
{
    Linear,
    InQuad,
    OutQuad,
    InOutQuad,
    InCubic,
    OutCubic,
    InOutCubic,
    InSine,
    OutSine,
    InOutSine,
    InExpo,
    OutExpo,
    InOutExpo,
    InBack,
    OutBack,
    InOutBack,
    InElastic,
    OutElastic,
    InOutElastic,
    InBounce,
    OutBounce,
    InOutBounce,
}

/// <summary>What a tween does once it reaches its end.</summary>
public enum TweenLoop
{
    /// <summary>It ends.</summary>
    None,
    /// <summary>It starts over from its start.</summary>
    Restart,
    /// <summary>It goes back to its start, then forth again.</summary>
    PingPong,
}

/// <summary>
/// A tween: a field of a component of an entity, from one value to another over some time. The field
/// holds a number, a vector, a color or a rotation, which is given in degrees around x, y and z.
/// <code>
/// new TweenSpec(door, "Transform.position", new Vec3(0, 3, 0), 1.5f) { Relative = true, Ease = Ease.InOutSine }.Play();
/// </code>
/// </summary>
public struct TweenSpec
{
    public Entity Entity;
    /// <summary>"Component.field": "Transform.position", "UiRect.opacity", "UiImage.color", "PointLight.intensity".</summary>
    public string Field;
    /// <summary>As many components as the field has: X for a number, XYZ for a position, all four for a color.</summary>
    public Vec4 To;
    /// <summary>The start; null starts from the value the field has once the delay is over.</summary>
    public Vec4? From;
    /// <summary>To is added to the start rather than reached.</summary>
    public bool Relative;
    /// <summary>In seconds.</summary>
    public float Duration;
    public float Delay;
    public Ease Ease;
    /// <summary>A curve asset drawn in the editor, which replaces the ease when set.</summary>
    public AssetId Curve;
    public TweenLoop Loop;
    /// <summary>How many times it plays again once over, with a loop; -1 without end.</summary>
    public int Repeats;

    public TweenSpec(Entity entity, string field, Vec4 to, float duration)
    {
        Entity = entity;
        Field = field;
        To = to;
        Duration = duration;
        Ease = Ease.OutQuad;
    }

    public TweenSpec(Entity entity, string field, Vec3 to, float duration)
        : this(entity, field, new Vec4(to.X, to.Y, to.Z, 0.0f), duration)
    {
    }

    public TweenSpec(Entity entity, string field, Vec2 to, float duration)
        : this(entity, field, new Vec4(to.X, to.Y, 0.0f, 0.0f), duration)
    {
    }

    public TweenSpec(Entity entity, string field, float to, float duration)
        : this(entity, field, new Vec4(to, 0.0f, 0.0f, 0.0f), duration)
    {
    }

    /// <summary>Plays it; throws when the entity lacks the component or the field cannot be animated.</summary>
    public readonly TweenHandle Play() => Tween.Play(this);
}

/// <summary>
/// A tween or a sequence that plays: what stops it, and what a coroutine awaits.
/// <code>await Tween.Fade(panel, 0.0f, 0.3f);</code>
/// </summary>
public readonly unsafe struct TweenHandle : IEquatable<TweenHandle>
{
    internal TweenHandle(ulong id) => Id = id;

    internal ulong Id { get; }

    /// <summary>While it plays, waits for its delay or is paused.</summary>
    public bool IsPlaying => Id != 0 && Bootstrap.Native.IsTweenPlaying(Id) != 0;

    /// <summary>Stops it where it is, or, with complete, at its end.</summary>
    public void Kill(bool complete = false)
    {
        if (Id != 0)
        {
            Bootstrap.Native.KillTween(Id, complete ? 1 : 0);
        }
    }

    public void Pause()
    {
        if (Id != 0)
        {
            Bootstrap.Native.PauseTween(Id);
        }
    }

    /// <summary>Goes on from where Pause held it.</summary>
    public void Resume()
    {
        if (Id != 0)
        {
            Bootstrap.Native.ResumeTween(Id);
        }
    }

    /// <summary>Awaiting a tween waits for its end, within a coroutine or any async method.</summary>
    public WaitAwaiter GetAwaiter() => new(new CoroutineWait(CoroutineWaitKind.Tween) { Tween = Id });

    public bool Equals(TweenHandle other) => Id == other.Id;

    public override bool Equals(object? other) => other is TweenHandle handle && Equals(handle);

    public override int GetHashCode() => Id.GetHashCode();

    public static bool operator ==(TweenHandle left, TweenHandle right) => left.Equals(right);

    public static bool operator !=(TweenHandle left, TweenHandle right) => !left.Equals(right);
}

/// <summary>
/// Tweens that play one step after the other; the tweens of a step play together.
/// <code>
/// await new TweenSequence()
///     .Append(new TweenSpec(label, "UiRect.opacity", 1.0f, 0.2f))
///     .AppendInterval(1.5f)
///     .Append(new TweenSpec(label, "UiRect.opacity", 0.0f, 0.4f))
///     .Play();
/// </code>
/// </summary>
public sealed class TweenSequence
{
    private readonly List<(TweenSpec Tween, int Step)> _tweens = [];
    private readonly List<float> _intervals = [];

    /// <summary>A step of its own, after the others.</summary>
    public TweenSequence Append(TweenSpec tween)
    {
        _intervals.Add(0.0f);
        _tweens.Add((tween, _intervals.Count - 1));
        return this;
    }

    /// <summary>Plays with the last step, or in a first step.</summary>
    public TweenSequence Join(TweenSpec tween)
    {
        if (_intervals.Count == 0)
        {
            _intervals.Add(0.0f);
        }
        _tweens.Add((tween, _intervals.Count - 1));
        return this;
    }

    /// <summary>A wait after the last step.</summary>
    public TweenSequence AppendInterval(float seconds)
    {
        if (_intervals.Count == 0)
        {
            _intervals.Add(0.0f);
        }
        _intervals[^1] += seconds;
        return this;
    }

    /// <summary>Plays it; the handle ends with its last step.</summary>
    public unsafe TweenHandle Play()
    {
        var tweens = new Tween.NativeTween[_tweens.Count];
        var texts = new byte[_tweens.Count][];
        for (int index = 0; index < _tweens.Count; ++index)
        {
            texts[index] = Utf8.ToBytes(_tweens[index].Tween.Field ?? string.Empty);
        }
        var handles = new GCHandle[texts.Length];
        try
        {
            for (int index = 0; index < _tweens.Count; ++index)
            {
                handles[index] = GCHandle.Alloc(texts[index], GCHandleType.Pinned);
                tweens[index] = Tween.ToNative(_tweens[index].Tween, (byte*)handles[index].AddrOfPinnedObject(),
                                               _tweens[index].Step);
            }
            float[] intervals = [.. _intervals];
            byte* error = null;
            ulong id;
            fixed (Tween.NativeTween* first = tweens)
            fixed (float* firstInterval = intervals)
            {
                id = Bootstrap.Native.PlaySequence(Scene.Current.Pointer, first, tweens.Length, firstInterval,
                                                   intervals.Length, &error);
            }
            return id != 0 ? new TweenHandle(id) : throw new InvalidOperationException(Utf8.ToString(error));
        }
        finally
        {
            foreach (GCHandle handle in handles)
            {
                if (handle.IsAllocated)
                {
                    handle.Free();
                }
            }
        }
    }
}

/// <summary>
/// The tweens of the game: fields of components that go from one value to another over some time,
/// written each frame before the interface is laid out. A tween ends with its entity.
/// </summary>
public static unsafe class Tween
{
    // In the layout of NativeTween in the engine's ManagedGame.cpp.
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeTween
    {
        public Entity Entity;
        public byte* Field;
        public Vec4 To;
        public Vec4 From;
        public Uuid Curve;
        public float Duration;
        public float Delay;
        public int HasFrom;
        public int Relative;
        public int Ease;
        public int Loop;
        public int Repeats;
        public int Step;
    }

    internal static NativeTween ToNative(in TweenSpec tween, byte* field, int step) => new()
    {
        Entity = tween.Entity,
        Field = field,
        To = tween.To,
        From = tween.From ?? default,
        Curve = tween.Curve.Uuid,
        Duration = tween.Duration,
        Delay = tween.Delay,
        HasFrom = tween.From.HasValue ? 1 : 0,
        Relative = tween.Relative ? 1 : 0,
        Ease = (int)tween.Ease,
        Loop = (int)tween.Loop,
        Repeats = tween.Repeats,
        Step = step,
    };

    /// <summary>Plays a tween; throws when the entity lacks the component or the field cannot be animated.</summary>
    public static TweenHandle Play(in TweenSpec tween)
    {
        using var field = new Utf8Buffer(tween.Field ?? string.Empty);
        NativeTween native = ToNative(tween, field.Pointer, 0);
        byte* error = null;
        ulong id = Bootstrap.Native.PlayTween(Scene.Current.Pointer, &native, &error);
        return id != 0 ? new TweenHandle(id) : throw new InvalidOperationException(Utf8.ToString(error));
    }

    /// <summary>Tweens any field that holds a number.</summary>
    public static TweenHandle To(Entity entity, string field, float to, float duration, Ease ease = Ease.OutQuad)
        => Play(new TweenSpec(entity, field, to, duration) { Ease = ease });

    /// <summary>Tweens any field that holds a vector, a color or a rotation.</summary>
    public static TweenHandle To(Entity entity, string field, Vec4 to, float duration, Ease ease = Ease.OutQuad)
        => Play(new TweenSpec(entity, field, to, duration) { Ease = ease });

    /// <summary>Moves the entity to a position, relative to its parent.</summary>
    public static TweenHandle Move(Entity entity, Vec3 position, float duration, Ease ease = Ease.OutQuad)
        => Play(new TweenSpec(entity, "Transform.position", position, duration) { Ease = ease });

    /// <summary>Moves the entity by an offset.</summary>
    public static TweenHandle MoveBy(Entity entity, Vec3 offset, float duration, Ease ease = Ease.OutQuad)
        => Play(new TweenSpec(entity, "Transform.position", offset, duration) { Ease = ease, Relative = true });

    /// <summary>Turns the entity to a rotation, in degrees around x, y and z.</summary>
    public static TweenHandle Rotate(Entity entity, Vec3 degrees, float duration, Ease ease = Ease.OutQuad)
        => Play(new TweenSpec(entity, "Transform.rotation", degrees, duration) { Ease = ease });

    public static TweenHandle Scale(Entity entity, Vec3 scale, float duration, Ease ease = Ease.OutQuad)
        => Play(new TweenSpec(entity, "Transform.scale", scale, duration) { Ease = ease });

    /// <summary>Fades an element of the interface, and those inside it.</summary>
    public static TweenHandle Fade(Entity entity, float opacity, float duration, Ease ease = Ease.OutQuad)
        => Play(new TweenSpec(entity, "UiRect.opacity", opacity, duration) { Ease = ease });

    /// <summary>Starts the Tweener of the entity, which does not start by itself without Play On Start.</summary>
    public static TweenHandle PlayTweener(Entity entity)
    {
        byte* error = null;
        ulong id = Bootstrap.Native.PlayTweener(Scene.Current.Pointer, entity, &error);
        return id != 0 ? new TweenHandle(id) : throw new InvalidOperationException(Utf8.ToString(error));
    }
}
