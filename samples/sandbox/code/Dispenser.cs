using Devex;

// Drops a prefab at each of its points in turn, and removes the oldest drops beyond a count.
public class Dispenser : Component
{
    [AssetType("scene")]
    public AssetId Prefab;

    // Where the drops appear, in the world.
    public List<Vec3> Points = [];

    // Seconds between two drops.
    public float Interval = 2.0f;

    public int MaxDrops = 6;

    private float _timer;
    private int _next;
    private readonly Queue<Entity> _drops = new();

    public override void Update(float delta)
    {
        _timer += delta;
        if (_timer < Interval || !Prefab.IsValid || Points.Count == 0)
        {
            return;
        }
        _timer = 0.0f;
        Entity drop = Prefabs.Instantiate(Prefab, Points[_next++ % Points.Count]);
        if (drop.IsValid)
        {
            _drops.Enqueue(drop);
        }
        while (_drops.Count > MaxDrops)
        {
            Entity oldest = _drops.Dequeue();
            if (oldest.IsAlive)
            {
                oldest.Destroy();
            }
        }
    }
}
