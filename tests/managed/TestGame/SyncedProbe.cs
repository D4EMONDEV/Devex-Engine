using Devex;

/// <summary>Changes fields that a NetworkSync lists, as gameplay would, for the others to follow.</summary>
public sealed class SyncedProbe : Component
{
    public float Score;
    public string NameGiven = "";

    public override void Update(float delta)
    {
        Score += 1;
        NameGiven = "du C#";
    }
}
