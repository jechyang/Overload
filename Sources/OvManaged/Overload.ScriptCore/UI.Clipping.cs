using System;
using System.Collections.Generic;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public interface IClipper { void PerformClipping(); }
public interface IClipRegion { Rect canvasRect { get; } }
public interface IClippable {
    RectTransform rectTransform { get; }
    void RecalculateClipping();
    void Cull(Rect clipRect, bool validRect);
    void SetClipRect(Rect value, bool validRect);
}
public interface IMask { bool MaskEnabled(); RectTransform rectTransform { get; } }
public interface IMaskable { void RecalculateMasking(); }

public static class Clipping
{
    public static Rect FindCullAndClipWorldRect(IReadOnlyList<RectMask2D> masks,out bool validRect) {
        validRect=false; Rect result=default;
        foreach(var mask in masks) if(mask.IsActive()) { result=validRect?Intersect(result,mask.canvasRect):mask.canvasRect;validRect=true; }
        validRect=validRect && result.width>0 && result.height>0;return result;
    }
    public static Rect Intersect(Rect a,Rect b) {
        var min=new Vector2(Math.Max(a.min.x,b.min.x),Math.Max(a.min.y,b.min.y));
        return new(min,new(Math.Max(0,Math.Min(a.max.x,b.max.x)-min.x),Math.Max(0,Math.Min(a.max.y,b.max.y)-min.y)));
    }
    // Coordinates relative to the root canvas's lower-left layout origin.
    public static Rect CanvasRect(RectTransform rect) {
        var chain=new List<RectTransform>();for(RectTransform? r=rect;r!=null;r=r.parent)chain.Add(r);
        Vector2 min=default,size=default;
        for(int i=chain.Count-1;i>=0;--i) {
            var r=chain[i];var next=r.rect.size;
            if(i==chain.Count-1) { min=default;size=next;continue; }
            var p=r.pivot;var low=r.anchorMin;var high=r.anchorMax;
            min+=new Vector2(size.x*(low.x+(high.x-low.x)*p.x)+r.anchoredPosition.x-next.x*p.x,
                size.y*(low.y+(high.y-low.y)*p.y)+r.anchoredPosition.y-next.y*p.y);size=next;
        }
        return new(min,size);
    }
    public static Vector2 LocalToCanvas(RectTransform rect,Vector2 point) {var r=CanvasRect(rect);return r.min+new Vector2(r.size.x*rect.pivot.x,r.size.y*rect.pivot.y)+point;}
    public static Rect CanvasToLocal(RectTransform rect,Rect value) => new(value.min-LocalToCanvas(rect,Vector2.zero),value.size);
}
public sealed class RectangularVertexClipper { public Rect GetCanvasRect(RectTransform rect,Canvas canvas) => Clipping.CanvasRect(rect); }

public static class ClipperRegistry
{
    internal static IDisposable Register(UIBehaviour clipper) => UISessionRegistry<IClipper>.For(clipper.Owner).Register((IClipper)clipper,clipper);
    public static void Cull(Actor anchor) {
        foreach(var item in UISessionRegistry<IClipper>.For(anchor).Snapshot()) if(LayoutUtility.Active(item) && item is Behaviour b && anchor.Access.Query(anchor.Handle,19,b.Owner.Handle)!=0)item.PerformClipping();
        // Geometry can move without a mesh rebuild; apply current ancestry before every snapshot.
        foreach(var graphic in UISessionRegistry<Graphic>.For(anchor).Snapshot()) if(graphic is MaskableGraphic maskable && graphic.IsActive() && anchor.Access.Query(anchor.Handle,19,graphic.Owner.Handle)!=0)maskable.RecalculateClipping();
    }
}

public class CanvasGroup : UIBehaviour, ICanvasRaycastFilter
{
    [SerializeField] private float m_Alpha=1;
    [SerializeField] private bool m_Interactable=true,m_BlocksRaycasts=true,m_IgnoreParentGroups;
    public float alpha {get=>m_Alpha;set{if(!float.IsFinite(value))throw new ArgumentException("Alpha must be finite");m_Alpha=Math.Clamp(value,0,1);SetDirty();}}
    public bool interactable {get=>m_Interactable;set{m_Interactable=value;SetDirty();}}
    public bool blocksRaycasts {get=>m_BlocksRaycasts;set{m_BlocksRaycasts=value;SetDirty();}}
    public bool ignoreParentGroups {get=>m_IgnoreParentGroups;set{m_IgnoreParentGroups=value;SetDirty();}}
    internal override void EnsureNativeComponents(){base.EnsureNativeComponents();Sync();}
    protected override void SetDirty(){if(owner!=null){Sync();foreach(var selectable in Owner.GetComponentsInChildren<Selectable>(true))selectable.RefreshCanvasGroups();}}
    private void Sync(){var native=Owner.AddComponent<NativeCanvasGroup>();native.Set(m_Alpha,m_Interactable,m_BlocksRaycasts,m_IgnoreParentGroups,IsActive());}
    internal override void ReleaseNativeComponents(){if(owner!=null && Owner.IsAlive)Owner.GetComponent<NativeCanvasGroup>()?.Clear();}
    public override void OnEnable(){base.OnEnable();Sync();}
    public override void OnDisable(){ReleaseNativeComponents();base.OnDisable();}
    public override void OnDestroy(){ReleaseNativeComponents();base.OnDestroy();}
    public override void OnReloadRelease(){ReleaseNativeComponents();base.OnReloadRelease();}
    public bool IsRaycastLocationValid(Vector2 point,Actor? eventCamera)=>!IsActive()||blocksRaycasts;
}

public class RectMask2D : UIBehaviour,IClipper,IClipRegion,ICanvasRaycastFilter
{
    private IDisposable? registration;
    private readonly List<WeakReference<IClippable>> targets=new();
    public Rect canvasRect=>Clipping.CanvasRect(rectTransform);
    internal override void EnsureNativeComponents(){base.EnsureNativeComponents();Owner.AddComponent<NativeRectMask>().enabled=IsActive();if(IsActive())registration??=ClipperRegistry.Register(this);}
    protected override void SetDirty(){if(owner!=null){Owner.AddComponent<NativeRectMask>().enabled=IsActive();PerformClipping();}}
    public override void OnEnable(){base.OnEnable();EnsureNativeComponents();PerformClipping();}
    internal override void ReleaseNativeComponents(){if(owner!=null && Owner.IsAlive && Owner.GetComponent<NativeRectMask>() is NativeRectMask mask)mask.enabled=false;registration?.Dispose();registration=null;}
    public override void OnDisable(){ReleaseNativeComponents();foreach(var t in targets.ToArray())if(t.TryGetTarget(out var value))value.RecalculateClipping();base.OnDisable();}
    public override void OnDestroy(){ReleaseNativeComponents();targets.Clear();base.OnDestroy();}
    public override void OnReloadRelease(){ReleaseNativeComponents();targets.Clear();base.OnReloadRelease();}
    public void AddClippable(IClippable target){ArgumentNullException.ThrowIfNull(target);foreach(var t in targets)if(t.TryGetTarget(out var value)&&ReferenceEquals(value,target))return;targets.Add(new(target));target.RecalculateClipping();}
    public void RemoveClippable(IClippable target){targets.RemoveAll(t=>!t.TryGetTarget(out var value)||ReferenceEquals(value,target));target.SetClipRect(default,false);}
    public void PerformClipping(){foreach(var t in targets.ToArray())if(t.TryGetTarget(out var value))value.RecalculateClipping();else targets.Remove(t);}
    public bool IsRaycastLocationValid(Vector2 point,Actor? eventCamera)=>!IsActive()||rectTransform.rect.Contains(point);
}

public class MaskableGraphic : Graphic,IClippable,IMaskable
{
    [SerializeField] private bool m_Maskable=true;
    public bool maskable {get=>m_Maskable;set{if(value==m_Maskable)return;m_Maskable=value;RecalculateMasking();RecalculateClipping();}}
    public event Action<bool>? onCullStateChanged;
    public void RecalculateClipping(){
        if(!IsActive())return;
        canvasRenderer.IgnoreParentRectMasks(!maskable);
        var masks=new List<RectMask2D>();
        if(maskable && !(Owner.GetComponent<Canvas>() is Canvas own && own.overrideSorting))for(Actor? a=Owner.Parent;a!=null;a=a.Parent){
            foreach(var mask in a.GetComponents<RectMask2D>())if(mask.IsActive())masks.Add(mask);
            if(a.GetComponent<Canvas>() is Canvas c && c.overrideSorting)break;
        }
        if(masks.Count==0){SetClipRect(default,false);SetCull(false);return;}
        Rect clip=Clipping.FindCullAndClipWorldRect(masks,out bool valid);SetClipRect(clip,true);Cull(clip,valid);
    }
    public virtual void RecalculateMasking(){SetMaterialDirty();}
    public virtual void SetClipRect(Rect value,bool validRect){if(validRect)canvasRenderer.EnableRectClipping(Clipping.CanvasToLocal(rectTransform,value));else canvasRenderer.DisableRectClipping();}
    public virtual void Cull(Rect clipRect,bool validRect){var intersection=Clipping.Intersect(Clipping.CanvasRect(rectTransform),clipRect);SetCull(!validRect||intersection.width<=0||intersection.height<=0);}
    private void SetCull(bool value){if(canvasRenderer.cull==value)return;canvasRenderer.cull=value;onCullStateChanged?.Invoke(value);OnCullingChanged();}
    protected override void UpdateMaterial(){base.UpdateMaterial();RecalculateClipping();MaskUtilities.ApplyStencil(this);}
    protected override void OnTransformParentChanged(){base.OnTransformParentChanged();RecalculateMasking();RecalculateClipping();}
    protected override void OnCanvasHierarchyChanged(){base.OnCanvasHierarchyChanged();RecalculateMasking();RecalculateClipping();}
}
