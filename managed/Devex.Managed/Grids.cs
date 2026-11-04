using System.Runtime.InteropServices;

namespace Devex;

/// <summary>A cell of a GridMap.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct GridCoordinates(int x, int y, int z)
{
    public int X = x;
    public int Y = y;
    public int Z = z;

    public override readonly string ToString() => $"({X}, {Y}, {Z})";
}

/// <summary>What a game does to the GridMap component of an entity while it runs, for a game where the player builds.</summary>
public static unsafe class Grids
{
    /// <summary>Puts a piece of the palette in a cell, replacing what was there, turned by quarters around Y.</summary>
    public static void Set(Entity grid, GridCoordinates cell, int piece, int rotation = 0)
        => Bootstrap.Native.GridSet(Scene.Current.Pointer, grid, &cell, piece, rotation);

    /// <summary>Empties a cell; false when it was empty.</summary>
    public static bool Remove(Entity grid, GridCoordinates cell) => Bootstrap.Native.GridRemove(Scene.Current.Pointer, grid, &cell) != 0;

    /// <summary>The piece in a cell, -1 when it is empty.</summary>
    public static int PieceAt(Entity grid, GridCoordinates cell)
    {
        int piece;
        int rotation;
        Bootstrap.Native.GridGet(Scene.Current.Pointer, grid, &cell, &piece, &rotation);
        return piece;
    }

    /// <summary>The cell a position of the world is in.</summary>
    public static GridCoordinates CellAt(Entity grid, Vec3 position)
    {
        GridCoordinates cell;
        Bootstrap.Native.GridCellAt(Scene.Current.Pointer, grid, &position, &cell);
        return cell;
    }

    /// <summary>Where a piece of a cell stands, in the world: the middle of its cell, at the bottom.</summary>
    public static Vec3 PositionOf(Entity grid, GridCoordinates cell)
    {
        Vec3 position;
        Bootstrap.Native.GridPositionOf(Scene.Current.Pointer, grid, &cell, &position);
        return position;
    }
}
