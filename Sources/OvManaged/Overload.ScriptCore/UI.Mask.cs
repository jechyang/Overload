using System;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

/// <summary>Immutable 8-bit stencil parameters selected in managed code.</summary>
public readonly record struct UIStencilState(bool enabled,byte reference,byte readMask,byte writeMask,bool replace,bool colorWrite)
{
    public static UIStencilState Disabled => new(false,0,255,0,false,true);
    internal ulong Packed => (enabled?1ul:0ul)|(replace?2ul:0ul)|(colorWrite?4ul:0ul)|((ulong)reference<<8)|((ulong)readMask<<16)|((ulong)writeMask<<24);
}
public static class StencilMaterial
{
    public static UIStencilState ForDepth(int depth) {
        if((uint)depth>8)throw new ArgumentOutOfRangeException(nameof(depth),"UI supports eight stencil levels");
        return depth==0?UIStencilState.Disabled:new(true,(byte)((1<<depth)-1),(byte)((1<<depth)-1),0,false,true);
    }
    public static UIStencilState ForMask(int depth,bool show) {
        if((uint)depth>=8)throw new InvalidOperationException("UI stencil nesting exceeds eight levels");
        return new(true,(byte)((1<<(depth+1))-1),(byte)((1<<depth)-1),(byte)(1<<depth),true,show);
    }
    public static UIStencilState ForPop(int depth) {
        if((uint)depth>=8)throw new ArgumentOutOfRangeException(nameof(depth));
        return new(true,(byte)((1<<depth)-1),(byte)((1<<depth)-1),(byte)(1<<depth),true,false);
    }
}
public static class MaskUtilities
{
    public static int GetStencilDepth(Actor actor) {
        int depth=0;
        if(actor.GetComponent<Canvas>() is Canvas own && own.overrideSorting)return 0;
        for(Actor? a=actor.Parent;a!=null;a=a.Parent){
            foreach(var mask in a.GetComponents<Mask>())if(mask.MaskEnabled())++depth;
            if(a.GetComponent<Canvas>() is Canvas c && c.overrideSorting)break;
        }
        return depth;
    }
    public static void NotifyStencilStateChanged(Actor actor) {
        foreach(var graphic in UISessionRegistry<Graphic>.For(actor).Snapshot())if(graphic.owner!=null && graphic.Owner.IsAlive && graphic.IsActive())graphic.SetMaterialDirty();
    }
    public static void Notify2DMaskStateChanged(Actor actor) => ClipperRegistry.Cull(actor);
    internal static void ApplyStencil(Graphic graphic) {
        int depth=graphic is MaskableGraphic mg && !mg.maskable?0:GetStencilDepth(graphic.Owner);
        var mask=graphic.Owner.GetComponent<Mask>();
        if(mask!=null && mask.MaskEnabled()) {
            graphic.canvasRenderer.SetStencil(StencilMaterial.ForMask(depth,mask.showMaskGraphic));
            graphic.canvasRenderer.SetPopStencil(StencilMaterial.ForPop(depth));
        } else {
            graphic.canvasRenderer.SetStencil(StencilMaterial.ForDepth(depth));
            graphic.canvasRenderer.SetPopStencil(UIStencilState.Disabled);
        }
    }
}
public class Mask : UIBehaviour,IMask,IMaterialModifier,ICanvasRaycastFilter,IGraphicEnabledDisabled
{
    [SerializeField] private bool m_ShowMaskGraphic=true;
    public bool showMaskGraphic {get=>m_ShowMaskGraphic;set{if(value==m_ShowMaskGraphic)return;m_ShowMaskGraphic=value;SetDirty();}}
    public Graphic? graphic=>owner==null?null:Owner.GetComponent<Graphic>();
    public bool MaskEnabled()=>IsActive() && graphic?.IsActive()==true;
    protected override void SetDirty(){if(owner!=null && Owner.IsAlive)MaskUtilities.NotifyStencilStateChanged(Owner);}
    internal override void ReleaseNativeComponents()=>SetDirty();
    public override void OnEnable(){base.OnEnable();SetDirty();}
    public override void OnDisable(){SetDirty();base.OnDisable();}
    public override void OnDestroy(){SetDirty();base.OnDestroy();}
    public override void OnReloadRelease(){OnDestroy();base.OnReloadRelease();}
    public void OnSiblingGraphicEnabledDisabled()=>SetDirty();
    public AssetRef GetModifiedMaterial(AssetRef baseMaterial){if(graphic is Graphic g)MaskUtilities.ApplyStencil(g);return baseMaterial;}
    public bool IsRaycastLocationValid(Vector2 point,Actor? eventCamera)=>!MaskEnabled()||rectTransform.rect.Contains(point);
}
