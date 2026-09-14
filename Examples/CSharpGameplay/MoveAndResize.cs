using Overload;
using Overload.Maths;

namespace Gameplay;

public sealed class MoveAndResize : Behaviour
{
    private readonly float speed = 2;
    public override void OnUpdate(float deltaTime)
    {
        var position = Transform.Position;
        Transform.Position = new Vector3(position.X + speed * deltaTime, position.Y, position.Z);
        if (Owner.GetComponent<Box>() is { } box)
        {
            var size = box.Size;
            box.Size = new Vector3(size.X + deltaTime, size.Y, size.Z);
        }
    }
}
