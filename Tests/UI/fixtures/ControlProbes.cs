using Overload.EventSystems;
using Overload.Maths;
using Overload.UI;

[assembly: EnableUI]

namespace ControlFixture;

public class ProbeImage : Image
{
    public ProbeImage() { color = new Color(1, 1, 1, 1); }
}

public class TintButton : Button
{
    public int clicks;
    public override void OnEnable()
    {
        base.OnEnable();
        targetGraphic = Owner.GetComponent<Graphic>();
    }
    public override void OnPointerClick(PointerEventData eventData)
    {
        base.OnPointerClick(eventData);
        ++clicks;
    }
}

public class CounterToggle : Toggle
{
    public int flips;
    public override void OnEnable()
    {
        base.OnEnable();
        targetGraphic = Owner.GetComponent<Graphic>();
    }
    public override void OnPointerClick(PointerEventData eventData)
    {
        base.OnPointerClick(eventData);
        ++flips;
    }
}

public class ValueSlider : Slider
{
    public void SetNorm(float v) => normalizedValue = v;
}

public class ValueScroll : Scrollbar
{
    public void SetVal(float v) => value = v;
}

public class ScrollProbe : ScrollRect
{
    public void SetNorm(float x, float y)
    {
        horizontalNormalizedPosition = x;
        verticalNormalizedPosition = y;
    }
}
