namespace Devex;

/// <summary>A mouse button, as the engine numbers them.</summary>
public enum MouseButton
{
    Left = 0,
    Right = 1,
    Middle = 2,
}

/// <summary>Keyboard and mouse of the current frame. "Pressed" is true only on the frame of the press.</summary>
public static unsafe class Input
{
    public static bool IsKeyDown(Key key) => Bootstrap.Native.IsKeyDown((int)key) != 0;

    public static bool WasKeyPressed(Key key) => Bootstrap.Native.WasKeyPressed((int)key) != 0;

    public static bool IsMouseButtonDown(MouseButton button) => Bootstrap.Native.IsMouseButtonDown((int)button) != 0;

    public static bool WasMouseButtonPressed(MouseButton button)
        => Bootstrap.Native.WasMouseButtonPressed((int)button) != 0;

    /// <summary>How far the mouse moved since the previous frame, in pixels.</summary>
    public static Vec2 MouseDelta
    {
        get
        {
            float* values = stackalloc float[2];
            Bootstrap.Native.MouseDelta(values);
            return new Vec2(values[0], values[1]);
        }
    }

    /// <summary>Where the mouse is in the window, in pixels.</summary>
    public static Vec2 MousePosition
    {
        get
        {
            float* values = stackalloc float[2];
            Bootstrap.Native.MousePosition(values);
            return new Vec2(values[0], values[1]);
        }
    }

    /// <summary>A captured mouse is hidden and reports unbounded motion, as a first-person camera needs.</summary>
    public static bool MouseCaptured
    {
        get => Bootstrap.Native.IsMouseCaptured() != 0;
        set => Bootstrap.Native.SetMouseCaptured(value ? 1 : 0);
    }
}

