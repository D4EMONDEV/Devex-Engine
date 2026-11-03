using Devex;

public static class EditorTools
{
    [EditorCommand("devex.test-tools", "Move selection and create")]
    public static void Move(EditorContext context)
    {
        foreach (Entity entity in context.Selection)
        {
            entity.Transform.Position.X += 2;
            if (entity.Get<Mover>() is Mover mover)
                mover.Speed = 9;
        }
        context.CreateEntity("Created by addon");
    }

    [EditorCommand("devex.test-tools", "Failure \"with quotes\"")]
    public static void Fail(EditorContext context)
    {
        context.CreateEntity("Discarded");
        throw new InvalidOperationException("An editor command failed");
    }
}
