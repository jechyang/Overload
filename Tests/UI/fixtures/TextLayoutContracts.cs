using System;
using System.Collections.Generic;
using System.Linq;
using Overload.UI;
using Overload.Maths;

internal static class TextLayoutContracts
{
    private static int checks;
    private static void Check(bool value, string message) { ++checks; if (!value) throw new Exception(message); }
    private sealed class Lease(Action release) : IDisposable { public void Dispose() => release(); }
    private sealed class Fonts : ITextFontSource
    {
        internal int Requests, Kernings, Live;
        internal readonly List<(string path, FontStyle style, uint[] points)> Calls = new();
        internal bool FailKerning;
        public TextFontBatch EnsureGlyphs(string font, FontStyle style, IReadOnlyList<uint> scalars)
        {
            ++Requests; ++Live; Calls.Add((font, style, scalars.ToArray()));
            Check(scalars.Count == scalars.Distinct().Count(), "Bulk request is deduplicated");
            ulong generation = (font == "primary" ? 1ul : 2ul) + (ulong)style * 10;
            var glyphs = new Dictionary<uint, TextGlyph>();
            foreach (uint cp in scalars)
            {
                bool missing = font == "primary" ? cp > 127 : cp is not (0x4e2d or 0x6587 or 0x1f600 or 0x301 or 0xfffd);
                float advance = cp == 32 ? .25f : .5f;
                glyphs.Add(cp, new(cp, missing, advance, new(0, .8f), new(new(0, -.2f), new(.5f, 1)),
                    new(new(0, 0), new(1, 1)), cp == 32 ? uint.MaxValue : cp % 2, generation, 16, -8));
            }
            return new(font, style, generation, new(.8f, -.2f, 1), glyphs, new Lease(() => --Live));
        }
        public IReadOnlyList<float> GetKerning(TextFontBatch batch, IReadOnlyList<TextKerningPair> pairs)
        {
            ++Kernings;
            if (FailKerning) throw new Exception("Injected font failure");
            Check(pairs.Count == pairs.Distinct().Count(), "Kerning pairs are deduplicated");
            return pairs.Select(p => p.left == 'A' && p.right == 'V' ? -.1f : 0).ToArray();
        }
    }
    private static FontData Settings() => new() { font = "primary", fallbackFonts = new[] { "fallback" }, fontSize = 20 };
    private static TextLayoutResult Generate(string text, Fonts fonts, FontData? data = null, float width = 200, float height = 100)
        => TextLayout.Generate(text, data ?? Settings(), new(new(-20, -10), new(width, height)), Color.white, fonts);

    public static void Main()
    {
        var fonts = new Fonts();
        using (var layout = Generate("AVAV", fonts))
        {
            Check(layout.characters.Count == 4 && layout.vertices.Count == 16 && layout.indices.Count == 24, "Managed quad generation");
            Check(Math.Abs(layout.preferredSize.x - 36) < .001f, "Kerning applied to scaled metrics");
            Check(fonts.Requests == 1 && fonts.Kernings == 1, "One bulk crossing per font/style for glyphs and kerning");
            Check(layout.ranges.Count == 4, "Painter order preserved across alternating pages");
        }
        Check(fonts.Live == 0, "Layout disposal releases font leases");
        using (var layout = Generate("A中文😀e\u0301\ud800", fonts))
        {
            Check(layout.characters.Count == 7, "Unicode scalars and invalid surrogate replacement");
            Check(layout.characters[3].utf16Length == 2 && layout.characters[3].scalar == 0x1f600, "Astral scalar retains UTF16 range");
            Check(layout.characters[5].advance == 0 && layout.characters[5].origin == layout.characters[4].origin, "Combining mark attaches to base");
            Check(layout.ranges.Any(r => r.page.generation == 2), "Fallback glyphs use their own native generation");
        }
        using (var layout = Generate("AA BB", fonts, width: 25))
        { Check(layout.lineCount == 2, "Word wrap"); Check(layout.characters[3].line == 1, "Second word moves as a unit"); }
        using (var layout = Generate("AA  BB", fonts, width: 20)) Check(layout.lineCount == 2, "Overflowing whitespace does not create phantom lines");
        using (var layout = Generate("ABCD", fonts, width: 5)) Check(layout.lineCount == 4, "Long word scalar wrapping makes progress");
        using (var layout = Generate("中文中文", fonts, width: 20)) Check(layout.lineCount == 2, "CJK wrap opportunities");
        using (var layout = Generate("A\r\nB\n", fonts)) { Check(layout.lineCount == 3, "CRLF and trailing newline"); Check(layout.characters.Count == 4 && layout.characters[1].utf16Length == 2, "Newlines preserve source ranges for editing consumers"); }
        using (var layout = Generate("\n", fonts)) Check(layout.lineCount == 2 && layout.preferredSize.y == 40, "Newline-only text uses real font metrics");
        var spacing = Settings(); spacing.lineSpacing = .5f;
        using (var layout = Generate("A\nA", fonts, spacing))
            Check(layout.preferredSize.y == 30 && layout.characters[0].origin.y - layout.characters[2].origin.y == 10, "Line spacing below one permits overlapping line boxes");
        spacing.lineSpacing = 2;
        using (var layout = Generate("A\nA", fonts, spacing))
            Check(layout.preferredSize.y == 60 && layout.characters[0].origin.y - layout.characters[2].origin.y == 40, "Line spacing expands baseline advance");
        using (var layout = Generate("A\tB", fonts)) Check(layout.characters[2].origin.x - layout.characters[0].origin.x == 20, "Tab stops");
        for (int anchor = 0; anchor < 9; ++anchor)
        {
            var data = Settings(); data.alignment = (TextAnchor)anchor;
            using var layout = Generate("A", fonts, data);
            float x = -20 + (200 - 10) * (anchor % 3) * .5f;
            float y = 90 - (100 - 20) * (anchor / 3) * .5f - 16;
            Check(Math.Abs(layout.characters[0].origin.x - x) < .001f && Math.Abs(layout.characters[0].origin.y - y) < .001f, "Nine anchor positions / nonzero rect origin");
        }
        var fit = Settings(); fit.bestFit = true; fit.minSize = 5; fit.maxSize = 30;
        int requests = fonts.Requests;
        using (var layout = Generate("AAAA", fonts, fit, 20, 10)) Check(layout.fontSize == 10 && layout.fits, "Best fit chooses largest fitting integer");
        Check(fonts.Requests == requests + 1, "Best fit reuses fixed-em glyphs across sizes");
        var overflow = Settings(); overflow.horizontalOverflow = HorizontalWrapMode.Overflow;
        using (var layout = Generate("AAAA", fonts, overflow, 5, 100)) Check(layout.lineCount == 1 && !layout.fits, "Horizontal overflow");
        using (var layout = Generate("A\nB", fonts, width: 100, height: 20)) Check(layout.vertices.Count == 4, "Vertical truncation excludes full overflowing line");
        overflow.verticalOverflow = VerticalWrapMode.Overflow;
        using (var layout = Generate("A\nB", fonts, overflow, 100, 20)) Check(layout.vertices.Count == 8, "Vertical overflow emits all lines");
        using (var layout = Generate("<b>A<i>B</i></b><color=#ff000080>C</color><size=30>D</size>", fonts))
        {
            Check(layout.characters.Count == 4 && layout.vertices[8].color == new Color32(255, 0, 0, 128), "Nested rich tags and RGBA tint");
            Check(layout.preferredSize.x == 45 && layout.preferredSize.y == 30, "Rich absolute size affects layout");
            Check(layout.ranges[0].page.generation == 11 && layout.ranges[1].page.generation == 31, "Rich style batches");
        }
        var plain = Settings(); plain.richText = false;
        using (var layout = Generate("<b>A</b>", fonts, plain)) Check(layout.characters.Count == 8, "Rich text disabled");
        using (var layout = Generate("<oops>A", fonts)) Check(layout.characters.Count == 7, "Unknown tags remain literal");
        using (var layout = Generate("", fonts)) Check(layout.lineCount == 0 && layout.vertices.Count == 0 && layout.preferredSize == Vector2.zero, "Empty text");
        var broken = new Fonts { FailKerning = true };
        bool failed = false;
        try { using var ignored = Generate("AB", broken); } catch { failed = true; }
        Check(failed && broken.Live == 0, "Failed native request releases all acquired leases");
        var invalid = Settings(); invalid.lineSpacing = float.NaN; failed = false;
        try { using var ignored = Generate("A", fonts, invalid); } catch (ArgumentException) { failed = true; }
        Check(failed, "Nonfinite settings rejected");
        Check(fonts.Live == 0, "No remaining font leases");
        Console.WriteLine($"PASS {checks} managed text layout checks");
    }
}
