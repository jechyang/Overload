using System;
using System.Collections.Generic;
using System.Linq;
using Overload;
using Overload.Maths;
using Overload.UI;

// Only the native handle/geometry boundary is doubled. Every layout controller,
// tracker, utility and rebuild scheduler in this executable is production source.
internal sealed class LayoutWorld : INativeAccess
{
    internal sealed class Node(ulong id) {
        internal readonly ulong Id = id;
        internal ulong Parent;
        internal bool Active = true, Alive = true;
        internal Vector2 Min = new(.5f,.5f), Max = new(.5f,.5f), Pivot = new(.5f,.5f), Position, Delta = new(100,100);
        internal readonly List<ulong> Children = new();
        internal readonly List<Behaviour> Behaviours = new();
        internal readonly Dictionary<ulong,(uint mask,Vector2[] saved)> Leases = new();
    }
    internal readonly Dictionary<ulong,Node> Nodes = new();
    internal readonly List<Exception> Errors = new();
    internal readonly List<string> Logs = new();
    internal int Writes;
    internal bool ForeignBridgeException;
    private ulong next = 1, token = 1;
    public string LogicalName => "LayoutTests";
    public CanvasUpdateRegistry UIRegistry { get; } = new();
    internal Actor New(Actor? parent = null) {
        var n = new Node(next++); Nodes.Add(n.Id,n);
        if(parent != null) { n.Parent=parent.Handle; Nodes[parent.Handle].Children.Add(n.Id); }
        return new(this,n.Id);
    }
    internal T Add<T>(Actor actor) where T:Behaviour,new() {
        var b = new T { owner=actor }; Nodes[actor.Handle].Behaviours.Add(b); return b;
    }
    internal Vector2 Size(Node n) {
        Vector2 parent = n.Parent==0 ? Vector2.zero : Size(Nodes[n.Parent]);
        return new(parent.x*(n.Max.x-n.Min.x)+n.Delta.x,parent.y*(n.Max.y-n.Min.y)+n.Delta.y);
    }
    internal void Tick(Actor root) => UIRegistry.Update(root,new(800,600),Nodes.Values.Where(n=>n.Alive).SelectMany(n=>n.Behaviours).ToArray(),Errors.Add);
    private Node Get(ulong id) { if(!Nodes.TryGetValue(id,out var n)||!n.Alive){if(ForeignBridgeException)throw new Exception("Native invalid handle");throw new InvalidOperationException("Invalid native epoch");} return n; }
    public IReadOnlyList<Behaviour> GetBehaviours(ulong owner) => Get(owner).Behaviours;
    public Vector3 Read(ulong component,bool box)=>new(0,0,0);
    public void Write(ulong component,bool box,Vector3 value) { }
    public void Log(string message)=>Logs.Add(message);
    public ulong ExchangeFont(ulong owner, uint op, byte[] packet) => throw new NotSupportedException("Layout test does not request fonts");
    public void SetUIMesh(ulong renderer,UIVertex[] vertices,int[] indices) { }
    public ulong Query(ulong handle,uint op,ulong arg=0,string text="") {
        bool component = op is 40 or 41 or 60 or 61 or 62;
        var n=Get(component?handle/10:handle);
        switch(op) {
            case 1:return n.Id;
            case 2:return n.Id;
            case 4:return n.Active?1ul:0;
            case 5:n.Active=arg!=0;return 0;
            case 14:return n.Parent;
            case 15:return (ulong)n.Children.Count;
            case 16:return n.Children[(int)arg];
            case 18:for(var p=n;;p=Get(p.Parent)){if(!p.Active)return 0;if(p.Parent==0)return 1;}
            case 20:return 0;
            case 21:return 1;
            case 30:case 31:return arg==1?n.Id*10+1:0;
            case 40:return NativeUIComponent.Pack(text switch {"anchorMin"=>n.Min,"anchorMax"=>n.Max,"pivot"=>n.Pivot,"anchoredPosition"=>n.Position,"sizeDelta"=>n.Delta,"rectSize"=>Size(n),_=>throw new Exception(text)});
            case 41:
                var value=NativeUIComponent.Unpack(arg);++Writes;
                switch(text){case "anchorMin":n.Min=value;break;case "anchorMax":n.Max=value;break;case "pivot":n.Pivot=value;break;case "anchoredPosition":n.Position=value;break;case "sizeDelta":n.Delta=value;break;default:throw new Exception(text);}return 0;
            case 60:
                ulong id=text.Length==0?token++:ulong.Parse(text);
                uint mask=(uint)arg;
                foreach(var pair in n.Leases)if(pair.Key!=id&&(pair.Value.mask&mask)!=0)throw new InvalidOperationException("Conflicting native claims");
                Vector2[] current=[n.Position,n.Min,n.Max,n.Delta,n.Pivot];
                if(n.Leases.TryGetValue(id,out var claim)) {
                    for(int bit=0;bit<10;++bit)if((mask&(1u<<bit))!=0&&(claim.mask&(1u<<bit))==0)claim.saved[bit/2][bit%2]=current[bit/2][bit%2];
                    n.Leases[id]=(claim.mask|mask,claim.saved);
                } else n.Leases.Add(id,(mask,current));
                return id;
            case 61:
                var old=n.Leases[arg];uint release=text.Length==0?1023:uint.Parse(text);
                Vector2[] values=[n.Position,n.Min,n.Max,n.Delta,n.Pivot];
                for(int bit=0;bit<10;++bit)if((release&old.mask&(1u<<bit))!=0)values[bit/2][bit%2]=old.saved[bit/2][bit%2];
                n.Position=values[0];n.Min=values[1];n.Max=values[2];n.Delta=values[3];n.Pivot=values[4];
                if((old.mask&~release)==0)n.Leases.Remove(arg);else n.Leases[arg]=(old.mask&~release,old.saved);
                return 0;
            case 62:return n.Leases.Values.Aggregate(0u,(sum,c)=>sum|c.mask);
            default:throw new Exception("Unexpected operation "+op);
        }
    }
}
internal sealed class CountingGrid : GridLayoutGroup {
    internal int Passes;
    public override void CalculateLayoutInputHorizontal(){++Passes;base.CalculateLayoutInputHorizontal();}
}
internal sealed class RecursiveController : Overload.EventSystems.UIBehaviour,ILayoutSelfController {
    public void SetLayoutHorizontal()=>LayoutRebuilder.ForceRebuildLayoutImmediate(rectTransform);
    public void SetLayoutVertical(){}
}
internal sealed class DynamicSize : Behaviour, ILayoutElement {
    internal float Width = 20;
    public float minWidth => Width; public float preferredWidth => Width; public float flexibleWidth => 0;
    public float minHeight => 10; public float preferredHeight => 10; public float flexibleHeight => 0;
    public int layoutPriority => 1;
    public void CalculateLayoutInputHorizontal() { } public void CalculateLayoutInputVertical() { }
}
internal sealed class DeferredGrid : GridLayoutGroup {
    internal int Passes;
    public override void CalculateLayoutInputHorizontal() { ++Passes; base.CalculateLayoutInputHorizontal(); }
    public override void SetLayoutVertical() { base.SetLayoutVertical(); if (Passes == 1) spacing = new(17, 9); }
}
internal static class LayoutContracts
{
    private static int checks;
    private static void Check(bool result,string message){++checks;if(!result)throw new Exception(message);}
    private static void Near(float actual,float expected,string message)=>Check(Math.Abs(actual-expected)<.002f,message+": "+actual+" != "+expected);
    private static RectTransform Rect(Actor a)=>a.GetComponent<RectTransform>()!;
    private static void Grids(){
        foreach(GridLayoutGroup.Corner corner in Enum.GetValues<GridLayoutGroup.Corner>())
        foreach(GridLayoutGroup.Axis axis in Enum.GetValues<GridLayoutGroup.Axis>())
        foreach(GridLayoutGroup.Constraint constraint in Enum.GetValues<GridLayoutGroup.Constraint>())
        foreach(TextAnchor alignment in Enum.GetValues<TextAnchor>()) {
            var w=new LayoutWorld();var root=w.New();Rect(root).sizeDelta=new(260,180);
            var grid=w.Add<GridLayoutGroup>(root);grid.startCorner=corner;grid.startAxis=axis;grid.constraint=constraint;grid.constraintCount=2;
            grid.cellSize=new(60,40);grid.spacing=new(10,5);grid.padding=new(3,7,11,13);grid.childAlignment=alignment;
            var children=new List<Actor>();
            for(int i=0;i<7;++i){var c=w.New(root);Rect(c).pivot=new(.2f,.8f);Rect(c).anchorMin=Vector2.zero;Rect(c).anchorMax=Vector2.one;children.Add(c);}
            LayoutRebuilder.ForceRebuildLayoutImmediate(Rect(root));
            int columns=constraint==GridLayoutGroup.Constraint.FixedColumnCount?2:constraint==GridLayoutGroup.Constraint.FixedRowCount?4:3;
            int rows=constraint==GridLayoutGroup.Constraint.FixedColumnCount?4:constraint==GridLayoutGroup.Constraint.FixedRowCount?2:3;
            int stride=axis==GridLayoutGroup.Axis.Horizontal?columns:rows;
            int usedColumns=axis==GridLayoutGroup.Axis.Horizontal?columns:Math.Min(columns,(7+stride-1)/stride);
            int usedRows=axis==GridLayoutGroup.Axis.Vertical?rows:Math.Min(rows,(7+stride-1)/stride);
            float left=3+(250-(usedColumns*70-10))*((int)alignment%3)*.5f;
            float top=11+(156-(usedRows*45-5))*((int)alignment/3)*.5f;
            for(int i=0;i<7;++i){
                int x=axis==GridLayoutGroup.Axis.Horizontal?i%stride:i/stride,y=axis==GridLayoutGroup.Axis.Horizontal?i/stride:i%stride;
                if(((int)corner&1)!=0)x=usedColumns-1-x;if(((int)corner&2)!=0)y=usedRows-1-y;
                var r=Rect(children[i]);Near(r.sizeDelta.x,60,"Grid width");Near(r.sizeDelta.y,40,"Grid height");
                Near(r.anchoredPosition.x,left+x*70+12,"Grid corner/axis/constraint/alignment x");
                Near(r.anchoredPosition.y,-top-y*45-8,"Grid corner/axis/constraint/alignment y");
            }
            grid.enabled=false;
            foreach(var c in children){Check(Rect(c).anchorMin==Vector2.zero&&Rect(c).anchorMax==Vector2.one,"Grid disable restores anchors");Check(Rect(c).sizeDelta==new Vector2(100,100),"Grid disable restores size");}
        }
        var empty=new LayoutWorld();var actor=empty.New();var g=empty.Add<GridLayoutGroup>(actor);g.spacing=new(-100,-100);g.cellSize=new(0,0);g.constraintCount=0;
        LayoutRebuilder.ForceRebuildLayoutImmediate(Rect(actor));Check(g.constraintCount==1,"constraint count clamp");
        var child=empty.New(actor);LayoutRebuilder.ForceRebuildLayoutImmediate(Rect(actor));Check(float.IsFinite(Rect(child).anchoredPosition.x),"nonpositive grid step finite");
    }
    private static void Aspects(){
        foreach(AspectRatioFitter.AspectMode mode in Enum.GetValues<AspectRatioFitter.AspectMode>()){
            var w=new LayoutWorld();var parent=w.New();Rect(parent).sizeDelta=new(300,200);var child=w.New(parent);var r=Rect(child);
            r.anchorMin=new(.1f,.2f);r.anchorMax=new(.9f,.8f);r.pivot=new(.2f,.8f);r.sizeDelta=new(20,30);r.anchoredPosition=new(7,9);
            var fit=w.Add<AspectRatioFitter>(child);fit.aspectRatio=2;fit.aspectMode=mode;
            LayoutRebuilder.ForceRebuildLayoutImmediate(r);
            if(mode==AspectRatioFitter.AspectMode.None){Near(r.rect.width,260,"None width");Near(r.rect.height,150,"None height");}
            else {Near(r.rect.width/r.rect.height,2,"Aspect ratio");Check(r.pivot==new Vector2(.2f,.8f),"Aspect preserves noncenter pivot");}
            if(mode is AspectRatioFitter.AspectMode.FitInParent or AspectRatioFitter.AspectMode.EnvelopeParent){
                Near(r.rect.width,mode==AspectRatioFitter.AspectMode.FitInParent?300:400,"Aspect fit/envelope width");
                w.Tick(parent);Rect(parent).sizeDelta=new(600,100);w.Tick(parent);
                Near(r.rect.width,mode==AspectRatioFitter.AspectMode.FitInParent?200:600,"Parent resize");
            }
            fit.enabled=false;Check(r.anchorMin==new Vector2(.1f,.2f)&&r.anchorMax==new Vector2(.9f,.8f),"Aspect restores stretch");
            Check(r.sizeDelta==new Vector2(20,30)&&r.anchoredPosition==new Vector2(7,9),"Aspect restores dimensions and position");
        }
    }
    private static void HorizontalVertical(){
        foreach(bool vertical in new[]{false,true})foreach(bool control in new[]{false,true})foreach(bool expand in new[]{false,true}){
            var w=new LayoutWorld();var root=w.New();Rect(root).sizeDelta=new(200,200);
            HorizontalOrVerticalLayoutGroup group=vertical?w.Add<VerticalLayoutGroup>(root):w.Add<HorizontalLayoutGroup>(root);
            group.childControlWidth=group.childControlHeight=control;group.childForceExpandWidth=group.childForceExpandHeight=expand;group.childAlignment=TextAnchor.MiddleCenter;group.spacing=10;
            var a=w.New(root);var b=w.New(root);Rect(a).sizeDelta=Rect(b).sizeDelta=new(30,30);
            foreach(var c in new[]{a,b}){var e=w.Add<LayoutElement>(c);e.minWidth=e.minHeight=20;e.preferredWidth=e.preferredHeight=40;}
            LayoutRebuilder.ForceRebuildLayoutImmediate(Rect(root));
            float expected=control?(expand?95:40):30;
            Near(Rect(a).sizeDelta[vertical?1:0],expected,"HV control/expand size");
            Check((w.Nodes[a.Handle].Leases.Values.First().mask & (uint)DrivenTransformProperties.SizeDelta)==(control?(uint)DrivenTransformProperties.SizeDelta:0),"HV size ownership");
            group.enabled=false;Check(Rect(a).sizeDelta==new Vector2(30,30),"HV restore size");
        }
    }
    private static void SchedulingAndLifetime(){
        var w=new LayoutWorld();var root=w.New();var grid=w.Add<CountingGrid>(root);grid.cellSize=new(20,20);var child=w.New(root);
        w.Tick(root);w.Tick(root);int passes=grid.Passes,writes=w.Writes;w.Tick(root);w.Tick(root);
        Check(grid.Passes==passes&&w.Writes==writes,"Idle frames must not rebuild or write geometry");
        grid.cellSize=grid.cellSize;w.Tick(root);Check(grid.Passes==passes,"Unchanged property must not dirty");
        w.Nodes[child.Handle].Delta=new(33,42);w.Tick(root);Check(grid.Passes==passes+1,"Native child edit marks parent dirty");
        passes=grid.Passes;w.New(root);w.Tick(root);Check(grid.Passes==passes+1,"Child addition marks layout dirty");
        grid.ReleaseSubscriptions(_=>{});Check(w.Nodes[child.Handle].Leases.Count==0,"Session cleanup releases native leases");
        Check(Rect(child).sizeDelta==new Vector2(100,100),"Session cleanup restores authored size");
        var recursive=new LayoutWorld();var a=recursive.New();recursive.Add<RecursiveController>(a);
        bool rejected=false;try{LayoutRebuilder.ForceRebuildLayoutImmediate(Rect(a));}catch(InvalidOperationException){rejected=true;}Check(rejected,"Recursive layout must report cycle");
        var conflict=new LayoutWorld();var p=conflict.New();conflict.Add<GridLayoutGroup>(p);var c=conflict.New(p);var fit=conflict.Add<ContentSizeFitter>(c);fit.horizontalFit=ContentSizeFitter.FitMode.PreferredSize;
        rejected=false;try{LayoutRebuilder.ForceRebuildLayoutImmediate(Rect(p));}catch(InvalidOperationException){rejected=true;}Check(rejected,"Parent group/self fitter property conflict must report cycle");
    }
    private static void FitterAndMutation() {
        var w=new LayoutWorld();var root=w.New();var r=Rect(root);r.sizeDelta=new(90,80);
        var size=w.Add<LayoutElement>(root);size.minWidth=12;size.preferredWidth=35;size.minHeight=15;size.preferredHeight=45;
        var fitter=w.Add<ContentSizeFitter>(root);fitter.horizontalFit=ContentSizeFitter.FitMode.PreferredSize;fitter.verticalFit=ContentSizeFitter.FitMode.MinSize;
        w.Tick(root);Check(r.sizeDelta==new Vector2(35,15),"Content fitter preferred/min dimensions");
        fitter.horizontalFit=ContentSizeFitter.FitMode.Unconstrained;w.Tick(root);Check(r.sizeDelta==new Vector2(90,15),"Content fitter releases one axis");
        fitter.enabled=false;Check(r.sizeDelta==new Vector2(90,80),"Content fitter restores both axes");
        var dynamicWorld=new LayoutWorld();var p=dynamicWorld.New();var g=dynamicWorld.Add<HorizontalLayoutGroup>(p);g.childForceExpandWidth=g.childForceExpandHeight=false;
        var child=dynamicWorld.New(p);var input=dynamicWorld.Add<DynamicSize>(child);dynamicWorld.Tick(p);Near(Rect(child).sizeDelta.x,20,"Dynamic layout initial value");
        input.Width=57;dynamicWorld.Tick(p);Near(Rect(child).sizeDelta.x,57,"Non-UI layout provider changes are observed");
        var deferredWorld=new LayoutWorld();var d=deferredWorld.New();var controller=deferredWorld.Add<DeferredGrid>(d);controller.cellSize=new(10,10);controller.constraint=GridLayoutGroup.Constraint.FixedColumnCount;
        deferredWorld.New(d);var second=deferredWorld.New(d);deferredWorld.Tick(d);deferredWorld.Tick(d);Near(Rect(second).anchoredPosition.x,32,"Controller change during layout deferred to next pass");
        var ignoredWorld=new LayoutWorld();var ip=ignoredWorld.New();var ig=ignoredWorld.Add<GridLayoutGroup>(ip);var ic=ignoredWorld.New(ip);var ie=ignoredWorld.Add<LayoutElement>(ic);
        ignoredWorld.Tick(ip);ie.ignoreLayout=true;ignoredWorld.Tick(ip);Check(Rect(ic).sizeDelta==new Vector2(100,100)&&ignoredWorld.Nodes[ic.Handle].Leases.Count==0,"Ignored child releases driven claims");
        // Inspector/host field writes bypass the public enabled setter.
        ie.ignoreLayout=false;ignoredWorld.Tick(ip);
        typeof(Overload.EventSystems.UIBehaviour).GetField("m_Enabled",System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Instance)!.SetValue(ig,false);
        ignoredWorld.Tick(ip);Check(ignoredWorld.Nodes[ic.Handle].Leases.Count==0,"Inspector disable releases driven properties before rebuild");
        var switched=new LayoutWorld();var sa=switched.New();var sr=Rect(sa);sr.sizeDelta=new(120,80);var af=switched.Add<AspectRatioFitter>(sa);af.aspectRatio=2;af.aspectMode=AspectRatioFitter.AspectMode.WidthControlsHeight;switched.Tick(sa);switched.Tick(sa);switched.Tick(sa);
        typeof(AspectRatioFitter).GetField("m_AspectMode",System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Instance)!.SetValue(af,AspectRatioFitter.AspectMode.HeightControlsWidth);af.MarkDirtyFromHost();
        switched.Tick(sa);Check(sr.sizeDelta==new Vector2(160,80),"Inspector aspect mode switch restores old axis before solving");
        g.childControlWidth=false;Rect(child).sizeDelta=new(23,30);dynamicWorld.Tick(p);Near(Rect(child).sizeDelta.x,23,"User write after disabling child control survives reconciliation");
        // Layout-generated child dimension notifications must not dirty their parent again.
        var stable=new LayoutWorld();var sp=stable.New();var sg=stable.Add<CountingGrid>(sp);var sc=stable.New(sp);stable.Add<LayoutElement>(sc);stable.Tick(sp);int measured=sg.Passes;stable.Tick(sp);Check(sg.Passes==measured,"First layout consumes child dimension notification without another pass");
        var reparented=new LayoutWorld();var oldParent=reparented.New();var moved=reparented.New(oldParent);reparented.Add<LayoutElement>(moved);reparented.Tick(oldParent);
        var newParent=reparented.New();reparented.Nodes[moved.Handle].Parent=newParent.Handle;reparented.Nodes[newParent.Handle].Children.Add(moved.Handle);reparented.Nodes[oldParent.Handle].Children.Clear();reparented.Nodes[oldParent.Handle].Alive=false;reparented.ForeignBridgeException=true;
        reparented.Tick(newParent);Check(reparented.Errors.Count==0,"Reparenting away from a destroyed parent ignores only old-parent lookup failure");
        var invalid=new AspectRatioFitter();bool rejected=false;try{invalid.aspectRatio=0;}catch(ArgumentOutOfRangeException){rejected=true;}Check(rejected,"Zero aspect rejected");
        rejected=false;try{ig.cellSize=new(float.NaN,1);}catch(ArgumentException){rejected=true;}Check(rejected,"Nonfinite grid rejected");
        // A removed native epoch must be ignored even when the host uses its own Exception subtype.
        var deadWorld=new LayoutWorld();var dp=deadWorld.New();var dg=deadWorld.Add<GridLayoutGroup>(dp);var dc=deadWorld.New(dp);deadWorld.Tick(dp);deadWorld.Nodes[dc.Handle].Alive=false;deadWorld.ForeignBridgeException=true;
        var cleanupErrors=new List<string>();dg.ReleaseSubscriptions(cleanupErrors.Add);Check(cleanupErrors.Count==0,"Deleted child cleanup accepts native host exception type");
    }
    public static void Main(){Grids();Aspects();HorizontalVertical();SchedulingAndLifetime();FitterAndMutation();Console.WriteLine("PASS U4 layout contracts: "+checks+" assertions");}
}
