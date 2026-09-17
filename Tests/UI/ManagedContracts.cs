using System;
using System.Threading.Tasks;
using Overload;
using Overload.Maths;
using Overload.UI;

namespace UIFixture;

public class Left : LayoutElement
{
    public override void OnAwake() { minWidth = 30; preferredWidth = 50; flexibleWidth = 1; preferredHeight = 20; }
}
public class Right : LayoutElement
{
    public override void OnAwake() { minWidth = 40; preferredWidth = 100; flexibleWidth = 2; preferredHeight = 30; }
}
public class Scaler : CanvasScaler
{
    protected override float GetScreenDPI() => 144;
    public void Evaluate(Vector2 viewport) => Handle(viewport);
}
public class Column : VerticalLayoutGroup
{
    public override void OnAwake() { padding = new(2,3,4,5); spacing = 6; childForceExpandWidth = childForceExpandHeight = false; }
}
public class FittedPanel : ContentSizeFitter
{
    public override void OnAwake() { horizontalFit = verticalFit = FitMode.PreferredSize; }
}
public class Small : LayoutElement
{
    public override void OnAwake() { preferredWidth = 30; preferredHeight = 20; }
}
public class Large : LayoutElement
{
    public override void OnAwake() { preferredWidth = 40; preferredHeight = 50; }
}
public class RebuildObserver : Behaviour, ICanvasElement
{
    public int preRender, latePreRender;
    private bool destroyed;
    public RectTransform transform => Owner.GetComponent<RectTransform>()!;
    public override void OnAwake() => CanvasUpdateRegistry.RegisterCanvasElementForGraphicRebuild(this);
    public void Rebuild(CanvasUpdate stage) {
        if (MathF.Abs(Owner.Find("Panel")!.GetComponent<RectTransform>()!.rect.width - 45) > 0.001f)
            throw new Exception("Graphics callback ran before managed layout");
        if (stage == CanvasUpdate.PreRender) ++preRender;
        if (stage == CanvasUpdate.LatePreRender) ++latePreRender;
    }
    public bool IsDestroyed() => destroyed;
    public void LayoutComplete() { }
    public void GraphicUpdateComplete() { }
    public override void OnDestroy() { destroyed = true; }
    public override void OnReloadRelease() { destroyed = true; }
}
// Implementing an interface is enough; deriving from LayoutElement is optional.
public class ExtraPreferred : Behaviour, ILayoutElement
{
    public bool useOverride;
    public float minWidth => -1;
    public float preferredWidth => useOverride ? 80 : -1;
    public float flexibleWidth => -1;
    public float minHeight => -1;
    public float preferredHeight => -1;
    public float flexibleHeight => -1;
    public int layoutPriority => 5;
    public void CalculateLayoutInputHorizontal() { }
    public void CalculateLayoutInputVertical() { }
}
public class Probe : Behaviour
{
    public int passed;
    private RectTransform? retained;
    private static void Check(bool condition, string reason) { if (!condition) throw new Exception(reason); }
    private static bool Near(float a, float b) => MathF.Abs(a - b) < 0.002f;
    public override void OnAwake() { retained = Owner.Find("A")!.GetComponent<RectTransform>(); }
    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime == 2) {
            bool stale = false;
            try { _ = retained!.rect; } catch (Exception) { stale = true; }
            Check(stale, "deleted RectTransform facade must not revive after re-add");
            _ = Owner.Find("A")!.GetComponent<RectTransform>()!.rect;
            passed = 2; return;
        }
        if (deltaTime != 1) return;
        Actor root = Owner.Find("Canvas")!;
        var group = root.GetComponent<ScriptUI.CustomHorizontalLayout>()!;
        var a = Owner.Find("A")!.GetComponent<RectTransform>()!;
        var b = Owner.Find("B")!.GetComponent<RectTransform>()!;
        var left = a.Owner.GetComponent<LayoutElement>()!;
        var right = b.Owner.GetComponent<LayoutElement>()!;
        Check(group.horizontalPasses == 1, "custom virtual layout called by native frame extraction");
        var panel = Owner.Find("Panel")!.GetComponent<RectTransform>()!;
        Check(Near(panel.rect.width,45) && Near(panel.rect.height,85), "C# vertical group and self controller ordering");
        Check(Near(Owner.Find("Large")!.GetComponent<RectTransform>()!.anchoredPosition.y,-80), "top-down fitted child placement");
        var observer = Owner.Find("Observer")!.GetComponent<RebuildObserver>()!;
        Check(observer.preRender==1 && observer.latePreRender==1, "user graphic rebuild callbacks after layout");
        var graphic = Owner.Find("Triangle")!.GetComponent<TriangleGraphic>()!;
        ulong generation = graphic.canvasRenderer.meshGeneration;
        graphic.enabled = false;
        Check(graphic.canvasRenderer.vertexCount == 0, "disabling Graphic clears native CanvasRenderer geometry");
        graphic.enabled = true; graphic.SetVerticesDirty();
        Check(graphic.canvasRenderer.meshGeneration > generation, "Graphic clear advances the native mesh generation");
        Check(Near(a.rect.width, 90) && Near(b.rect.width, 180), "managed weighted flexible sizing");
        Check(root.GetComponents<ILayoutController>().Length == 1, "managed interface component lookup");
        Check(a.Owner.Parent!.Guid == root.Guid && root.GetChild(1).Guid == b.Owner.Guid, "native hierarchy API");
        var extra = a.Owner.GetComponent<ExtraPreferred>()!;
        extra.useOverride = true;
        Check(Near(LayoutUtility.GetPreferredWidth(a), 80), "user ILayoutElement priority");
        extra.useOverride = false;
        Check(Near(LayoutUtility.GetPreferredWidth(a), 50), "per-property negative fallback");
        right.ignoreLayout = true;
        LayoutRebuilder.ForceRebuildLayoutImmediate(root.GetComponent<RectTransform>()!);
        Check(Near(a.rect.width, 280), "ignore layout");
        right.ignoreLayout = false;
        right.enabled = false;
        Check(Near(LayoutUtility.GetPreferredWidth(b), 0), "disabled layout provider");
        right.enabled = true;
        left.flexibleWidth = right.flexibleWidth = 0;
        group.childAlignment = TextAnchor.MiddleCenter;
        LayoutRebuilder.ForceRebuildLayoutImmediate(group.rectTransform);
        Check(Near(a.anchoredPosition.x, 70) && Near(a.anchoredPosition.y, -110), "managed alignment and authored pivot");
        group.childControlWidth = false;
        a.sizeDelta = new(23,20);
        LayoutRebuilder.ForceRebuildLayoutImmediate(group.rectTransform);
        Check(Near(a.sizeDelta.x,23), "uncontrolled child size");
        group.childControlWidth = true; group.childAlignment = TextAnchor.UpperLeft;
        left.flexibleWidth = 1; right.flexibleWidth = 2;
        var scaler = root.GetComponent<Scaler>()!;
        var canvas = root.GetComponent<Canvas>()!;
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
        scaler.referenceResolution = new(640,480); scaler.matchWidthOrHeight = 0.5f;
        scaler.Evaluate(new(480,640)); Check(Near(canvas.scaleFactor,1), "log screen match");
        scaler.screenMatchMode = CanvasScaler.ScreenMatchMode.Expand;
        scaler.Evaluate(new(480,640)); Check(Near(canvas.scaleFactor,0.75f), "expand mode");
        scaler.screenMatchMode = CanvasScaler.ScreenMatchMode.Shrink;
        scaler.Evaluate(new(480,640)); Check(Near(canvas.scaleFactor,4f/3), "shrink mode");
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ConstantPhysicalSize;
        float[] units = [2.54f,25.4f,1,72,6];
        for (int i=0;i<5;++i) {
            scaler.physicalUnit = (CanvasScaler.Unit)i; scaler.Evaluate(new(300,200));
            Check(Near(canvas.scaleFactor,144/units[i]), "virtual DPI and physical unit");
        }
        scaler.enabled = false; Check(Near(canvas.scaleFactor,1), "scaler disable resets canvas");
        scaler.uiScaleMode = CanvasScaler.ScaleMode.ConstantPixelSize; scaler.scaleFactor = 1; scaler.enabled = true;
        scaler.Evaluate(new(300,200));
        canvas.renderMode = RenderMode.WorldSpace; canvas.planeDistance = 12;
        scaler.dynamicPixelsPerUnit = 3; scaler.Evaluate(new(300,200));
        Check(canvas.renderMode == RenderMode.WorldSpace && Near(canvas.planeDistance,12) && Near(canvas.scaleFactor,3),
            "managed canvas modes, plane distance and world scaler");
        canvas.renderMode = RenderMode.ScreenSpaceOverlay; scaler.dynamicPixelsPerUnit = 1; scaler.Evaluate(new(300,200));
        bool workerRejected = Task.Run(() => { try { _ = a.rect; return false; } catch (Exception) { return true; } }).GetAwaiter().GetResult();
        Check(workerRejected, "UI native wrappers reject worker thread access");
        passed = 1;
    }
}

// Project code owns geometry generation. Editing this method is picked up by
// the existing project hot-reload path without rebuilding native code.
public class TriangleGraphic : Graphic
{
    public int populations;
    protected override void OnPopulateMesh(VertexHelper helper)
    {
        helper.Clear();
        var rect = rectTransform.rect;
        helper.AddVert(new Vector3(rect.min.x, rect.min.y, 0), new Color32(255, 0, 0, 255), new Vector2(0, 0));
        helper.AddVert(new Vector3(rect.max.x, rect.min.y, 0), new Color32(0, 255, 0, 255), new Vector2(1, 0));
        helper.AddVert(new Vector3((rect.min.x + rect.max.x) * 0.5f, rect.max.y, 0), new Color32(0, 0, 255, 255), new Vector2(0.5f, 1));
        helper.AddTriangle(0, 1, 2);
        ++populations;
    }
}

public class TexturedImage : Image
{
    public TexturedImage()
    {
        sprite = new AssetRef(AssetKind.Texture, "Textures/Checker.png");
        material = new AssetRef(AssetKind.Material, "Materials/UI.mat");
    }
}

public class TexturedRawImage : RawImage
{
    public TexturedRawImage()
    {
        texture = new AssetRef(AssetKind.Texture, "Textures/Checker.png");
        uvRect = new Rect(new Vector2(0.25f, 0.5f), new Vector2(0.5f, 0.25f));
    }
}
