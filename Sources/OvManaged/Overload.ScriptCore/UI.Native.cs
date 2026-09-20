using System;
using Overload.Maths;

namespace Overload.Maths
{
    public record struct Vector2(float x, float y)
    {
        public float this[int axis] {
            readonly get => axis == 0 ? x : axis == 1 ? y : throw new ArgumentOutOfRangeException(nameof(axis));
            set { if (axis == 0) x = value; else if (axis == 1) y = value; else throw new ArgumentOutOfRangeException(nameof(axis)); }
        }
        public static Vector2 zero => new(0, 0);
        public static Vector2 one => new(1, 1);
        public static Vector2 operator +(Vector2 a, Vector2 b) => new(a.x + b.x, a.y + b.y);
        public static Vector2 operator -(Vector2 a, Vector2 b) => new(a.x - b.x, a.y - b.y);
        public static Vector2 operator *(Vector2 a, float b) => new(a.x * b, a.y * b);
        public static Vector2 operator /(Vector2 a, float b) => new(a.x / b, a.y / b);
    }
    public readonly record struct Rect(Vector2 min, Vector2 size)
    {
        public Vector2 max => min + size;
        public float width => size.x;
        public float height => size.y;
        public bool Contains(Vector2 point) => point.x >= min.x && point.y >= min.y && point.x < max.x && point.y < max.y;
    }
}
namespace Overload
{
    public enum RenderMode { ScreenSpaceOverlay, ScreenSpaceCamera, WorldSpace }

    public abstract class NativeUIComponent
    {
        public Actor Owner { get; }
        internal ulong Handle { get; }
        internal NativeUIComponent(Actor owner, ulong handle) { Owner = owner; Handle = handle; }
        internal static NativeUIComponent? Resolve(Actor owner, Type type, bool create) {
            ulong kind = type == typeof(RectTransform) ? 1ul : type == typeof(Canvas) ? 2ul :
                type == typeof(CanvasRenderer) ? 3ul : type == typeof(NativeCanvasGroup) ? 4ul : type == typeof(NativeRectMask) ? 5ul :
                throw new NotSupportedException("Unknown native UI component: " + type.FullName);
            ulong handle = owner.Access.Query(owner.Handle, create ? 31u : 30u, kind);
            return handle == 0 ? null : kind == 1 ? new RectTransform(owner, handle) :
                kind == 2 ? new Canvas(owner, handle) : kind == 3 ? new CanvasRenderer(owner, handle) :
                kind == 4 ? new NativeCanvasGroup(owner,handle) : new NativeRectMask(owner,handle);
        }
        internal void ValidateReference() {
            if(this is RectTransform)Owner.Access.Query(Handle,40,0,"sizeDelta");
            else if(this is Canvas)Owner.Access.Query(Handle,42,0,"scaleFactor");
            else if(this is CanvasRenderer)Owner.Access.Query(Handle,48,0,"generation");
            else if(this is NativeCanvasGroup)Owner.Access.Query(Handle,49,0,"flags");
            else if(this is NativeRectMask)Owner.Access.Query(Handle,51);
            else throw new NotSupportedException("Unknown native UI component");
        }
        internal static ulong Pack(Vector2 value) => BitConverter.SingleToUInt32Bits(value.x) | ((ulong)BitConverter.SingleToUInt32Bits(value.y) << 32);
        internal static Vector2 Unpack(ulong value) => new(BitConverter.UInt32BitsToSingle((uint)value), BitConverter.UInt32BitsToSingle((uint)(value >> 32)));
    }
    public sealed class RectTransform : NativeUIComponent
    {
        public enum Axis { Horizontal, Vertical }
        public enum Edge { Left, Right, Top, Bottom }
        internal RectTransform(Actor owner, ulong handle) : base(owner, handle) { }
        private Vector2 Get(string name) => Unpack(Owner.Access.Query(Handle, 40, 0, name));
        private void Set(string name, Vector2 value) {
            if(Get(name)==value)return;
            Owner.Access.Query(Handle, 41, Pack(value), name);
            UI.LayoutRebuilder.MarkLayoutForRebuild(this);
        }
        public Vector2 anchorMin { get => Get("anchorMin"); set => Set("anchorMin", value); }
        public Vector2 anchorMax { get => Get("anchorMax"); set => Set("anchorMax", value); }
        public Vector2 pivot { get => Get("pivot"); set => Set("pivot", value); }
        public Vector2 anchoredPosition { get => Get("anchoredPosition"); set => Set("anchoredPosition", value); }
        public Vector2 sizeDelta { get => Get("sizeDelta"); set => Set("sizeDelta", value); }
        public Vector2 offsetMin { get => Get("offsetMin"); set => Set("offsetMin", value); }
        public Vector2 offsetMax { get => Get("offsetMax"); set => Set("offsetMax", value); }
        public Rect rect { get { Vector2 size = Get("rectSize"), p = pivot; return new(new(-p.x * size.x, -p.y * size.y), size); } }
        public RectTransform? parent => Owner.Parent?.GetComponent<RectTransform>();
        public void SetSizeWithCurrentAnchors(Axis axis, float size) {
            int i = (int)axis;
            Vector2 value = sizeDelta;
            value[i] = size - (parent?.rect.size[i] ?? 0) * (anchorMax[i] - anchorMin[i]);
            sizeDelta = value;
        }
        public void SetInsetAndSizeFromParentEdge(Edge edge, float inset, float size) {
            if (edge < Edge.Left || edge > Edge.Bottom) throw new ArgumentOutOfRangeException(nameof(edge));
            int axis = edge is Edge.Left or Edge.Right ? 0 : 1;
            bool far = edge is Edge.Right or Edge.Top;
            Vector2 low = anchorMin, high = anchorMax, delta = sizeDelta, position = anchoredPosition;
            low[axis] = high[axis] = far ? 1 : 0;
            delta[axis] = size;
            position[axis] = far ? -inset - size * (1 - pivot[axis]) : inset + size * pivot[axis];
            anchorMin = low; anchorMax = high; sizeDelta = delta; anchoredPosition = position;
        }
        public void GetLocalCorners(Vector2[] corners) {
            if (corners.Length < 4) throw new ArgumentException("Four corners required", nameof(corners));
            Rect r = rect;
            corners[0] = r.min; corners[1] = new(r.min.x, r.max.y); corners[2] = r.max; corners[3] = new(r.max.x, r.min.y);
        }
    }
    public sealed class CanvasRenderer : NativeUIComponent
    {
        internal CanvasRenderer(Actor owner, ulong handle) : base(owner, handle) { }
        public void SetMesh(UI.UIVertex[] vertices, int[] indices) => Owner.Access.SetUIMesh(Handle, vertices, indices);
        public void Clear() => Owner.Access.Query(Handle, 44);
        public void SetTexture(AssetRef value, int index = 0) {
            if (value.Kind is not (AssetKind.Unknown or AssetKind.Texture)) throw new ArgumentException("CanvasRenderer texture must be a Texture AssetRef", nameof(value));
            Owner.Access.Query(Handle, 46, Slot(AssetKind.Texture,index,false), value.Path ?? "");
        }
        public void SetMaterial(AssetRef value, int index = 0) {
            if (value.Kind is not (AssetKind.Unknown or AssetKind.Material)) throw new ArgumentException("CanvasRenderer material must be a Material AssetRef", nameof(value));
            Owner.Access.Query(Handle, 46, Slot(AssetKind.Material,index,false), value.Path ?? "");
        }
        private static ulong Slot(AssetKind kind, int index, bool pop) {
            if ((uint)index >= 64) throw new ArgumentOutOfRangeException(nameof(index));
            return (ulong)kind | ((ulong)index << 8) | (pop ? 1ul << 24 : 0);
        }
        public void SetMesh(UI.UIMesh mesh) { ArgumentNullException.ThrowIfNull(mesh); SetMesh(mesh.vertices,mesh.indices); }
        public int materialCount { get => checked((int)Owner.Access.Query(Handle,48,0,"materialCount")); set => Owner.Access.Query(Handle,47,checked((ulong)value),"materialCount"); }
        public int popMaterialCount { get => checked((int)Owner.Access.Query(Handle,48,0,"popMaterialCount")); set => Owner.Access.Query(Handle,47,checked((ulong)value),"popMaterialCount"); }
        public bool cull { get => Owner.Access.Query(Handle,48,0,"cull")!=0; set => Owner.Access.Query(Handle,47,value?1ul:0ul,"cull"); }
        public int absoluteDepth => unchecked((int)Owner.Access.Query(Handle,48,0,"depth"));
        public void SetPopMaterial(AssetRef value, int index) {
            if(value.Kind is not (AssetKind.Unknown or AssetKind.Material))throw new ArgumentException("Expected material");
            Owner.Access.Query(Handle,46,Slot(AssetKind.Material,index,true),value.Path??"");
        }
        public void SetIndexRange(int slot, int firstIndex, int indexCount) {
            if((uint)slot>=64 || firstIndex<0 || indexCount<0)throw new ArgumentOutOfRangeException(nameof(slot));
            Owner.Access.Query(Handle,47,(ulong)(uint)firstIndex | ((ulong)(uint)indexCount<<32),"range:"+slot);
        }
        public void SetColor(UI.Color value) {
            if(!float.IsFinite(value.r)||!float.IsFinite(value.g)||!float.IsFinite(value.b)||!float.IsFinite(value.a))throw new ArgumentException("Color must be finite");
            Owner.Access.Query(Handle,47,Pack(new(value.r,value.g)),"colorRG");
            Owner.Access.Query(Handle,47,Pack(new(value.b,value.a)),"colorBA");
        }
        public UI.Color GetColor() { var rg=Unpack(Owner.Access.Query(Handle,48,0,"colorRG"));var ba=Unpack(Owner.Access.Query(Handle,48,0,"colorBA"));return new(rg.x,rg.y,ba.x,ba.y); }
        public void SetStencil(UI.UIStencilState state) => Owner.Access.Query(Handle,47,state.Packed,"stencil");
        public void SetPopStencil(UI.UIStencilState state) => Owner.Access.Query(Handle,47,state.Packed,"popStencil");
        public void SetAlpha(float alpha) { var c=GetColor();SetColor(new(c.r,c.g,c.b,alpha)); }
        public float GetAlpha() => GetColor().a;
        /// <summary>Clip in renderer-local coordinates, converted during immutable frame extraction.</summary>
        public void EnableRectClipping(Rect rect) {
            if(!float.IsFinite(rect.min.x)||!float.IsFinite(rect.min.y)||!float.IsFinite(rect.size.x)||!float.IsFinite(rect.size.y))throw new ArgumentException("Clip must be finite");
            Owner.Access.Query(Handle,47,Pack(rect.min),"clipMin");Owner.Access.Query(Handle,47,Pack(rect.size),"clipSize");
        }
        internal void IgnoreParentRectMasks(bool value) => Owner.Access.Query(Handle,47,value?1ul:0ul,"ignoreParentMasks");
        public void DisableRectClipping() => Owner.Access.Query(Handle,47,0,"disableClip");
        internal void SetRaycastTarget(bool value) => Owner.Access.Query(Handle, 47, value ? 1ul : 0ul);
        public int vertexCount => checked((int)Owner.Access.Query(Handle, 48, 0, "vertices"));
        public int indexCount => checked((int)Owner.Access.Query(Handle, 48, 0, "indices"));
        public ulong meshGeneration => Owner.Access.Query(Handle, 48, 0, "generation");
    }
    internal sealed class NativeCanvasGroup : NativeUIComponent
    {
        internal NativeCanvasGroup(Actor owner,ulong handle):base(owner,handle){}
        internal void Clear() => Owner.Access.Query(Handle,50,0,"release");
        internal void Set(float alpha,bool interactable,bool blocks,bool ignore,bool active){
            Owner.Access.Query(Handle,50,Pack(new(alpha,0)),"alpha");
            Owner.Access.Query(Handle,50,(interactable?1ul:0ul)|(blocks?2ul:0ul)|(ignore?4ul:0ul)|(active?8ul:0ul),"flags");
        }
    }
    internal sealed class NativeRectMask : NativeUIComponent
    {
        internal NativeRectMask(Actor owner,ulong handle):base(owner,handle){}
        internal bool enabled {get=>Owner.Access.Query(Handle,51)!=0;set=>Owner.Access.Query(Handle,52,value?1ul:0ul);}
    }
    public sealed class Canvas : NativeUIComponent
    {
        internal Canvas(Actor owner, ulong handle) : base(owner, handle) { }
        public RenderMode renderMode {
            get => (RenderMode)Owner.Access.Query(Handle, 42, 0, "renderMode");
            set => Owner.Access.Query(Handle, 43, checked((ulong)value), "renderMode");
        }
        public float planeDistance {
            get => Unpack(Owner.Access.Query(Handle, 42, 0, "planeDistance")).x;
            set => Owner.Access.Query(Handle, 43, Pack(new(value, 0)), "planeDistance");
        }
        // Overload exposes the camera's owning Actor until a general native Camera facade is available.
        public Actor? worldCamera {
            get { ulong guid = Owner.Access.Query(Handle, 42, 0, "worldCamera"); return guid == 0 ? null : Owner.Find(guid); }
            set => Owner.Access.Query(Handle, 43, value?.Guid ?? 0, "worldCamera");
        }
        public float scaleFactor {
            get => Unpack(Owner.Access.Query(Handle, 42, 0, "scaleFactor")).x;
            set => Owner.Access.Query(Handle, 43, Pack(new(value, 0)), "scaleFactor");
        }
        public int sortingOrder {
            get => unchecked((int)Owner.Access.Query(Handle, 42, 0, "sortingOrder"));
            set => Owner.Access.Query(Handle, 43, unchecked((ulong)(long)value), "sortingOrder");
        }
        public bool overrideSorting {
            get => Owner.Access.Query(Handle, 42, 0, "overrideSorting") != 0;
            set => Owner.Access.Query(Handle, 43, value ? 1ul : 0ul, "overrideSorting");
        }
        public bool isRootCanvas {
            get { for (Actor? actor = Owner.Parent; actor != null; actor = actor.Parent) if (actor.GetComponent<Canvas>() != null) return false; return true; }
        }
    }
}
