using Overload;
using Overload.UI;

namespace UIFirstFrame;

public class GameplayOnly : Behaviour
{
    public int awakes;
    public override void OnAwake() => ++awakes;
}

public class LayoutProbe : LayoutElement
{
    public int awakes;
    public override void OnAwake() => ++awakes;
}

// Interface-based layouts must be prepared too, without inheriting UIBehaviour.
public class InterfaceProbe : Behaviour, ILayoutElement
{
    public int measurements;
    public void CalculateLayoutInputHorizontal() => ++measurements;
    public void CalculateLayoutInputVertical() { }
    public float minWidth => 30;
    public float preferredWidth => 50;
    public float flexibleWidth => 1;
    public float minHeight => 20;
    public float preferredHeight => 20;
    public float flexibleHeight => 0;
    public int layoutPriority => 10;
}
