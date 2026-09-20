using Overload;
using Overload.UI;
using Overload.EventSystems;
using Overload.Maths;
using System.Collections.Generic;

[assembly: EnableUI]
namespace FInputFixture;

public sealed class SystemComponent : EventSystem { }
public sealed class InputComponent : BaseInput { }
public sealed class ModuleComponent : StandaloneInputModule { }
public sealed class GraphicRaycasterComponent : GraphicRaycaster
{
    public override void OnUpdate(float deltaTime) { if (deltaTime == 2) blockingObjects = BlockingObjects.All; if (deltaTime == 3) blockingObjects = BlockingObjects.None; }
}
public sealed class PhysicsRaycasterComponent : PhysicsRaycaster { }
public sealed class Physics2DRaycasterComponent : Physics2DRaycaster { }
public sealed class Collider2DComponent : UIPhysicsCollider2D { }
public sealed class ClipComponent : RectMask2D { }
public sealed class GroupComponent : CanvasGroup { }
public sealed class ImageComponent : Image
{
    public override void OnEnable() { base.OnEnable(); color = Color.red; }
}
public sealed class GreenImageComponent : Image
{
    public override void OnEnable() { base.OnEnable(); color = Color.green; }
}
public sealed class MaskImageComponent : Image
{
    public override void OnEnable() { base.OnEnable(); raycastTarget = false; }
}
public sealed class MaskComponent : Mask
{
    public override void OnEnable() { base.OnEnable(); showMaskGraphic = false; }
}
public sealed class RaycasterComponent : BaseRaycaster
{
    public override void Raycast(PointerEventData data,List<RaycastResult> results) {
        var target = Owner.Find("Target");
        if (target != null && data.position.x >= 0 && data.position.x < 100 && data.position.y >= 0 && data.position.y < 100)
            results.Add(new() { actor = target,module = this,depth = 1,screenPosition = data.position });
    }
}
public sealed class TargetComponent : Behaviour, IPointerDownHandler, IPointerUpHandler, IPointerClickHandler,
    IInitializePotentialDragHandler, IBeginDragHandler, IDragHandler, IEndDragHandler, IPointerEnterHandler, IPointerExitHandler
{
    [SerializeField] public int downs, ups, clicks, enters, exits, begins, drags, ends, initialized;
    public void OnPointerDown(PointerEventData e) { ++downs; }
    public void OnPointerUp(PointerEventData e) { ++ups; }
    public void OnPointerClick(PointerEventData e) { ++clicks; }
    public void OnInitializePotentialDrag(PointerEventData e) { ++initialized; }
    public void OnBeginDrag(PointerEventData e) { ++begins; }
    public void OnDrag(PointerEventData e) { ++drags; }
    public void OnEndDrag(PointerEventData e) { ++ends; }
    public void OnPointerEnter(PointerEventData e) { ++enters; }
    public void OnPointerExit(PointerEventData e) { ++exits; }
}
