using System;
using System.Collections.Generic;
using Overload.Maths;

namespace Overload.UI;

public enum FontStyle { Normal, Bold, Italic, BoldAndItalic }
public enum HorizontalWrapMode { Wrap, Overflow }
public enum VerticalWrapMode { Truncate, Overflow }

/// <summary>Text layout settings. Native glyphs use a fixed em; fontSize only scales geometry.</summary>
public sealed class FontData
{
    public string font { get; set; } = ":Fonts/Roboto-Regular.ttf";
    public string[] fallbackFonts { get; set; } = Array.Empty<string>();
    public int fontSize { get; set; } = 14;
    public FontStyle fontStyle { get; set; }
    public float lineSpacing { get; set; } = 1;
    public TextAnchor alignment { get; set; }
    public bool richText { get; set; } = true;
    public HorizontalWrapMode horizontalOverflow { get; set; }
    public VerticalWrapMode verticalOverflow { get; set; }
    public bool bestFit { get; set; }
    public int minSize { get; set; } = 10;
    public int maxSize { get; set; } = 40;

    internal void Validate()
    {
        if (string.IsNullOrWhiteSpace(font) || fallbackFonts == null || fallbackFonts.Length > 16)
            throw new ArgumentException("Text requires a font and at most 16 fallbacks");
        foreach (string path in fallbackFonts) if (string.IsNullOrWhiteSpace(path)) throw new ArgumentException("Invalid fallback font");
        if (fontSize < 1 || fontSize > 4096 || minSize < 1 || maxSize > 4096 || minSize > maxSize ||
            !float.IsFinite(lineSpacing) || lineSpacing <= 0 || lineSpacing > 100 ||
            !Enum.IsDefined(fontStyle) || !Enum.IsDefined(alignment) || !Enum.IsDefined(horizontalOverflow) || !Enum.IsDefined(verticalOverflow))
            throw new ArgumentException("Invalid text layout settings");
    }
}

public readonly record struct TextFontMetrics(float ascender, float descender, float lineHeight);
public readonly record struct TextGlyph(uint glyphId, bool missing, float advance, Vector2 bearing,
    Rect plane, Rect uv, uint page, ulong generation, float distanceScale, float distanceBias);
public readonly record struct TextKerningPair(uint left, uint right);
public readonly record struct TextPageKey(ulong generation, uint page);

/// <summary>
/// One immutable native batch lease. The host adapter owns its page tokens and
/// releases them when Dispose is called, after the renderer has pinned GPU pages.
/// Font/FreeType pointers never enter managed code or a render worker.
/// </summary>
public sealed class TextFontBatch : IDisposable
{
    public string font { get; }
    public FontStyle style { get; }
    public ulong generation { get; }
    public TextFontMetrics metrics { get; }
    public IReadOnlyDictionary<uint, TextGlyph> glyphs { get; }
    private IDisposable? lease;
    public TextFontBatch(string font, FontStyle style, ulong generation, TextFontMetrics metrics,
        IReadOnlyDictionary<uint, TextGlyph> glyphs, IDisposable lease)
    {
        ArgumentNullException.ThrowIfNull(lease);
        this.font = font; this.style = style; this.generation = generation; this.metrics = metrics;
        this.glyphs = new System.Collections.ObjectModel.ReadOnlyDictionary<uint, TextGlyph>(new Dictionary<uint, TextGlyph>(glyphs));
        this.lease = lease;
    }
    public void Dispose() { lease?.Dispose(); lease = null; }
}

/// <summary>Owner-thread bulk resource boundary for the native Font bridge.</summary>
public interface ITextFontSource
{
    TextFontBatch EnsureGlyphs(string font, FontStyle style, IReadOnlyList<uint> scalars);
    IReadOnlyList<float> GetKerning(TextFontBatch batch, IReadOnlyList<TextKerningPair> pairs);
}

/// <summary>A contiguous painter-order range; equal pages separated by other pages stay separate.</summary>
public readonly record struct TextDrawRange(TextPageKey page, int firstIndex, int indexCount, float distanceScale, float distanceBias);
public readonly record struct TextCharacter(int utf16Index, int utf16Length, uint scalar, int line, Vector2 origin, float advance);

public sealed class TextLayoutResult : IDisposable
{
    public IReadOnlyList<UIVertex> vertices { get; }
    public IReadOnlyList<int> indices { get; }
    public IReadOnlyList<TextDrawRange> ranges { get; }
    public IReadOnlyList<TextCharacter> characters { get; }
    public Vector2 preferredSize { get; }
    public int fontSize { get; }
    public int lineCount { get; }
    public bool fits { get; }
    private readonly List<TextFontBatch> leases;
    internal IReadOnlyList<TextFontBatch> FontBatches => leases;
    internal TextLayoutResult(List<UIVertex> vertices, List<int> indices, List<TextDrawRange> ranges,
        List<TextCharacter> characters, Vector2 preferredSize, int fontSize, int lineCount, bool fits, List<TextFontBatch> leases)
    {
        this.vertices = vertices.AsReadOnly(); this.indices = indices.AsReadOnly(); this.ranges = ranges.AsReadOnly();
        this.characters = characters.AsReadOnly(); this.preferredSize = preferredSize;
        this.fontSize = fontSize; this.lineCount = lineCount; this.fits = fits; this.leases = leases;
    }
    public void Dispose() { foreach (var lease in leases) lease.Dispose(); leases.Clear(); }
}
