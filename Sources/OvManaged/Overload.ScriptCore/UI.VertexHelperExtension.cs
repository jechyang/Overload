using Overload.Maths;
namespace Overload.UI;

// Adapted experimental reference API; all validation remains in VertexHelper.
public static class VertexHelperExtension
{
    public static void AddVert(this VertexHelper helper,Vector3 position,Color32 color,Vector2 uv0,Vector2 uv1,Vector2 uv2,Vector2 uv3,Vector3 normal,Vector4 tangent) =>
        helper.AddVert(position,color,uv0,uv1,uv2,uv3,normal,tangent);
}
