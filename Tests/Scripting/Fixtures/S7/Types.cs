using System;
using System.IO;
using Overload;
using Overload.Maths;

namespace S7;
public sealed class Noop : Behaviour { public override void OnUpdate(float dt) { } }
public sealed class Move : Behaviour
{
    public override void OnUpdate(float dt) { var p=Transform.Position; Transform.Position=new Vector3(p.X+dt,p.Y,p.Z); }
}
[ScriptId("s7-probe")]
public sealed class Probe : Behaviour
{
    [FieldId("speed")] public float Speed=2;
    private int frames;
    private float total;
    private void Mark(string text) {
        string? path=Environment.GetEnvironmentVariable("OV_S7_MARKER");
        if(path != null) File.AppendAllText(path,text+"\n");
    }
    public override void OnAwake() {
        if(Speed!=7) throw new Exception("S7 persisted field mismatch");
        Mark("awake speed=7 runtime="+System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription);
    }
    public override void OnStart() { Mark("start"); }
    public override void OnUpdate(float dt) {
        total+=Speed*dt; var p=Transform.Position;Transform.Position=new Vector3(p.X+Speed*dt,p.Y,p.Z);
        var box=Owner.GetComponent<Box>()!;box.Size=new Vector3(2+total,3,4);
        if(Math.Abs(Transform.Position.X-total)>0.01f || Math.Abs(box.Size.X-(2+total))>0.01f) throw new Exception("S7 native Transform/Box mismatch");
        if(++frames==8) Mark("running frames=8 transform-box=verified");
    }
    public override void OnDestroy(){Mark("destroy frames="+frames);}
}
