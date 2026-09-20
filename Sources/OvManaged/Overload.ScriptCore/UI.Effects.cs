using System;
using System.Collections.Generic;
using Overload.EventSystems;
using Overload.Maths;

namespace Overload.UI;

public interface IMaterialModifier { AssetRef GetModifiedMaterial(AssetRef baseMaterial); }
public interface IMeshModifier { void ModifyMesh(VertexHelper vertices); }
public interface IGraphicEnabledDisabled { void OnSiblingGraphicEnabledDisabled(); }

public abstract class BaseMeshEffect : UIBehaviour, IMeshModifier
{
    public Graphic? graphic => owner == null ? null : Owner.GetComponent<Graphic>();
    protected override void SetDirty() { if(owner!=null && Owner.IsAlive)graphic?.SetVerticesDirty(); }
    internal override void ReleaseNativeComponents()=>SetDirty();
    public override void OnEnable() { base.OnEnable(); SetDirty(); }
    public override void OnDisable() { SetDirty(); base.OnDisable(); }
    public override void OnDestroy() { SetDirty(); base.OnDestroy(); }
    public override void OnReloadRelease() { SetDirty(); base.OnReloadRelease(); }
    public abstract void ModifyMesh(VertexHelper vertices);
}

public class Shadow : BaseMeshEffect
{
    [SerializeField] private Color m_EffectColor = new(0,0,0,0.5f);
    [SerializeField] private Vector2 m_EffectDistance = new(1,-1);
    [SerializeField] private bool m_UseGraphicAlpha = true;
    public Color effectColor { get => m_EffectColor; set { if(value==m_EffectColor)return; m_EffectColor=value; SetDirty(); } }
    public Vector2 effectDistance { get => m_EffectDistance; set {
        if(!float.IsFinite(value.x)||!float.IsFinite(value.y))throw new ArgumentException("Effect distance must be finite");
        value=new(Math.Clamp(value.x,-600,600),Math.Clamp(value.y,-600,600));
        if(value==m_EffectDistance)return; m_EffectDistance=value;SetDirty();
    } }
    public bool useGraphicAlpha { get=>m_UseGraphicAlpha; set {if(value==m_UseGraphicAlpha)return;m_UseGraphicAlpha=value;SetDirty();} }
    protected void ApplyShadow(List<UIVertex> stream, int start, int end, float x, float y) {
        if(stream.Count+end-start>VertexHelper.MaximumVertices)throw new ArgumentException("Effect exceeds UI vertex limit");
        for(int i=start;i<end;++i) {
            var v=stream[i]; stream.Add(v);
            v.position=new(v.position.X+x,v.position.Y+y,v.position.Z);
            Color32 tint=effectColor;
            if(useGraphicAlpha)tint=new(tint.r,tint.g,tint.b,(byte)(tint.a*v.color.a/255));
            v.color=tint;stream[i]=v;
        }
    }
    public override void ModifyMesh(VertexHelper vertices) {
        if(!IsActive())return;
        var stream=new List<UIVertex>();vertices.GetUIVertexStream(stream);
        ApplyShadow(stream,0,stream.Count,effectDistance.x,effectDistance.y);
        vertices.Clear();vertices.AddUIVertexTriangleStream(stream);
    }
}
public class Outline : Shadow
{
    public override void ModifyMesh(VertexHelper vertices) {
        if(!IsActive())return;
        var stream=new List<UIVertex>();vertices.GetUIVertexStream(stream);
        if((long)stream.Count*5>VertexHelper.MaximumVertices)throw new ArgumentException("Outline exceeds UI vertex limit");
        int start=0,end=stream.Count;
        foreach(var d in new[]{new Vector2(effectDistance.x,effectDistance.y),new Vector2(effectDistance.x,-effectDistance.y),new Vector2(-effectDistance.x,effectDistance.y),new Vector2(-effectDistance.x,-effectDistance.y)}) {
            ApplyShadow(stream,start,end,d.x,d.y);start=end;end=stream.Count;
        }
        vertices.Clear();vertices.AddUIVertexTriangleStream(stream);
    }
}
public class PositionAsUV1 : BaseMeshEffect
{
    public override void ModifyMesh(VertexHelper vertices) {
        if(!IsActive())return;
        UIVertex v=default;
        for(int i=0;i<vertices.currentVertCount;++i){vertices.PopulateUIVertex(ref v,i);v.uv1=new(v.position.X,v.position.Y);vertices.SetUIVertex(v,i);}
    }
}
