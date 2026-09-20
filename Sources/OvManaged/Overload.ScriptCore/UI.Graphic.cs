using System;
using System.Collections.Generic;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public readonly record struct Color(float r, float g, float b, float a)
{
    public static Color white => new(1, 1, 1, 1);
    public static Color black => new(0, 0, 0, 1);
    public static Color red => new(1, 0, 0, 1);
    public static Color green => new(0, 1, 0, 1);
    public static Color blue => new(0, 0, 1, 1);
    public static implicit operator Color32(Color value) => new(
        Byte(value.r), Byte(value.g), Byte(value.b), Byte(value.a));
    private static byte Byte(float value) => (byte)Math.Clamp((int)MathF.Round(value * 255), 0, 255);
}

public readonly record struct Color32(byte r, byte g, byte b, byte a)
{
    public static implicit operator Color(Color32 value) => new(value.r / 255f, value.g / 255f, value.b / 255f, value.a / 255f);
}

public abstract class Graphic : UIBehaviour, ICanvasElement
{
    [SerializeField] private float m_ColorR = 1, m_ColorG = 1, m_ColorB = 1, m_ColorA = 1;
    [SerializeField] private bool m_RaycastTarget = true;
    [SerializeField] private AssetRef m_Material = new(AssetKind.Unknown, "");
    [NonSerialized] private bool verticesDirty = true, materialDirty = true;
    [NonSerialized] private CanvasRenderer? cachedRenderer;
    [NonSerialized] private IDisposable? registration;
    [NonSerialized] private Action? dirtyLayout, dirtyVertices, dirtyMaterial;
    [NonSerialized] private bool tweenActive, tweenIgnoreTimeScale, tweenRGB, tweenAlpha;
    [NonSerialized] private float tweenElapsed, tweenDuration;
    [NonSerialized] private Color tweenStart, tweenTarget;
    public Canvas? canvas { get { for(Actor? a=owner;a!=null;a=a.Parent)if(a.GetComponent<Canvas>() is Canvas c)return c;return null; } }
    public int depth => canvasRenderer.absoluteDepth;
    public virtual float pixelsPerUnit => 1;
    public static AssetRef defaultGraphicMaterial => new(AssetKind.Unknown, "");
    public virtual AssetRef defaultMaterial => defaultGraphicMaterial;
    public virtual AssetRef materialForRendering {
        get { var result=material; foreach(var modifier in Owner.GetComponents<IMaterialModifier>())if(LayoutUtility.Active(modifier))result=modifier.GetModifiedMaterial(result); return result; }
    }

    public virtual Color color {
        get => new(m_ColorR, m_ColorG, m_ColorB, m_ColorA);
        set { if(!float.IsFinite(value.r)||!float.IsFinite(value.g)||!float.IsFinite(value.b)||!float.IsFinite(value.a))throw new ArgumentException("Color must be finite"); if (value == color) return; m_ColorR = value.r; m_ColorG = value.g; m_ColorB = value.b; m_ColorA = value.a; SetVerticesDirty(); }
    }
    public virtual bool raycastTarget { get => m_RaycastTarget; set { if (m_RaycastTarget == value) return; m_RaycastTarget = value; SetMaterialDirty(); } }
    public virtual AssetRef material {
        get => string.IsNullOrEmpty(m_Material.Path) ? defaultMaterial : m_Material;
        set { if (value == m_Material) return; if (value.Kind is not (AssetKind.Unknown or AssetKind.Material)) throw new ArgumentException("Graphic material must be a Material AssetRef"); m_Material = value; SetMaterialDirty(); }
    }
    public virtual AssetRef mainTexture => new(AssetKind.Unknown, "");
    public CanvasRenderer canvasRenderer => cachedRenderer ??= Owner.GetComponent<CanvasRenderer>() ?? throw new InvalidOperationException("Graphic requires CanvasRenderer");
    RectTransform ICanvasElement.transform => rectTransform;

    internal override void EnsureNativeComponents() { base.EnsureNativeComponents(); cachedRenderer = Owner.AddComponent<CanvasRenderer>(); if(IsActive())registration ??= GraphicRegistry.Register(this); }
    internal override void ReleaseNativeComponents() {
        try { (cachedRenderer ?? Owner.GetComponent<CanvasRenderer>())?.Clear(); } catch { }
        cachedRenderer = null; registration?.Dispose(); registration=null; tweenActive=false;
    }
    protected override void SetDirty() => SetAllDirty();
    protected override void OnRectTransformDimensionsChange() { SetVerticesDirty(); SetLayoutDirty(); }
    protected override void OnTransformParentChanged() { base.OnTransformParentChanged(); SetAllDirty(); }
    protected override void OnCanvasHierarchyChanged() { base.OnCanvasHierarchyChanged(); SetAllDirty(); }
    public override void OnEnable() { base.OnEnable(); registration ??= GraphicRegistry.Register(this); NotifyGraphicState(); SetAllDirty(); }
    public override void OnDisable() { CanvasUpdateRegistry.UnRegisterCanvasElementForRebuild(this); ReleaseNativeComponents(); NotifyGraphicState(); base.OnDisable(); }
    public override void OnDestroy() { CanvasUpdateRegistry.UnRegisterCanvasElementForRebuild(this); ReleaseNativeComponents(); NotifyGraphicState(); base.OnDestroy(); }
    public override void OnReloadRelease() { CanvasUpdateRegistry.UnRegisterCanvasElementForRebuild(this); ReleaseNativeComponents(); NotifyGraphicState(); base.OnReloadRelease(); }

    public virtual void OnRebuildRequested() => SetAllDirty();
    public virtual void SetAllDirty() { SetLayoutDirty(); SetVerticesDirty(); SetMaterialDirty(); }
    public virtual void SetLayoutDirty() { if (IsActive()) { LayoutRebuilder.MarkLayoutForRebuild(rectTransform); dirtyLayout?.Invoke(); } }
    public virtual void SetVerticesDirty() {
        if (!IsActive()) return; verticesDirty = true; CanvasUpdateRegistry.RegisterCanvasElementForGraphicRebuild(this); dirtyVertices?.Invoke();
    }
    public virtual void SetMaterialDirty() {
        if (!IsActive()) return; materialDirty = true; CanvasUpdateRegistry.RegisterCanvasElementForGraphicRebuild(this); dirtyMaterial?.Invoke();
    }
    public virtual void Rebuild(CanvasUpdate executing) {
        if (executing != CanvasUpdate.PreRender || !IsActive() || (!verticesDirty && !materialDirty)) return;
        if (canvasRenderer.cull) return;
        if(verticesDirty) {
            verticesDirty=false;
            try { UpdateGeometry(); } catch { verticesDirty=true; throw; }
        }
        if(materialDirty) {
            materialDirty=false;
            try { UpdateMaterial(); } catch { materialDirty=true; throw; }
        }
    }
    protected virtual void UpdateGeometry() {
        using VertexHelper helper = new();
        if(rectTransform.rect.width >= 0 && rectTransform.rect.height >= 0) OnPopulateMesh(helper);
        foreach(var modifier in Owner.GetComponents<IMeshModifier>()) if(LayoutUtility.Active(modifier))modifier.ModifyMesh(helper);
        canvasRenderer.SetMesh(helper.Vertices(),helper.Indices());
    }
    protected virtual void UpdateMaterial() {
        canvasRenderer.materialCount=1;
        canvasRenderer.SetStencil(UIStencilState.Disabled);canvasRenderer.SetPopStencil(UIStencilState.Disabled);
        canvasRenderer.SetTexture(mainTexture); canvasRenderer.SetMaterial(materialForRendering); canvasRenderer.SetRaycastTarget(raycastTarget);
    }
    private void NotifyGraphicState() {
        if(owner==null)return;
        foreach(var listener in Owner.GetComponents<IGraphicEnabledDisabled>())listener.OnSiblingGraphicEnabledDisabled();
    }
    public virtual void OnCullingChanged() {
        if(!canvasRenderer.cull && (verticesDirty||materialDirty))CanvasUpdateRegistry.RegisterCanvasElementForGraphicRebuild(this);
    }
    public void RegisterDirtyLayoutCallback(Action callback) => dirtyLayout+=callback;
    public void UnregisterDirtyLayoutCallback(Action callback) => dirtyLayout-=callback;
    public void RegisterDirtyVerticesCallback(Action callback) => dirtyVertices+=callback;
    public void UnregisterDirtyVerticesCallback(Action callback) => dirtyVertices-=callback;
    public void RegisterDirtyMaterialCallback(Action callback) => dirtyMaterial+=callback;
    public void UnregisterDirtyMaterialCallback(Action callback) => dirtyMaterial-=callback;
    /// <summary>Tests a point in this Graphic's local RectTransform coordinates after the raycaster projects its screen ray.</summary>
    public virtual bool Raycast(Vector2 localPoint, Actor? eventCamera=null) {
        if(!IsActive() || !raycastTarget || canvasRenderer.cull || !rectTransform.rect.Contains(localPoint))return false;
        var canvasPoint=Clipping.LocalToCanvas(rectTransform,localPoint);bool ignoreGroups=false;
        for(Actor? a=Owner;a!=null;a=a.Parent) {
            var rect=a.GetComponent<RectTransform>();var point=rect==null?localPoint:canvasPoint-Clipping.LocalToCanvas(rect,Vector2.zero);
            foreach(var filter in a.GetComponents<ICanvasRaycastFilter>()) {
                if(!LayoutUtility.Active(filter))continue;
                if(filter is CanvasGroup group){if(ignoreGroups)continue;if(!group.IsRaycastLocationValid(point,eventCamera))return false;if(group.ignoreParentGroups)ignoreGroups=true;}
                else if(!filter.IsRaycastLocationValid(point,eventCamera))return false;
            }
            if(a.GetComponent<Canvas>() is Canvas c && c.overrideSorting)break;
        }
        return true;
    }
    public virtual void CrossFadeColor(Color targetColor,float duration,bool ignoreTimeScale,bool useAlpha) => CrossFadeColor(targetColor,duration,ignoreTimeScale,useAlpha,true);
    public virtual void CrossFadeColor(Color targetColor,float duration,bool ignoreTimeScale,bool useAlpha,bool useRGB) {
        if(!float.IsFinite(duration)||duration<0)throw new ArgumentOutOfRangeException(nameof(duration));
        if(!float.IsFinite(targetColor.r)||!float.IsFinite(targetColor.g)||!float.IsFinite(targetColor.b)||!float.IsFinite(targetColor.a))throw new ArgumentException("Color must be finite");
        tweenStart=canvasRenderer.GetColor();tweenTarget=targetColor;tweenDuration=duration;tweenElapsed=0;
        tweenRGB=useRGB;tweenAlpha=useAlpha;tweenIgnoreTimeScale=ignoreTimeScale;tweenActive=true;
        if(duration==0||!IsActive())AdvanceTween(duration,duration);
    }
    public virtual void CrossFadeAlpha(float alpha,float duration,bool ignoreTimeScale) => CrossFadeColor(new(1,1,1,alpha),duration,ignoreTimeScale,true,false);
    internal void AdvanceTween(float delta,float unscaled) {
        if(!tweenActive)return;
        tweenElapsed+=tweenIgnoreTimeScale?unscaled:delta;float t=tweenDuration==0?1:Math.Clamp(tweenElapsed/tweenDuration,0,1);
        float L(float a,float b)=>a+(b-a)*t;
        canvasRenderer.SetColor(new(tweenRGB?L(tweenStart.r,tweenTarget.r):tweenStart.r,tweenRGB?L(tweenStart.g,tweenTarget.g):tweenStart.g,
            tweenRGB?L(tweenStart.b,tweenTarget.b):tweenStart.b,tweenAlpha?L(tweenStart.a,tweenTarget.a):tweenStart.a));
        if(t>=1)tweenActive=false;
    }
    protected virtual void OnPopulateMesh(VertexHelper helper) {
        var rect = rectTransform.rect; Color32 tint = color;
        helper.Clear();
        helper.AddVert(new Vector3(rect.min.x, rect.min.y, 0), tint, new Vector2(0, 0));
        helper.AddVert(new Vector3(rect.min.x, rect.max.y, 0), tint, new Vector2(0, 1));
        helper.AddVert(new Vector3(rect.max.x, rect.max.y, 0), tint, new Vector2(1, 1));
        helper.AddVert(new Vector3(rect.max.x, rect.min.y, 0), tint, new Vector2(1, 0));
        helper.AddTriangle(0, 1, 2); helper.AddTriangle(2, 3, 0);
    }
    public virtual void SetNativeSize() { }
    public virtual void LayoutComplete() { }
    public virtual void GraphicUpdateComplete() { }
}
