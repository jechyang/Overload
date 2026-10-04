using Overload.Maths;
namespace Overload.UI;

public static class RectTransformUtility
{
    // Uses the same immutable projection as raycasts, without clipping the captured drag.
    public static bool ScreenPointToLocalPointInRectangle(RectTransform rect, Vector2 screen, Actor? camera, out Vector2 local)
    {
        for (Actor? actor = rect.Owner; actor != null; actor = actor.Parent) {
            if (actor.GetComponent<Graphic>() is not Graphic graphic) continue;
            var renderer = graphic.canvasRenderer;
            ulong packed = NativeUIComponent.Pack(screen);
            if (actor.Access.Query(renderer.Handle, 53, packed, "projectValid") == 0) continue;
            Vector2 point = NativeUIComponent.Unpack(actor.Access.Query(renderer.Handle, 53, packed, "projectLocal"));
            local = Clipping.LocalToCanvas(graphic.rectTransform, point) - Clipping.LocalToCanvas(rect, Vector2.zero);
            return true;
        }
        var canvas = rect.Owner.GetComponentInParentCanvas();
        if (canvas != null && canvas.renderMode == RenderMode.ScreenSpaceOverlay) {
            local = screen / canvas.scaleFactor - Clipping.LocalToCanvas(rect, Vector2.zero);
            return true;
        }
        local = default; return false;
    }
    public static bool RectangleContainsScreenPoint(RectTransform rect, Vector2 screen, Actor? camera = null)
        => ScreenPointToLocalPointInRectangle(rect, screen, camera, out var point) && rect.rect.Contains(point);
    internal static Canvas? GetComponentInParentCanvas(this Actor owner) {
        for (Actor? actor = owner; actor != null; actor = actor.Parent)
            if (actor.GetComponent<Canvas>() is Canvas canvas) return canvas;
        return null;
    }
}
