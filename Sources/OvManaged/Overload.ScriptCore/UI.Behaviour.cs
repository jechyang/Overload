using System;

namespace Overload.EventSystems
{
    public abstract class UIBehaviour : Behaviour
    {
        [SerializeField] private bool m_Enabled = true;
        private Maths.Vector2? previousSize;
        private ulong previousParent, previousCanvas;
        public bool enabled {
            get => m_Enabled;
            set {
                if (m_Enabled == value) return;
                bool wasActive = IsActive(); m_Enabled = value;
                if (owner != null && Owner.ActiveInHierarchy) { if (value) OnEnable(); else if (wasActive) OnDisable(); }
                SetDirty();
            }
        }
        public RectTransform rectTransform => Owner.GetComponent<RectTransform>() ?? throw new InvalidOperationException("UI requires RectTransform");
        public virtual bool IsActive() => owner != null && enabled && Owner.ActiveInHierarchy;
        public virtual bool IsDestroyed() => owner == null;
        protected virtual void OnRectTransformDimensionsChange() => SetDirty();
        protected virtual void OnTransformParentChanged() => SetDirty();
        protected virtual void OnCanvasHierarchyChanged() => SetDirty();
        protected virtual void SetDirty() { if (owner != null) UI.LayoutRebuilder.MarkLayoutForRebuild(rectTransform); }
        internal virtual void EnsureNativeComponents() {
            Owner.AddComponent<RectTransform>();
            if (this is UI.CanvasScaler) Owner.AddComponent<Canvas>();
        }
        internal virtual void ReleaseNativeComponents() { }
        internal void MarkDirtyFromHost() => SetDirty();
        internal void NotifyNativeChanges() {
            if (owner == null) return;
            var size = rectTransform.rect.size;
            ulong parent = Owner.Parent?.Handle ?? 0, canvas = 0;
            for (Actor? cursor = Owner; cursor != null; cursor = cursor.Parent)
                if (cursor.GetComponent<Canvas>() != null) { canvas = cursor.Handle; break; }
            bool sizeChanged = !previousSize.HasValue || previousSize.Value != size;
            bool parentChanged = previousSize.HasValue && previousParent != parent;
            bool canvasChanged = !previousSize.HasValue || previousCanvas != canvas;
            previousSize = size; previousParent = parent; previousCanvas = canvas;
            if (parentChanged) OnTransformParentChanged();
            if (canvasChanged) OnCanvasHierarchyChanged();
            if (sizeChanged) OnRectTransformDimensionsChange();
        }
    }
}
namespace Overload.UI
{
    [AttributeUsage(AttributeTargets.Assembly)]
    public sealed class EnableUIAttribute : Attribute { }
    public interface ILayoutElement
    {
        void CalculateLayoutInputHorizontal();
        void CalculateLayoutInputVertical();
        float minWidth { get; }
        float preferredWidth { get; }
        float flexibleWidth { get; }
        float minHeight { get; }
        float preferredHeight { get; }
        float flexibleHeight { get; }
        int layoutPriority { get; }
    }
    public interface ILayoutController { void SetLayoutHorizontal(); void SetLayoutVertical(); }
    public interface ILayoutSelfController : ILayoutController { }
    public interface ILayoutGroup : ILayoutController { }
    public interface ILayoutIgnorer { bool ignoreLayout { get; } }
    public enum CanvasUpdate { Prelayout, Layout, PostLayout, PreRender, LatePreRender, MaxUpdateValue }
    public interface ICanvasElement
    {
        RectTransform transform { get; }
        void Rebuild(CanvasUpdate executing);
        void LayoutComplete();
        void GraphicUpdateComplete();
        bool IsDestroyed();
    }
    public static class LayoutUtility
    {
        internal static bool Active(object component) => component is EventSystems.UIBehaviour ui ? ui.IsActive() :
            component is Behaviour behaviour && behaviour.owner != null && behaviour.Owner.ActiveInHierarchy;
        public static float GetLayoutProperty(RectTransform rect, Func<ILayoutElement, float> property, float defaultValue) =>
            GetLayoutProperty(rect, property, defaultValue, out _);
        public static float GetLayoutProperty(RectTransform rect, Func<ILayoutElement, float> property, float defaultValue, out ILayoutElement? source) {
            source = null; int priority = int.MinValue; float value = defaultValue;
            foreach (ILayoutElement e in rect.Owner.GetComponents<ILayoutElement>()) {
                if (!Active(e) || e.layoutPriority < priority) continue;
                float candidate = property(e);
                if (candidate < 0) continue;
                if (!float.IsFinite(candidate)) throw new InvalidOperationException("Layout input must be finite");
                if (e.layoutPriority > priority || candidate > value) { value = candidate; priority = e.layoutPriority; source = e; }
            }
            return value;
        }
        public static float GetMinWidth(RectTransform r) => GetLayoutProperty(r, e => e.minWidth, 0);
        public static float GetMinHeight(RectTransform r) => GetLayoutProperty(r, e => e.minHeight, 0);
        public static float GetPreferredWidth(RectTransform r) => Math.Max(GetMinWidth(r), GetLayoutProperty(r, e => e.preferredWidth, 0));
        public static float GetPreferredHeight(RectTransform r) => Math.Max(GetMinHeight(r), GetLayoutProperty(r, e => e.preferredHeight, 0));
        public static float GetFlexibleWidth(RectTransform r) => GetLayoutProperty(r, e => e.flexibleWidth, 0);
        public static float GetFlexibleHeight(RectTransform r) => GetLayoutProperty(r, e => e.flexibleHeight, 0);
        public static float GetMinSize(RectTransform r, int axis) => axis == 0 ? GetMinWidth(r) : GetMinHeight(r);
        public static float GetPreferredSize(RectTransform r, int axis) => axis == 0 ? GetPreferredWidth(r) : GetPreferredHeight(r);
        public static float GetFlexibleSize(RectTransform r, int axis) => axis == 0 ? GetFlexibleWidth(r) : GetFlexibleHeight(r);
    }
}
