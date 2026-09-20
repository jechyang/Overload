using System;
using System.Collections.Generic;
using System.Linq;
using Overload.Maths;

namespace Overload.UI;

public class Text : MaskableGraphic, ILayoutElement
{
    [SerializeField] private string m_Text = "";
    [SerializeField] private AssetRef m_Font = new(AssetKind.Font, "");
    [SerializeField] private string m_FallbackFonts = "";
    [SerializeField] private int m_FontSize = 14, m_BestFitMinSize = 10, m_BestFitMaxSize = 40;
    [SerializeField] private FontStyle m_FontStyle;
    [SerializeField] private float m_LineSpacing = 1;
    [SerializeField] private TextAnchor m_Alignment;
    [SerializeField] private HorizontalWrapMode m_HorizontalOverflow;
    [SerializeField] private VerticalWrapMode m_VerticalOverflow;
    [SerializeField] private bool m_RichText = true, m_BestFit;
    [NonSerialized] private NativeTextFontSource? source;
    [NonSerialized] private TextLayoutResult? lastLayout;
    [NonSerialized] private IDisposable? fontRegistration;
    [NonSerialized] private bool measurementDirty = true;
    [NonSerialized] private float measuredWidth, measuredHeight;
    [NonSerialized] private readonly Dictionary<string, ulong> fontVersions = new();
    [NonSerialized] private readonly List<(TextDrawRange range, int start, int count)> drawRanges = new();

    private void Change<T>(ref T field, T value)
    { if (EqualityComparer<T>.Default.Equals(field, value)) return; field = value; SetAllDirty(); }
    public virtual string text { get => m_Text; set => Change(ref m_Text, value ?? ""); }
    /// <summary>An empty font path selects the engine's built-in Roboto font.</summary>
    public AssetRef font {
        get => m_Font;
        set { if (value.Kind is not (AssetKind.Font or AssetKind.Unknown)) throw new ArgumentException("Text font requires a Font AssetRef"); Change(ref m_Font, value); }
    }
    // A newline-delimited backing string uses the existing string field ABI and
    // round-trips before the shared generic list serializer is integrated.
    public string[] fallbackFonts {
        get => m_FallbackFonts.Split('\n', StringSplitOptions.RemoveEmptyEntries);
        set {
            ArgumentNullException.ThrowIfNull(value);
            if (value.Length > 16 || value.Any(p => string.IsNullOrWhiteSpace(p) || p.Contains('\n') || p.Contains('\r')))
                throw new ArgumentException("Invalid fallback font paths");
            Change(ref m_FallbackFonts, string.Join('\n', value));
        }
    }
    public int fontSize { get => m_FontSize; set { if (value is < 1 or > 4096) throw new ArgumentOutOfRangeException(nameof(value)); Change(ref m_FontSize, value); } }
    public FontStyle fontStyle { get => m_FontStyle; set { if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value)); Change(ref m_FontStyle, value); } }
    public float lineSpacing { get => m_LineSpacing; set { if (!float.IsFinite(value) || value <= 0 || value > 100) throw new ArgumentOutOfRangeException(nameof(value)); Change(ref m_LineSpacing, value); } }
    public TextAnchor alignment { get => m_Alignment; set { if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value)); Change(ref m_Alignment, value); } }
    public HorizontalWrapMode horizontalOverflow { get => m_HorizontalOverflow; set { if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value)); Change(ref m_HorizontalOverflow, value); } }
    public VerticalWrapMode verticalOverflow { get => m_VerticalOverflow; set { if (!Enum.IsDefined(value)) throw new ArgumentOutOfRangeException(nameof(value)); Change(ref m_VerticalOverflow, value); } }
    public bool supportRichText { get => m_RichText; set => Change(ref m_RichText, value); }
    public bool resizeTextForBestFit { get => m_BestFit; set => Change(ref m_BestFit, value); }
    public int resizeTextMinSize { get => m_BestFitMinSize; set { if (value is < 1 or > 4096) throw new ArgumentOutOfRangeException(nameof(value)); Change(ref m_BestFitMinSize, value); } }
    public int resizeTextMaxSize { get => m_BestFitMaxSize; set { if (value is < 1 or > 4096) throw new ArgumentOutOfRangeException(nameof(value)); Change(ref m_BestFitMaxSize, value); } }
    public int fontSizeUsedForBestFit => lastLayout?.fontSize ?? fontSize;
    public IReadOnlyList<TextCharacter> characters => lastLayout?.characters ?? Array.Empty<TextCharacter>();
    public FontData fontData => new() {
        font = EffectiveFontPath, fallbackFonts = fallbackFonts, fontSize = fontSize, fontStyle = fontStyle,
        lineSpacing = lineSpacing, alignment = alignment, horizontalOverflow = horizontalOverflow,
        verticalOverflow = verticalOverflow, richText = supportRichText, bestFit = resizeTextForBestFit,
        minSize = resizeTextMinSize, maxSize = resizeTextMaxSize
    };
    internal string EffectiveFontPath => string.IsNullOrEmpty(font.Path) ? ":Fonts/Roboto-Regular.ttf" : font.Path;
    private NativeTextFontSource Source => source ??= new NativeTextFontSource(Owner);
    public override void SetVerticesDirty() { measurementDirty = true; base.SetVerticesDirty(); }
    public override void SetLayoutDirty() { measurementDirty = true; base.SetLayoutDirty(); }
    public override void OnEnable() { base.OnEnable(); fontRegistration ??= FontUpdateTracker.TrackText(this); }
    internal override void EnsureNativeComponents()
    { base.EnsureNativeComponents(); if (IsActive()) fontRegistration ??= FontUpdateTracker.TrackText(this); }
    internal override void ReleaseNativeComponents()
    {
        fontRegistration?.Dispose(); fontRegistration = null;
        lastLayout?.Dispose(); lastLayout = null; source = null; drawRanges.Clear(); fontVersions.Clear(); measurementDirty = true;
        base.ReleaseNativeComponents();
    }
    protected override void UpdateGeometry()
    {
        drawRanges.Clear();
        if (string.IsNullOrEmpty(text))
        { canvasRenderer.Clear(); lastLayout?.Dispose(); lastLayout = null; fontVersions.Clear(); return; }
        var generated = TextLayout.Generate(text, fontData, rectTransform.rect, color, Source);
        try
        {
            if (generated.ranges.Count > 64) throw new InvalidOperationException("Text exceeds CanvasRenderer's 64 ordered material ranges");
            using var combined = new VertexHelper();
            foreach (var range in generated.ranges)
            {
                using var segment = new VertexHelper();
                // Per-range modifiers preserve page ownership, SDF channels and
                // the painter order of fallback/combining glyphs.
                for (int i = range.firstIndex; i < range.firstIndex + range.indexCount; i += 3)
                {
                    int start = segment.currentVertCount;
                    segment.AddVert(generated.vertices[generated.indices[i]]);
                    segment.AddVert(generated.vertices[generated.indices[i + 1]]);
                    segment.AddVert(generated.vertices[generated.indices[i + 2]]);
                    segment.AddTriangle(start, start + 1, start + 2);
                }
                foreach (var modifier in Owner.GetComponents<IMeshModifier>()) if (LayoutUtility.Active(modifier)) modifier.ModifyMesh(segment);
                drawRanges.Add((range, combined.currentIndexCount, segment.currentIndexCount));
                combined.AddUIVertexStream(segment.Vertices(), segment.Indices());
            }
            canvasRenderer.SetMesh(combined.Vertices(), combined.Indices());
            canvasRenderer.materialCount = drawRanges.Count;
            for (int i = 0; i < drawRanges.Count; ++i)
            {
                canvasRenderer.SetIndexRange(i, drawRanges[i].start, drawRanges[i].count);
                Source.Bind(canvasRenderer, drawRanges[i].range.page, i);
            }
            fontVersions.Clear();
            foreach (var batch in generated.FontBatches) fontVersions[batch.font] = batch.generation;
            lastLayout?.Dispose(); lastLayout = generated;
            SetMaterialDirty();
        }
        finally { generated.Dispose(); } // Renderer now pins each native page independently.
    }
    protected override void UpdateMaterial()
    {
        canvasRenderer.materialCount = drawRanges.Count;
        for (int i = 0; i < drawRanges.Count; ++i) canvasRenderer.SetMaterial(materialForRendering, i);
        canvasRenderer.SetRaycastTarget(raycastTarget);
        RecalculateClipping(); MaskUtilities.ApplyStencil(this);
    }
    internal void CheckFontUpdates(Dictionary<string, ulong> frameVersions)
    {
        foreach (var entry in fontVersions)
        {
            if (!frameVersions.TryGetValue(entry.Key, out ulong generation))
            {
                using var current = Source.EnsureGlyphs(entry.Key, FontStyle.Normal, Array.Empty<uint>());
                frameVersions.Add(entry.Key, generation = current.generation);
            }
            if (generation != entry.Value) { SetAllDirty(); break; }
        }
    }
    private void Measure()
    {
        if (!measurementDirty) return;
        measuredWidth = measuredHeight = 0;
        if (!string.IsNullOrEmpty(text))
        {
            var data = fontData; data.bestFit = false; data.horizontalOverflow = HorizontalWrapMode.Overflow; data.verticalOverflow = VerticalWrapMode.Overflow;
            using (var unwrapped = TextLayout.Generate(text, data, rectTransform.rect, color, Source))
                measuredWidth = unwrapped.preferredSize.x;
            data.horizontalOverflow = horizontalOverflow;
            using var wrapped = TextLayout.Generate(text, data, rectTransform.rect, color, Source);
            measuredHeight = wrapped.preferredSize.y;
        }
        measurementDirty = false;
    }
    public virtual void CalculateLayoutInputHorizontal() => Measure();
    public virtual void CalculateLayoutInputVertical() => Measure();
    public virtual float minWidth => 0;
    public virtual float preferredWidth { get { Measure(); return measuredWidth; } }
    public virtual float flexibleWidth => -1;
    public virtual float minHeight => 0;
    public virtual float preferredHeight { get { Measure(); return measuredHeight; } }
    public virtual float flexibleHeight => -1;
    public virtual int layoutPriority => 0;
}

public static class FontUpdateTracker
{
    public static IDisposable TrackText(Text text) => UISessionRegistry<Text>.For(text.Owner).Register(text, text);
    public static void RebuildForFont(Actor owner, string font)
    {
        foreach (var text in UISessionRegistry<Text>.For(owner).Snapshot())
            if (text.IsActive() && (text.EffectiveFontPath == font || Array.IndexOf(text.fallbackFonts, font) >= 0)) text.SetAllDirty();
    }
    // Called once by the session UI update, including edit-mode first frames.
    internal static void Update(Actor sceneAnchor)
    {
        var generations = new Dictionary<string, ulong>();
        foreach (var text in UISessionRegistry<Text>.For(sceneAnchor).Snapshot())
            if (text.IsActive() && sceneAnchor.Access.Query(sceneAnchor.Handle, 19, text.Owner.Handle) != 0) text.CheckFontUpdates(generations);
    }
}
