using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;

namespace Devex;

/// <summary>
/// A coroutine: an async method that waits for time, frames, conditions or tweens in the middle of
/// what it does, and goes on during the Update of the frame its wait ends in.
/// <code>
/// public override void Start() => Blink();
///
/// async Coroutine Blink()
/// {
///     while (true)
///     {
///         await Wait.Seconds(0.5f);
///         await Tween.Fade(Entity, 0.0f, 0.2f);
///         await Tween.Fade(Entity, 1.0f, 0.2f);
///     }
/// }
/// </code>
/// Calling it starts it. One started by a component, or by a coroutine of the component, ends with
/// the component; the others end with the scene. All of them end when the scene is replaced, the
/// game stops or the code reloads, without running their finally blocks.
/// </summary>
[AsyncMethodBuilder(typeof(CoroutineMethodBuilder))]
public sealed class Coroutine
{
    private ExceptionDispatchInfo? _failure;
    private Action? _resume;

    internal Coroutine(Component? owner) => Owner = owner;

    /// <summary>The component it ends with; null for one that ends with the scene.</summary>
    internal Component? Owner { get; }

    /// <summary>The method, on the heap once it first waits.</summary>
    internal IAsyncStateMachine? Machine { get; set; }

    /// <summary>Something awaited it once it failed, which reports the error instead.</summary>
    internal bool Observed { get; set; }

    internal Exception? Failure => _failure?.SourceException;

    /// <summary>It reached its end, failed, or was stopped.</summary>
    public bool IsDone { get; private set; }

    /// <summary>It was stopped, or ended with its component or the scene, before its end.</summary>
    public bool IsStopped { get; private set; }

    /// <summary>Ends it where it waits. Coroutines that await it go on.</summary>
    public void Stop()
    {
        if (!IsDone)
        {
            IsDone = true;
            IsStopped = true;
        }
    }

    /// <summary>Awaiting a coroutine waits for its end, and throws what it failed with.</summary>
    public CoroutineAwaiter GetAwaiter() => new(this);

    // Goes on from where it waits, on the thread of the game, whatever thread an awaiter that is not
    // the engine's completes on.
    internal Action ResumeAction => _resume ??= () => Coroutines.ResumeFromAnywhere(this);

    internal void Finish(Exception? exception)
    {
        if (IsDone)
        {
            return;
        }
        IsDone = true;
        if (exception != null)
        {
            _failure = ExceptionDispatchInfo.Capture(exception);
            Coroutines.Failed(this);
        }
    }

    internal void ThrowIfFailed()
    {
        Observed = true;
        _failure?.Throw();
    }

    // The name of the async method, from the name the compiler gives its state machine: "<Blink>d__3".
    internal string Name
    {
        get
        {
            string name = Machine?.GetType().Name ?? "coroutine";
            int end = name.IndexOf('>');
            return name.StartsWith('<') && end > 1 ? name[1..end] : name;
        }
    }
}

/// <summary>What awaiting a coroutine gives.</summary>
public readonly struct CoroutineAwaiter : ICriticalNotifyCompletion, IEngineAwaiter
{
    private readonly Coroutine _coroutine;

    internal CoroutineAwaiter(Coroutine coroutine) => _coroutine = coroutine;

    public bool IsCompleted => _coroutine.IsDone;

    public void GetResult() => _coroutine.ThrowIfFailed();

    CoroutineWait IEngineAwaiter.Wait => new(CoroutineWaitKind.Coroutine) { Coroutine = _coroutine };

    public void OnCompleted(Action continuation)
        => Coroutines.Schedule(((IEngineAwaiter)this).Wait, null, continuation);

    public void UnsafeOnCompleted(Action continuation) => OnCompleted(continuation);
}

/// <summary>
/// The waits of coroutines, which any async method of the game may await too: it then goes on in the
/// Update of the frame the wait ends in.
/// </summary>
public static class Wait
{
    /// <summary>Seconds of game time.</summary>
    public static WaitAwaiter Seconds(float seconds) => new(new CoroutineWait(CoroutineWaitKind.Seconds) { Seconds = seconds });

    /// <summary>The next frame.</summary>
    public static WaitAwaiter NextFrame() => new(new CoroutineWait(CoroutineWaitKind.NextFrame));

    /// <summary>Until the condition holds, asked once per frame.</summary>
    public static WaitAwaiter Until(Func<bool> condition)
        => new(new CoroutineWait(CoroutineWaitKind.Until) { Condition = condition });

    /// <summary>As long as the condition holds, asked once per frame.</summary>
    public static WaitAwaiter While(Func<bool> condition)
        => new(new CoroutineWait(CoroutineWaitKind.While) { Condition = condition });
}

/// <summary>What awaiting a wait or a tween gives.</summary>
public readonly struct WaitAwaiter : ICriticalNotifyCompletion, IEngineAwaiter
{
    private readonly CoroutineWait _wait;

    internal WaitAwaiter(CoroutineWait wait) => _wait = wait;

    CoroutineWait IEngineAwaiter.Wait => _wait;

    public WaitAwaiter GetAwaiter() => this;

    public bool IsCompleted => Coroutines.IsOver(_wait);

    public void GetResult()
    {
    }

    public void OnCompleted(Action continuation) => Coroutines.Schedule(_wait, null, continuation);

    public void UnsafeOnCompleted(Action continuation) => OnCompleted(continuation);
}

internal enum CoroutineWaitKind
{
    NextFrame,
    Seconds,
    Until,
    While,
    Tween,
    Coroutine,
}

internal struct CoroutineWait(CoroutineWaitKind kind)
{
    public CoroutineWaitKind Kind = kind;
    public float Seconds;
    public Func<bool>? Condition;
    public ulong Tween;
    public Coroutine? Coroutine;
}

// The awaiters the scheduler waits for itself, rather than through a callback.
internal interface IEngineAwaiter
{
    CoroutineWait Wait { get; }
}

/// <summary>What the compiler uses to run async methods that return a Coroutine.</summary>
[EditorBrowsable(EditorBrowsableState.Never)]
public struct CoroutineMethodBuilder
{
    private Coroutine? _coroutine;

    public static CoroutineMethodBuilder Create() => new() { _coroutine = new Coroutine(Coroutines.OwnerOfNew) };

    public Coroutine Task => _coroutine!;

    // Runs at once, until the first wait.
    public void Start<TStateMachine>(ref TStateMachine stateMachine) where TStateMachine : IAsyncStateMachine
    {
        Coroutine? previous = Coroutines.Enter(_coroutine!);
        try
        {
            stateMachine.MoveNext();
        }
        finally
        {
            Coroutines.Leave(previous);
        }
    }

    public void SetStateMachine(IAsyncStateMachine stateMachine) => _coroutine!.Machine ??= stateMachine;

    public void SetResult() => _coroutine!.Finish(null);

    public void SetException(Exception exception) => _coroutine!.Finish(exception);

    public void AwaitOnCompleted<TAwaiter, TStateMachine>(ref TAwaiter awaiter, ref TStateMachine stateMachine)
        where TAwaiter : INotifyCompletion where TStateMachine : IAsyncStateMachine
    {
        Coroutine coroutine = Prepare(ref stateMachine);
        if (awaiter is IEngineAwaiter engine)
        {
            Coroutines.Schedule(engine.Wait, coroutine, null);
        }
        else
        {
            awaiter.OnCompleted(coroutine.ResumeAction);
        }
    }

    public void AwaitUnsafeOnCompleted<TAwaiter, TStateMachine>(ref TAwaiter awaiter, ref TStateMachine stateMachine)
        where TAwaiter : ICriticalNotifyCompletion where TStateMachine : IAsyncStateMachine
    {
        Coroutine coroutine = Prepare(ref stateMachine);
        if (awaiter is IEngineAwaiter engine)
        {
            Coroutines.Schedule(engine.Wait, coroutine, null);
        }
        else
        {
            awaiter.UnsafeOnCompleted(coroutine.ResumeAction);
        }
    }

    private readonly Coroutine Prepare<TStateMachine>(ref TStateMachine stateMachine) where TStateMachine : IAsyncStateMachine
    {
        // The first wait moves the method to the heap, with its state, where the next ones find it.
        _coroutine!.Machine ??= stateMachine;
        return _coroutine;
    }
}

/// <summary>
/// Resumes the coroutines of the game, and the async methods that await its waits, during Update. The
/// runtime installs it as the synchronization context of the game, so that an await of a Task goes
/// on in the game rather than on another thread.
/// </summary>
internal static unsafe class Coroutines
{
    private sealed class Entry
    {
        public CoroutineWait Wait;
        // A coroutine goes on through its method; another async method through its continuation.
        public Coroutine? Coroutine;
        public Action? Continuation;
        public Component? Owner;
    }

    private sealed class GameSynchronizationContext : SynchronizationContext
    {
        public override void Post(SendOrPostCallback callback, object? state) => Coroutines.Post(() => callback(state));

        public override SynchronizationContext CreateCopy() => this;
    }

    private static List<Entry> _waiting = [];
    private static List<Entry> _resuming = [];
    private static readonly Queue<Action> Posted = [];
    // Failures reported unless something awaits them before the end of the next Update.
    private static readonly List<Coroutine> FailedNow = [];
    private static readonly List<Coroutine> FailedBefore = [];
    private static int _mainThread = -1;
    private static bool _updating;

    public static SynchronizationContext Context { get; } = new GameSynchronizationContext();

    /// <summary>The coroutine that runs, whose owner the coroutines it starts get.</summary>
    public static Coroutine? Current { get; private set; }

    /// <summary>The component whose method runs, which owns the coroutines it starts.</summary>
    public static Component? CurrentOwner { get; set; }

    public static Component? OwnerOfNew => Current != null ? Current.Owner : CurrentOwner;

    public static int Count => _waiting.Count;

    private static bool OnMainThread => System.Environment.CurrentManagedThreadId == _mainThread;

    /// <summary>The thread the game runs on, given at the start of each phase.</summary>
    public static void SetMainThread() => _mainThread = System.Environment.CurrentManagedThreadId;

    public static Coroutine? Enter(Coroutine coroutine)
    {
        Coroutine? previous = Current;
        Current = coroutine;
        return previous;
    }

    public static void Leave(Coroutine? previous) => Current = previous;

    public static void Schedule(CoroutineWait wait, Coroutine? coroutine, Action? continuation)
    {
        var entry = new Entry
        {
            Wait = wait,
            Coroutine = coroutine,
            Continuation = continuation,
            Owner = coroutine != null ? coroutine.Owner : OwnerOfNew,
        };
        if (OnMainThread)
        {
            _waiting.Add(entry);
        }
        else
        {
            // Awaited after an await that went on elsewhere, as ConfigureAwait(false) does.
            Post(() => _waiting.Add(entry));
        }
    }

    public static void Post(Action action)
    {
        lock (Posted)
        {
            Posted.Enqueue(action);
        }
    }

    public static void ResumeFromAnywhere(Coroutine coroutine)
    {
        if (OnMainThread && _updating)
        {
            Run(coroutine);
        }
        else
        {
            Post(() => Run(coroutine));
        }
    }

    public static void Failed(Coroutine coroutine) => FailedNow.Add(coroutine);

    /// <summary>Whether the wait is over now, without waiting for another frame.</summary>
    public static bool IsOver(in CoroutineWait wait)
    {
        return wait.Kind switch
        {
            CoroutineWaitKind.Seconds => wait.Seconds <= 0.0f,
            CoroutineWaitKind.Until => wait.Condition == null || wait.Condition(),
            CoroutineWaitKind.While => wait.Condition == null || !wait.Condition(),
            CoroutineWaitKind.Tween => wait.Tween == 0 || Bootstrap.Native.IsTweenPlaying(wait.Tween) == 0,
            CoroutineWaitKind.Coroutine => wait.Coroutine == null || wait.Coroutine.IsDone,
            _ => false,
        };
    }

    /// <summary>Once per frame, during Update, after the components and the systems.</summary>
    public static void Update(float delta)
    {
        _updating = true;
        try
        {
            RunPosted();
            // Those that start waiting now wait for the next frame.
            (_resuming, _waiting) = (_waiting, _resuming);
            foreach (Entry entry in _resuming)
            {
                if (entry.Coroutine is { IsDone: true })
                {
                    continue;
                }
                if (entry.Owner != null && !GameRuntime.IsLive(entry.Owner))
                {
                    entry.Coroutine?.Stop();
                    continue;
                }
                bool over;
                try
                {
                    if (entry.Wait.Kind == CoroutineWaitKind.Seconds)
                    {
                        entry.Wait.Seconds -= delta;
                    }
                    over = entry.Wait.Kind == CoroutineWaitKind.NextFrame || IsOver(entry.Wait);
                }
                catch (Exception exception)
                {
                    // A condition that fails fails the coroutine that waits for it.
                    if (entry.Coroutine != null)
                    {
                        entry.Coroutine.Finish(exception);
                    }
                    else
                    {
                        GameRuntime.Report("A condition awaited", "Wait.Until", exception);
                    }
                    continue;
                }
                if (!over)
                {
                    _waiting.Add(entry);
                }
                else if (entry.Coroutine != null)
                {
                    Run(entry.Coroutine);
                }
                else if (entry.Continuation != null)
                {
                    RunContinuation(entry.Continuation, entry.Owner);
                }
            }
            _resuming.Clear();
            ReportFailures();
        }
        finally
        {
            _updating = false;
        }
    }

    /// <summary>Ends every coroutine, as a new scene, the end of the game and a reload of the code do.</summary>
    public static void Clear()
    {
        foreach (Entry entry in _waiting)
        {
            entry.Coroutine?.Stop();
        }
        _waiting.Clear();
        _resuming.Clear();
        lock (Posted)
        {
            Posted.Clear();
        }
        FailedBefore.AddRange(FailedNow);
        FailedNow.Clear();
        ReportFailures();
        FailedBefore.Clear();
        Current = null;
        CurrentOwner = null;
    }

    private static void Run(Coroutine coroutine)
    {
        if (coroutine.IsDone || coroutine.Machine == null)
        {
            return;
        }
        Coroutine? previous = Enter(coroutine);
        try
        {
            coroutine.Machine.MoveNext();
        }
        finally
        {
            Leave(previous);
        }
    }

    private static void RunContinuation(Action continuation, Component? owner)
    {
        Coroutine? previous = Current;
        Component? previousOwner = CurrentOwner;
        Current = null;
        CurrentOwner = owner;
        try
        {
            continuation();
        }
        catch (Exception exception)
        {
            GameRuntime.Report("An async method", "async", exception);
        }
        finally
        {
            Current = previous;
            CurrentOwner = previousOwner;
        }
    }

    private static void RunPosted()
    {
        int count;
        lock (Posted)
        {
            count = Posted.Count;
        }
        // What those posted post waits for the next frame.
        for (int index = 0; index < count; ++index)
        {
            Action? action;
            lock (Posted)
            {
                if (!Posted.TryDequeue(out action))
                {
                    return;
                }
            }
            RunContinuation(action, null);
        }
    }

    private static void ReportFailures()
    {
        foreach (Coroutine coroutine in FailedBefore)
        {
            if (!coroutine.Observed && coroutine.Failure != null)
            {
                string name = coroutine.Name;
                Component? owner = coroutine.Owner;
                string where = owner != null && GameRuntime.InPhase && GameRuntime.IsLive(owner)
                    ? $"The coroutine {owner.GetType().Name}.{name} of '{owner.Entity.Name}'"
                    : $"The coroutine {name}";
                GameRuntime.Report(where, $"coroutine {name}", coroutine.Failure);
            }
        }
        FailedBefore.Clear();
        FailedBefore.AddRange(FailedNow);
        FailedNow.Clear();
    }
}
