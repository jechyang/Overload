using Overload;
using Overload.Maths;
using Overload.UI;

[assembly: EnableUI]

namespace ScriptUI;

// This is ordinary project C# code. Changing this layout needs no C++ rebuild.
public class CustomHorizontalLayout : HorizontalLayoutGroup
{
    public float extraGap = 5;
    public int horizontalPasses;

    public CustomHorizontalLayout()
    {
        padding = new RectOffset(10, 10, 10, 10);
        spacing = 10;
        childForceExpandWidth = false;
        childForceExpandHeight = false;
    }

    public override void SetLayoutHorizontal()
    {
        base.SetLayoutHorizontal();
        if (rectChildren.Count > 1)
            rectChildren[1].anchoredPosition += new Vector2(extraGap, 0);
        ++horizontalPasses;
    }
}
