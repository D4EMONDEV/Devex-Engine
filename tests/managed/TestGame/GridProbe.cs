using Devex;

/// <summary>Builds on a grid while it runs, and reads what it did.</summary>
public sealed class GridProbe : Component
{
    public float Placed;
    public float Empty;
    public float CellX;
    public float PositionX;
    public float Removed;
    public float Gone;

    private bool _done;

    public override void Update(float delta)
    {
        if (_done)
        {
            return;
        }
        _done = true;
        var cell = new GridCoordinates(1, 0, 2);
        Grids.Set(Entity, cell, 3, 1);
        Placed = Grids.PieceAt(Entity, cell);
        Empty = Grids.PieceAt(Entity, new GridCoordinates(5, 5, 5));
        CellX = Grids.CellAt(Entity, new Vec3(14.9f, 0.0f, 0.0f)).X;
        PositionX = Grids.PositionOf(Entity, cell).X;
        Grids.Set(Entity, new GridCoordinates(7, 0, 0), 1);
        Removed = Grids.Remove(Entity, new GridCoordinates(7, 0, 0)) ? 1 : 0;
        Gone = Grids.Remove(Entity, new GridCoordinates(7, 0, 0)) ? 1 : 0;
    }
}
