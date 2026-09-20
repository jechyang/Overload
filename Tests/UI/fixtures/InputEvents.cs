using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using Overload;
using Overload.UI;
using Overload.Maths;
using Overload.EventSystems;

sealed class World : INativeAccess
{
    internal readonly Dictionary<ulong,List<Behaviour>> objects = new();
    internal readonly Dictionary<ulong,ulong> parents = new();
    internal readonly HashSet<ulong> inactive = new();
    public CanvasUpdateRegistry UIRegistry { get; } = new();
    public string LogicalName => "FTests";
    internal Actor Create(ulong id, ulong parent=0) { objects[id]=new(); parents[id]=parent; return new(this,id); }
    internal T Add<T>(Actor actor) where T : Behaviour,new() { var c=new T(); c.owner=actor; objects[actor.Handle].Add(c); c.OnEnable(); return c; }
    public ulong Query(ulong owner,uint operation,ulong argument=0,string text="") {
        if (!objects.ContainsKey(owner)) return 0;
        return operation switch { 1=>owner, 18=>inactive.Contains(owner)?0ul:1ul, 14=>parents[owner], 19=>1, 30=>0, 21=>1, _=>throw new InvalidOperationException("Unexpected test native operation "+operation) };
    }
    public IReadOnlyList<Behaviour> GetBehaviours(ulong owner) => objects.TryGetValue(owner,out var values)?values:Array.Empty<Behaviour>();
    public ulong ExchangeFont(ulong owner,uint operation,byte[] payload) => throw new NotSupportedException();
    public void SetUIMesh(ulong renderer,UIVertex[] vertices,int[] indices) => throw new NotSupportedException();
    public Vector3 Read(ulong component,bool box) => default;
    public void Write(ulong component,bool box,Vector3 value) => throw new NotSupportedException();
    public void Log(string text) => Console.WriteLine(text);
}
sealed class TestRaycaster : BaseRaycaster
{
    public Actor? target, splitTarget;
    public override void Raycast(PointerEventData data,List<RaycastResult> results) {
        if(target!=null && data.position.x>=0 && data.position.x<100) results.Add(new(){actor=data.position.x>=50 && splitTarget!=null?splitTarget:target,module=this,screenPosition=data.position,depth=1});
    }
}
sealed class Recorder : UIBehaviour, IPointerEnterHandler, IPointerExitHandler, IPointerDownHandler, IPointerUpHandler, IPointerClickHandler, IInitializePotentialDragHandler, IBeginDragHandler, IDragHandler, IEndDragHandler, IDropHandler, IScrollHandler, IMoveHandler, ISubmitHandler, ICancelHandler, ISelectHandler, IDeselectHandler, IUpdateSelectedHandler
{
    internal readonly List<string> log=new();
    internal Action<string,BaseEventData>? callback;
    void Record(string name,BaseEventData data) { log.Add(name); callback?.Invoke(name,data); }
    public void OnPointerEnter(PointerEventData data) => Record("PointerEnter",data);
    public void OnPointerExit(PointerEventData data) => Record("PointerExit",data);
    public void OnPointerDown(PointerEventData data) => Record("PointerDown",data);
    public void OnPointerUp(PointerEventData data) => Record("PointerUp",data);
    public void OnPointerClick(PointerEventData data) => Record("PointerClick",data);
    public void OnInitializePotentialDrag(PointerEventData data) => Record("InitializePotentialDrag",data);
    public void OnBeginDrag(PointerEventData data) => Record("BeginDrag",data);
    public void OnDrag(PointerEventData data) => Record("Drag",data);
    public void OnEndDrag(PointerEventData data) => Record("EndDrag",data);
    public void OnDrop(PointerEventData data) => Record("Drop",data);
    public void OnScroll(PointerEventData data) => Record("Scroll",data);
    public void OnMove(AxisEventData data) => Record("Move",data);
    public void OnSubmit(BaseEventData data) => Record("Submit",data);
    public void OnCancel(BaseEventData data) => Record("Cancel",data);
    public void OnSelect(BaseEventData data) => Record("Select",data);
    public void OnDeselect(BaseEventData data) => Record("Deselect",data);
    public void OnUpdateSelected(BaseEventData data) => Record("UpdateSelected",data);
    protected override void SetDirty() { }
}
static class Program
{
    static void Check(bool value,string name) { if(!value) throw new Exception(name); Console.WriteLine("PASS "+name); }
    static void Main(string[] args) {
        var bytes=File.ReadAllBytes(args[0]); var words=new ulong[bytes.Length/8]; Buffer.BlockCopy(bytes,0,words,0,bytes.Length);
        var decoded=NativeUIInputSource.Decode(words);
        Check(decoded.events.Count==8 && decoded.mousePosition==new Vector2(20,420) && decoded.composition=="中🙂" && decoded.compositionCursor==2,"native-to-managed wire packet");
        bool bad=false; try{NativeUIInputSource.Decode(words.AsSpan(0,words.Length-1));}catch(ArgumentException){bad=true;} Check(bad,"truncated native input rejected");
        var world=new World(); var root=world.Create(1); var target=world.Create(2); var parent=world.Create(3); world.parents[2]=3;
        var system=world.Add<EventSystem>(root); var input=world.Add<BaseInput>(root); input.source=null;
        var module=world.Add<StandaloneInputModule>(root); var raycaster=world.Add<TestRaycaster>(root); raycaster.target=target;
        var recorder=world.Add<Recorder>(target); var ancestor=world.Add<Recorder>(parent);
        ulong sequence=0; double time=0; Vector2 position=new(10,10);
        void Tick(UIInputEvent[] events, int[]? held=null,bool focused=true) { input.Inject(new(++sequence,time+=0.1,0.1f,focused,position,new(200,200),events,held));system.Process(); }
        Tick(new[]{new UIInputEvent(UIInputKind.PointerDown,0,position),new UIInputEvent(UIInputKind.PointerUp,0,position)});
        Check(string.Join(",",recorder.log)=="PointerEnter,PointerDown,InitializePotentialDrag,PointerUp,PointerClick","same-frame down/up/click order");
        Check(ancestor.log.SequenceEqual(new[]{"PointerEnter"}),"hover ancestors; handled down does not bubble");
        recorder.log.Clear(); ancestor.log.Clear();
        Tick(new[]{new UIInputEvent(UIInputKind.PointerDown,0,position)});
        position=new(40,10); Tick(new[]{new UIInputEvent(UIInputKind.PointerMove,0,position)});
        position=new(150,10); Tick(new[]{new UIInputEvent(UIInputKind.PointerMove,0,position),new UIInputEvent(UIInputKind.PointerUp,0,position)});
        Check(recorder.log.Contains("BeginDrag") && recorder.log.Contains("Drag") && recorder.log.Last()=="EndDrag" && !recorder.log.Contains("PointerClick"),"drag out of bounds retains capture and ends");
        position=new(10,10); recorder.log.Clear(); Tick(new[]{new UIInputEvent(UIInputKind.PointerDown,0,position)});
        Tick(new[]{new UIInputEvent(UIInputKind.Focus,0)},focused:false);
        Check(recorder.log.Contains("PointerUp") && !recorder.log.Contains("PointerClick") && !system.IsPointerOverObject(),"focus loss cancels capture and hover");
        recorder.log.Clear(); Tick(new[]{new UIInputEvent(UIInputKind.Focus,1),new UIInputEvent(UIInputKind.Touch,7,position),new UIInputEvent(UIInputKind.Touch,11,position)});
        Check(system.IsPointerOverObject(7) && system.IsPointerOverObject(11),"two independent finger captures");
        Tick(new[]{new UIInputEvent(UIInputKind.Touch,7,position,TouchPhase.Ended),new UIInputEvent(UIInputKind.Touch,11,position,TouchPhase.Canceled)});
        Check(recorder.log.Count(x=>x=="PointerClick")==1 && !system.IsPointerOverObject(11),"ended touch clicks; canceled touch does not");
        system.SetSelectedObject(target); recorder.log.Clear();
        Tick(new[]{new UIInputEvent(UIInputKind.KeyDown,257)},new[]{262});
        Check(recorder.log.Contains("UpdateSelected") && recorder.log.Contains("Move") && recorder.log.Contains("Submit"),"update-selected/navigation/submit");
        Tick(new[]{new UIInputEvent(UIInputKind.KeyDown,256)});
        Check(recorder.log.Contains("Cancel"),"cancel dispatch");
        recorder.callback=(name,data)=>{if(name=="Deselect")system.SetSelectedObject(parent);}; system.SetSelectedObject(null);
        Check(system.currentSelected==null && !system.alreadySelecting,"selection reentrancy guard"); recorder.callback=null;
        recorder.log.Clear(); recorder.callback=(name,data)=>{if(name=="PointerDown")world.inactive.Add(target.Handle);};
        Tick(new[]{new UIInputEvent(UIInputKind.PointerDown,0,position),new UIInputEvent(UIInputKind.PointerUp,0,position)});
        Check(!recorder.log.Contains("PointerClick"),"callback deactivates target without stale click"); world.inactive.Clear(); recorder.callback=null;
        var trigger=world.Add<EventTrigger>(target); var entry=new EventTrigger.Entry{eventID=EventTriggerType.Scroll};int scrolls=0;entry.callback.AddListener(_=>++scrolls);trigger.triggers.Add(entry);
        Tick(new[]{new UIInputEvent(UIInputKind.Scroll,0,new(0,1))}); Check(scrolls==1,"EventTrigger uses shared UIEvent");
        // Drag to another receiver: drop on the hit target and end on the captured drag owner.
        var dropActor=world.Create(4);var drop=world.Add<Recorder>(dropActor);raycaster.splitTarget=dropActor;
        recorder.log.Clear();position=new(10,10);Tick(new[]{new UIInputEvent(UIInputKind.PointerDown,0,position)});
        position=new(70,10);Tick(new[]{new UIInputEvent(UIInputKind.PointerMove,0,position),new UIInputEvent(UIInputKind.PointerUp,0,position)});
        Check(drop.log.Contains("Drop") && recorder.log.Last()=="EndDrag" && !recorder.log.Contains("PointerClick"),"drop is routed to new target; drag ends on capture owner");raycaster.splitTarget=null;
        // Right button capture has its own pointer ID and no duplicate hover chain.
        recorder.log.Clear();position=new(10,10);Tick(new[]{new UIInputEvent(UIInputKind.PointerDown,1,position),new UIInputEvent(UIInputKind.PointerUp,1,position)});
        Check(recorder.log.Count(x=>x=="PointerClick")==1 && recorder.log.Count(x=>x=="PointerEnter")<=1,"independent right-button click");
        system.SetSelectedObject(target); recorder.log.Clear();Tick(Array.Empty<UIInputEvent>(),new[]{262});
        int moveCount=recorder.log.Count(x=>x=="Move");Tick(Array.Empty<UIInputEvent>(),new[]{262});Tick(Array.Empty<UIInputEvent>(),new[]{262});
        Check(recorder.log.Count(x=>x=="Move")==moveCount,"navigation repeat delay suppresses early repeats");
        for(int i=0;i<5;++i)Tick(Array.Empty<UIInputEvent>(),new[]{262});
        Check(recorder.log.Count(x=>x=="Move")>moveCount,"held navigation repeats after delay");
        recorder.callback=(name,data)=>{if(name=="UpdateSelected")data.Use();};recorder.log.Clear();
        Tick(new[]{new UIInputEvent(UIInputKind.KeyDown,257)},new[]{262});Check(!recorder.log.Contains("Submit")&&!recorder.log.Contains("Move"),"used update-selected suppresses navigation");recorder.callback=null;
        var ownedEvents=new[]{new UIInputEvent(UIInputKind.Character,0x1f642)};var immutable=new UIInputSnapshot(100,100,1,true,default,default,ownedEvents);ownedEvents[0]=new(UIInputKind.Character,65);
        Check(immutable.events[0].id==0x1f642,"caller mutations cannot change snapshot");
        recorder.log.Clear();recorder.callback=(name,data)=>{if(name=="PointerDown")system.enabled=false;};
        Tick(new[]{new UIInputEvent(UIInputKind.PointerDown,0,position),new UIInputEvent(UIInputKind.PointerUp,0,position)});
        Check(!recorder.log.Contains("PointerClick") && !system.IsPointerOverObject(),"callback disabling EventSystem stops the rest of the frame");recorder.callback=null;system.enabled=true;
        var other=new World(); var foreign=other.Create(1);bad=false;try{system.SetSelectedObject(foreign);}catch(InvalidOperationException){bad=true;}Check(bad,"cross-session selection rejected");
        var a=new RaycastResult{module=raycaster,distance=1,sortingOrder=1,index=1}; var b=new RaycastResult{module=raycaster,distance=10,sortingOrder=2,index=2};
        Check(EventSystem.CompareRaycasts(a,b)>0,"sorting order precedes distance");
        foreach(var components in world.objects.Values) foreach(var component in components.ToArray()) { component.OnDestroy(); component.ReleaseSubscriptions(Console.WriteLine); component.owner=null; }
        Check(RaycasterManager.GetRaycasters(root).Length==0 && EventSystem.GetCurrent(root)==null,"destroy unregisters raycasters and systems");world.UIRegistry.Clear();
        Console.WriteLine("PASS F deterministic event consumers");
    }
}
