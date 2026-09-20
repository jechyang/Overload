using System;
using System.Collections.Generic;
using System.Buffers.Binary;
using Overload;
using Overload.UI;
using Overload.Maths;
using Overload.EventSystems;

sealed class GraphicWorld : INativeAccess
{
    public string LogicalName=>"BTests";
    public CanvasUpdateRegistry UIRegistry {get;}=new();
    public readonly List<Behaviour> Components=new();
    public readonly Dictionary<(ulong,string),ulong> Values=new();
    public UIVertex[] Vertices=Array.Empty<UIVertex>(); public int[] Indices=Array.Empty<int>();public int Uploads;
    public IReadOnlyList<Behaviour> GetBehaviours(ulong owner)=>Components.FindAll(c=>c.Owner.Handle==owner);
    public Vector3 Read(ulong c,bool b)=>default;public void Write(ulong c,bool b,Vector3 v){}public void Log(string s){}
    public ulong ExchangeFont(ulong owner,uint op,byte[] packet)=>throw new NotSupportedException();
    public void SetUIMesh(ulong r,UIVertex[] v,int[] i){UIMeshEncoding.Encode(v,i);Vertices=(UIVertex[])v.Clone();Indices=(int[])i.Clone();++Uploads;}
    public ulong Query(ulong h,uint op,ulong arg=0,string text="") {
        if(op is 1 or 18 or 19 or 21)return 1;
        if(op==14)return h==1?0ul:1ul;
        if(op==15)return 0;
        if(op is 30 or 31)return arg==1?h*10+1:arg==3?h*10+3:arg==2&&h==1?12ul:0;
        if(op==40)return NativeUIComponent.Pack(text switch{"rectSize"or"sizeDelta"=>new(100,100),"pivot"=>new(0,0),_=>Vector2.zero});
        if(op==42)return text=="scaleFactor"?NativeUIComponent.Pack(new(1,0)):0;
        if(op==48){if(text=="generation")return (ulong)Uploads;if(text=="depth")return 0;return Values.GetValueOrDefault((h,text),text is "colorRG"or"colorBA"?NativeUIComponent.Pack(Vector2.one):0);}
        if(op==47){Values[(h,text)]=arg;return 0;}
        if(op is 44 or 46)return 0;
        throw new Exception("Unexpected native op "+op);
    }
    public T Add<T>(ulong actor) where T:Behaviour,new(){var x=new T{owner=new Actor(this,actor)};Components.Add(x);return x;}
}
sealed class TestGraphic:Graphic{}
sealed class Shift:BaseMeshEffect {
    public override void ModifyMesh(VertexHelper h){UIVertex v=default;for(int i=0;i<h.currentVertCount;++i){h.PopulateUIVertex(ref v,i);v.position=new(v.position.X+3,v.position.Y,7);v.normal=new(.2f,.3f,.4f);v.tangent=new(1,2,3,4);v.uv2=new(.4f,.5f);v.uv3=new(.6f,.7f);h.SetUIVertex(v,i);}}
}
static class GraphicContracts
{
    static int checks;
    static void Check(bool yes,string why){++checks;if(!yes)throw new Exception(why);}
    static void Reject(Action action,string why){bool caught=false;try{action();}catch(ArgumentException){caught=true;}Check(caught,why);}
    static void Main(){
        using var h=new VertexHelper();var v=UIVertex.simpleVert;v.position=new(2,3,4);v.uv1=new(5,6);v.uv2=new(7,8);v.uv3=new(9,10);h.AddVert(v);h.AddVert(v);h.AddVert(v);h.AddTriangle(0,1,2);
        var mesh=new UIMesh();h.FillMesh(mesh);using var copy=new VertexHelper(mesh);UIVertex read=default;copy.PopulateUIVertex(ref read,1);Check(read.uv3==v.uv3&&read.tangent==v.tangent,"all channels survive FillMesh");
        var stream=new List<UIVertex>();h.GetUIVertexStream(stream);Check(stream.Count==3,"triangle stream");
        Reject(()=>h.AddUIVertexStream(new[]{v},new[]{0,0,2}),"reject bad indices");Check(h.currentVertCount==3&&h.currentIndexCount==3,"atomic bad index rejection");
        Reject(()=>h.AddUIVertexStream(new[]{v},new[]{0}),"reject incomplete triangle");Check(h.currentVertCount==3,"atomic incomplete stream");
        var bad=v;bad.normal=new(float.NaN,0,0);Reject(()=>h.AddVert(bad),"normal NaN");bad=v;bad.uv3=new(0,float.PositiveInfinity);Reject(()=>h.SetUIVertex(bad,0),"uv3 Inf");
        Reject(()=>h.AddUIVertexTriangleStream(new UIVertex[65538]),"overflow");
        var payload=UIMeshEncoding.Encode(mesh.GetVertices(),mesh.GetIndices());Check(payload.Length==16+3*88+12,"wire size");Check(BinaryPrimitives.ReadUInt32LittleEndian(payload)==UIMeshEncoding.Magic,"wire version");Check(BinaryPrimitives.ReadSingleLittleEndian(payload.AsSpan(16+80))==9,"uv3 encoding");
        for(int d=0;d<8;++d){var push=StencilMaterial.ForMask(d,false);var pop=StencilMaterial.ForPop(d);Check(push.writeMask==(1<<d)&&pop.writeMask==push.writeMask,"mask bit isolation");Check(StencilMaterial.ForDepth(d+1).readMask==push.reference,"children test all ancestors");}
        bool overflow=false;try{StencilMaterial.ForMask(8,true);}catch(InvalidOperationException){overflow=true;}Check(overflow,"depth limit");
        var world=new GraphicWorld();var graphic=world.Add<TestGraphic>(2);graphic.EnsureNativeComponents();graphic.OnEnable();
        var shift=world.Add<Shift>(2);shift.OnEnable();var uv=world.Add<PositionAsUV1>(2);uv.OnEnable();
        graphic.Rebuild(CanvasUpdate.PreRender);Check(world.Vertices[0].position.X==3&&world.Vertices[0].uv1.x==3,"modifier order");Check(world.Vertices[0].normal.X==.2f&&world.Vertices[0].uv3.x==.6f,"custom channels");
        int uploads=world.Uploads;graphic.SetMaterialDirty();graphic.Rebuild(CanvasUpdate.PreRender);Check(world.Uploads==uploads,"material dirty does not upload geometry");
        shift.enabled=false;graphic.Rebuild(CanvasUpdate.PreRender);Check(world.Vertices[0].position.X==0,"disabled modifier removed");
        var outline=world.Add<Outline>(2);outline.OnEnable();graphic.Rebuild(CanvasUpdate.PreRender);Check(world.Vertices.Length==30,"outline four copies and original");outline.enabled=false;
        var shadow=world.Add<Shadow>(2);shadow.effectColor=new(0,0,0,.5f);graphic.color=new(1,1,1,.5f);shadow.OnEnable();graphic.Rebuild(CanvasUpdate.PreRender);Check(world.Vertices.Length==12&&world.Vertices[0].color.a==64,"shadow alpha multiplication");
        graphic.CrossFadeAlpha(0,1,false);GraphicRebuildTracker.Update(graphic.Owner,.5f,.9f);Check(Math.Abs(graphic.canvasRenderer.GetAlpha()-.5f)<.001f,"scaled tween delta");
        graphic.CrossFadeAlpha(1,1,true);GraphicRebuildTracker.Update(graphic.Owner,0,1);Check(graphic.canvasRenderer.GetAlpha()==1,"unscaled tween delta");
        var canvas=new Actor(world,1).GetComponent<Canvas>()!;Check(GraphicRegistry.GetGraphicsForCanvas(canvas).Length==1,"registry consumer");
        graphic.OnDisable();Check(GraphicRegistry.GetGraphicsForCanvas(canvas).Length==0,"disable unregisters");graphic.OnEnable();graphic.OnReloadRelease();Check(GraphicRegistry.GetGraphicsForCanvas(canvas).Length==0,"reload unregisters");
        world.UIRegistry.Clear();Console.WriteLine($"PASS B graphics contracts: {checks} checks");
    }
}
