using Devex;

public sealed class AutoloadProbe : Component
{
    public int Value;

    public override void Start()
    {
        AutoloadProbe? state = Globals.Find("State").Get<AutoloadProbe>();
        Value = state == null ? -1 : state.Value + 1;
    }
}
