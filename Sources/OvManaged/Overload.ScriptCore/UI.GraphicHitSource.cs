using System.Collections.Generic;
using Overload.EventSystems;
using Overload.Maths;
namespace Overload.UI;

/// <summary>Reads the most recently extracted immutable UI frame; screen coordinates are Game viewport bottom-left pixels.</summary>
public sealed class GraphicHitSource : IUIGraphicHitSource
{
    private static ulong Query(Graphic graphic,Vector2 screen,string property) => graphic.Owner.Access.Query(graphic.canvasRenderer.Handle,53,NativeUIComponent.Pack(screen),property);
    public void GetHits(Actor canvasOwner,Vector2 screenPoint,List<UIGraphicHit> results) {
        var canvas=canvasOwner.GetComponent<Canvas>();if(canvas==null)return;
        foreach(var graphic in GraphicRegistry.GetRaycastableGraphicsForCanvas(canvas)) {
            if(Query(graphic,screenPoint,"valid")==0)continue;
            var xy=NativeUIComponent.Unpack(Query(graphic,screenPoint,"worldXY"));var z=NativeUIComponent.Unpack(Query(graphic,screenPoint,"worldZ"));
            var nxy=NativeUIComponent.Unpack(Query(graphic,screenPoint,"normalXY"));var nz=NativeUIComponent.Unpack(Query(graphic,screenPoint,"normalZ"));
            results.Add(new(graphic.Owner,NativeUIComponent.Unpack(Query(graphic,screenPoint,"distance")).x,
                unchecked((int)Query(graphic,screenPoint,"depth")),0,unchecked((int)Query(graphic,screenPoint,"order")),new(xy.x,xy.y,z.x),new(nxy.x,nxy.y,nz.x),Query(graphic,screenPoint,"reversed")!=0));
        }
    }
    public bool IsRaycastLocationValid(Actor owner,Vector2 screenPoint) {
        var graphic=owner.GetComponent<Graphic>();
        return graphic!=null && Query(graphic,screenPoint,"valid")!=0 && graphic.Raycast(NativeUIComponent.Unpack(Query(graphic,screenPoint,"local")));
    }
}
