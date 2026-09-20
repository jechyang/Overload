using System;
using System.Threading.Tasks;
using Overload;
using Overload.UI;
using Overload.Maths;
[assembly:EnableUI]
namespace GraphicRuntime;
public class Shape:MaskableGraphic { public override Color color {get=>Color.green;set=>base.color=value;} }
public class MaskShape:MaskableGraphic {
 protected override void OnPopulateMesh(VertexHelper h){var r=rectTransform.rect;var c=UIVertex.simpleVert;var center=r.min+r.size*.5f;
 c.position=new(center.x,r.min.y,0);h.AddVert(c);c.position=new(r.max.x,center.y,0);h.AddVert(c);c.position=new(center.x,r.max.y,0);h.AddVert(c);c.position=new(r.min.x,center.y,0);h.AddVert(c);h.AddTriangle(0,1,2);h.AddTriangle(2,3,0);}
}
public class HiddenMask:Mask { public HiddenMask(){showMaskGraphic=false;} }
public class ChannelModifier:BaseMeshEffect {
 [SerializeField]public float shift=3;
 [NonSerialized]private bool checkedThread;
 public override void ModifyMesh(VertexHelper h){if(!checkedThread){var r=graphic!.canvasRenderer;bool denied=Task.Run(()=>{try{_ = r.vertexCount;return false;}catch{return true;}}).GetAwaiter().GetResult();if(!denied)throw new Exception("Worker accessed CanvasRenderer");checkedThread=true;}UIVertex v=default;for(int i=0;i<h.currentVertCount;++i){h.PopulateUIVertex(ref v,i);v.position=new(v.position.X+shift,v.position.Y,0);v.normal=new(.2f,.3f,.4f);v.tangent=new(1,2,3,-1);v.uv1=new(.1f,.2f);v.uv2=new(.3f,.4f);v.uv3=new(.5f,.6f);v.color=new(255,0,0,255);h.SetUIVertex(v,i);}}
}

public class RootGroup:CanvasGroup {}
public class RootClip:RectMask2D {}
