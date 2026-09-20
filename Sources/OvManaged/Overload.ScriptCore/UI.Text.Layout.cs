using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using Overload.Maths;

namespace Overload.UI;

/// <summary>
/// Managed legacy text layout. Unicode scalars retain UTF-16 source ranges.
/// Combining marks attach to the preceding base; shaping/bidi are not performed.
/// No component, session, renderer or global font registry is retained here.
/// </summary>
public static class TextLayout
{
    private readonly record struct Format(FontStyle style, float size, Color color);
    private sealed class Token(uint scalar, int start, int length, Format format)
    {
        internal uint Scalar = scalar;
        internal int Start = start, Length = length;
        internal Format Format = format;
        internal TextFontBatch? Font;
        internal TextGlyph Glyph;
        internal float Kern;
        internal bool Mark => Rune.GetUnicodeCategory(new Rune(Scalar)) is UnicodeCategory.NonSpacingMark or UnicodeCategory.EnclosingMark;
        internal bool Space => Scalar is 32 or 9;
        internal bool Cjk => Scalar is >= 0x2e80 and <= 0x9fff or >= 0xf900 and <= 0xfaff or >= 0x20000 and <= 0x3134f;
    }
    private sealed class Line
    {
        internal readonly List<(Token token, float x, float advance)> Items = new();
        internal float Width, Ascender, Descender, Height, Advance;
        internal Token? Newline;
    }

    public static TextLayoutResult Generate(string text, FontData settings, Rect rect, Color color, ITextFontSource source)
    {
        ArgumentNullException.ThrowIfNull(text); ArgumentNullException.ThrowIfNull(settings); ArgumentNullException.ThrowIfNull(source);
        settings.Validate();
        if (text.Length > 65536 || !float.IsFinite(rect.min.x) || !float.IsFinite(rect.min.y) ||
            !float.IsFinite(rect.width) || !float.IsFinite(rect.height) || rect.width < 0 || rect.height < 0 ||
            !Finite(color)) throw new ArgumentException("Invalid text bounds, tint or length");
        var tokens = Parse(text, settings, color);
        var leases = new List<TextFontBatch>();
        try
        {
            Resolve(tokens, settings, source, leases);
            int size = settings.fontSize;
            List<Line> lines;
            if (settings.bestFit)
            {
                // The bounded search also handles absolute rich-text sizes, for
                // which line reflow need not be monotonic with the base size.
                size = settings.maxSize;
                for (; size > settings.minSize; --size)
                    if (Fits(Layout(tokens, size, settings, rect.width, leases), rect)) break;
            }
            lines = Layout(tokens, size, settings, rect.width, leases);
            return Build(lines, size, settings, rect, leases);
        }
        catch { foreach (var lease in leases) lease.Dispose(); throw; }
    }

    private static bool Finite(Color color) => float.IsFinite(color.r) && float.IsFinite(color.g) && float.IsFinite(color.b) && float.IsFinite(color.a);
    private static float Size(Token token, int baseSize) => token.Format.size > 0 ? token.Format.size : baseSize;
    private static List<Token> Parse(string text, FontData data, Color color)
    {
        var result = new List<Token>();
        var stack = new Stack<(string tag, Format previous)>();
        Format format = new(data.fontStyle, 0, color);
        for (int i = 0; i < text.Length;)
        {
            if (data.richText && text[i] == '<')
            {
                int end = text.IndexOf('>', i + 1);
                if (end >= 0 && end - i <= 128 && TryTag(text.Substring(i + 1, end - i - 1), ref format, stack)) { i = end + 1; continue; }
            }
            int start = i;
            var status = Rune.DecodeFromUtf16(text.AsSpan(i), out Rune rune, out int consumed);
            if (status != System.Buffers.OperationStatus.Done) { rune = Rune.ReplacementChar; consumed = 1; }
            i += consumed;
            uint scalar = (uint)rune.Value;
            if (scalar == '\r') { scalar = '\n'; if (i < text.Length && text[i] == '\n') ++i; }
            result.Add(new Token(scalar, start, i - start, format));
        }
        return result;
    }
    private static bool TryTag(string raw, ref Format format, Stack<(string tag, Format previous)> stack)
    {
        string tag = raw.ToLowerInvariant();
        if (tag.StartsWith('/'))
        {
            if (stack.Count == 0 || stack.Peek().tag != tag[1..]) return false;
            format = stack.Pop().previous; return true;
        }
        if (stack.Count >= 32) return false;
        Format next = format;
        string name = tag;
        if (tag == "b") next = format with { style = (FontStyle)((int)format.style | 1) };
        else if (tag == "i") next = format with { style = (FontStyle)((int)format.style | 2) };
        else if (tag.StartsWith("size=") && float.TryParse(tag.AsSpan(5), NumberStyles.Integer, CultureInfo.InvariantCulture, out float size) && size is >= 1 and <= 4096)
        { name = "size"; next = format with { size = size }; }
        else if (tag.StartsWith("color=") && TryColor(tag[6..].Trim('"', '\''), out Color tint))
        { name = "color"; next = format with { color = tint }; }
        else return false;
        stack.Push((name, format)); format = next; return true;
    }
    private static bool TryColor(string value, out Color color)
    {
        color = Color.white;
        if (value.StartsWith('#') && value.Length is 7 or 9 && uint.TryParse(value.AsSpan(1), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint rgba))
        {
            if (value.Length == 7) rgba = (rgba << 8) | 255;
            color = new((rgba >> 24) / 255f, ((rgba >> 16) & 255) / 255f, ((rgba >> 8) & 255) / 255f, (rgba & 255) / 255f); return true;
        }
        uint? named = value switch {
            "black" => 0x000000ff, "white" => 0xffffffff, "red" => 0xff0000ff, "green" => 0x008000ff,
            "blue" => 0x0000ffff, "yellow" => 0xffff00ff, "cyan" or "aqua" => 0x00ffffff,
            "magenta" or "fuchsia" => 0xff00ffff, "grey" or "gray" => 0x808080ff,
            "silver" => 0xc0c0c0ff, "lime" => 0x00ff00ff, "maroon" => 0x800000ff,
            "navy" => 0x000080ff, "olive" => 0x808000ff, "purple" => 0x800080ff, "teal" => 0x008080ff,
            "orange" => 0xffa500ff, "brown" => 0xa52a2aff, _ => null };
        return named.HasValue && TryColor("#" + named.Value.ToString("x8", CultureInfo.InvariantCulture), out color);
    }

    private static void Resolve(List<Token> tokens, FontData data, ITextFontSource source, List<TextFontBatch> leases)
    {
        var paths = new List<string> { data.font };
        foreach (string path in data.fallbackFonts) if (!paths.Contains(path)) paths.Add(path);
        // Request replacement characters up front so missing text never causes
        // a per-character crossing of the managed/native boundary.
        foreach (FontStyle style in Enum.GetValues<FontStyle>())
        {
            var pending = tokens.FindAll(t => t.Scalar != '\n' && t.Format.style == style);
            if (pending.Count == 0 && (style != data.fontStyle || tokens.Exists(t => t.Scalar != '\n'))) continue;
            TextFontBatch? primary = null;
            foreach (string path in paths)
            {
                var unique = new HashSet<uint> { 0xfffd, (uint)'?', (uint)' ' };
                foreach (var t in pending) unique.Add(t.Scalar == '\t' ? (uint)' ' : t.Scalar);
                var requested = new List<uint>(unique); requested.Sort();
                var batch = source.EnsureGlyphs(path, style, requested);
                leases.Add(batch); ValidateBatch(batch, path, style, requested); primary ??= batch;
                for (int i = pending.Count - 1; i >= 0; --i)
                {
                    var t = pending[i]; uint cp = t.Scalar == '\t' ? (uint)' ' : t.Scalar;
                    if (batch.glyphs.TryGetValue(cp, out var glyph) && !glyph.missing) { t.Font = batch; t.Glyph = glyph; pending.RemoveAt(i); }
                }
                if (pending.Count == 0) break;
            }
            foreach (var token in pending)
            {
                // Stable replacement fallback: U+FFFD then '?' then primary .notdef.
                foreach (uint cp in new uint[] { 0xfffd, (uint)'?' })
                {
                    foreach (var batch in leases)
                        if (batch.style == style && batch.glyphs.TryGetValue(cp, out var glyph) && !glyph.missing)
                        { token.Font = batch; token.Glyph = glyph; break; }
                    if (token.Font != null) break;
                }
                if (token.Font == null) { token.Font = primary!; token.Glyph = primary!.glyphs[0xfffd]; }
            }
        }
        var pairs = new Dictionary<TextFontBatch, Dictionary<TextKerningPair, List<Token>>>();
        Token? previous = null;
        foreach (var token in tokens)
        {
            if (token.Scalar == '\n' || token.Scalar == '\t') { previous = null; continue; }
            if (token.Mark) continue;
            if (previous?.Font == token.Font && previous != null && previous.Format.size == token.Format.size)
            {
                var pair = new TextKerningPair(previous.Glyph.glyphId, token.Glyph.glyphId);
                if (!pairs.TryGetValue(token.Font!, out var group)) pairs.Add(token.Font!, group = new());
                if (!group.TryGetValue(pair, out var consumers)) group.Add(pair, consumers = new());
                consumers.Add(token);
            }
            previous = token;
        }
        foreach (var entry in pairs)
        {
            var unique = new List<TextKerningPair>(entry.Value.Keys);
            var values = source.GetKerning(entry.Key, unique);
            if (values.Count != unique.Count) throw new InvalidOperationException("Invalid kerning batch length");
            for (int i = 0; i < values.Count; ++i)
            {
                if (!float.IsFinite(values[i])) throw new InvalidOperationException("Invalid kerning value");
                foreach (var token in entry.Value[unique[i]]) token.Kern = values[i];
            }
        }
    }

    private static void ValidateBatch(TextFontBatch batch, string path, FontStyle style, List<uint> requested)
    {
        if (batch.font != path || batch.style != style || batch.generation == 0 ||
            !float.IsFinite(batch.metrics.ascender) || !float.IsFinite(batch.metrics.descender) ||
            !float.IsFinite(batch.metrics.lineHeight) || batch.metrics.lineHeight <= 0 || batch.metrics.ascender < batch.metrics.descender)
            throw new InvalidOperationException("Invalid native font batch");
        foreach (uint cp in requested)
        {
            if (!batch.glyphs.TryGetValue(cp, out var g) || g.generation != batch.generation ||
                !float.IsFinite(g.advance) || !FiniteRect(g.plane) || !FiniteRect(g.uv) ||
                !float.IsFinite(g.distanceScale) || g.distanceScale <= 0 || !float.IsFinite(g.distanceBias) ||
                !float.IsFinite(g.bearing.x) || !float.IsFinite(g.bearing.y) ||
                g.uv.min.x < 0 || g.uv.min.y < 0 || g.uv.max.x > 1 || g.uv.max.y > 1)
                throw new InvalidOperationException("Invalid native glyph record");
        }
    }
    private static bool FiniteRect(Rect r) => float.IsFinite(r.min.x) && float.IsFinite(r.min.y) &&
        float.IsFinite(r.width) && float.IsFinite(r.height) && r.width >= 0 && r.height >= 0;

    private static List<Line> Layout(List<Token> tokens, int size, FontData data, float width, List<TextFontBatch> fonts)
    {
        var result = new List<Line>();
        if (tokens.Count == 0) return result;
        var defaults = fonts.Count == 0 ? new TextFontMetrics(.8f, -.2f, 1) : fonts[0].metrics;
        int start = 0;
        while (start < tokens.Count)
        {
            var line = new Line(); int end = start, breakAt = -1; float x = 0, baseX = 0;
            for (; end < tokens.Count && tokens[end].Scalar != '\n'; ++end)
            {
                var t = tokens[end]; float scale = Size(t, size);
                float kern = end == start || t.Mark ? 0 : t.Kern * scale;
                float advance = t.Mark ? 0 : t.Glyph.advance * scale;
                if (t.Scalar == '\t') { float tab = Math.Max(1, advance * 4); advance = tab - x % tab; }
                float next = x + kern + advance;
                // Never split a combining sequence; break at spaces/CJK first,
                // otherwise split long words by scalar to guarantee progress.
                if (data.horizontalOverflow == HorizontalWrapMode.Wrap && next > width && end > start && !t.Mark && !t.Space)
                {
                    if (breakAt > start) end = breakAt;
                    break;
                }
                x = next;
                if (t.Space || t.Cjk) breakAt = end + 1;
            }
            if (end == start && tokens[end].Scalar != '\n') ++end;
            x = 0;
            for (int i = start; i < end; ++i)
            {
                var t = tokens[i]; float scale = Size(t, size);
                float kern = i == start || t.Mark ? 0 : t.Kern * scale;
                float advance = t.Mark ? 0 : t.Glyph.advance * scale;
                if (t.Scalar == '\t') { float tab = Math.Max(1, advance * 4); advance = tab - x % tab; }
                float origin = t.Mark ? baseX : x + kern;
                if (!t.Mark) baseX = origin;
                line.Items.Add((t, origin, advance)); x += kern + advance;
                var metrics = t.Font!.metrics;
                line.Ascender = Math.Max(line.Ascender, metrics.ascender * scale);
                line.Descender = Math.Min(line.Descender, metrics.descender * scale);
                line.Height = Math.Max(line.Height, metrics.lineHeight * scale);
            }
            line.Width = x;
            // Trailing whitespace participates in source mapping, but not alignment.
            for (int i = line.Items.Count - 1; i >= 0 && line.Items[i].token.Space; --i) line.Width = line.Items[i].x;
            if (line.Items.Count == 0)
            { line.Ascender = defaults.ascender * size; line.Descender = defaults.descender * size; line.Height = defaults.lineHeight * size; }
            line.Height = Math.Max(line.Height, line.Ascender - line.Descender);
            line.Advance = line.Height * data.lineSpacing;
            result.Add(line);
            bool newline = end < tokens.Count && tokens[end].Scalar == '\n';
            if (newline) line.Newline = tokens[end];
            start = newline ? end + 1 : end;
            if (newline && start == tokens.Count)
            {
                float height = Math.Max(defaults.lineHeight, defaults.ascender - defaults.descender) * size;
                result.Add(new Line { Ascender = defaults.ascender * size, Descender = defaults.descender * size, Height = height, Advance = height * data.lineSpacing });
            }
        }
        return result;
    }
    private static Vector2 Extents(List<Line> lines)
    {
        float width = 0, height = 0, offset = 0;
        foreach (var line in lines) { width = Math.Max(width, line.Width); height = Math.Max(height, offset + line.Height); offset += line.Advance; }
        return new(width, height);
    }
    private static bool Fits(List<Line> lines, Rect rect) { var size = Extents(lines); return size.x <= rect.width + .001f && size.y <= rect.height + .001f; }

    private static TextLayoutResult Build(List<Line> lines, int size, FontData data, Rect rect, List<TextFontBatch> leases)
    {
        var vertices = new List<UIVertex>(); var indices = new List<int>(); var ranges = new List<TextDrawRange>();
        var characters = new List<TextCharacter>(); var preferred = Extents(lines);
        int horizontal = (int)data.alignment % 3, vertical = (int)data.alignment / 3;
        float visibleHeight = 0, lineOffset = 0;
        int visibleCount = 0;
        foreach (var line in lines)
        {
            if (data.verticalOverflow == VerticalWrapMode.Truncate && lineOffset + line.Height > rect.height + .001f) break;
            ++visibleCount; visibleHeight = Math.Max(visibleHeight, lineOffset + line.Height); lineOffset += line.Advance;
        }
        float top = rect.max.y - (rect.height - visibleHeight) * vertical * .5f;
        float consumed = 0;
        for (int number = 0; number < lines.Count; ++number)
        {
            var line = lines[number];
            bool visible = number < visibleCount;
            float baseline = top - consumed - line.Ascender;
            float offset = rect.min.x + (rect.width - line.Width) * horizontal * .5f;
            foreach (var item in line.Items)
            {
                var t = item.token; float scale = Size(t, size); var origin = new Vector2(offset + item.x, baseline);
                characters.Add(new(t.Start, t.Length, t.Scalar, number, origin, item.advance));
                if (!visible || t.Space || t.Glyph.page == uint.MaxValue) continue;
                var plane = t.Glyph.plane; var uv = t.Glyph.uv;
                Vector2 min = origin + plane.min * scale, max = origin + plane.max * scale;
                int first = vertices.Count;
                Color32 tint = t.Format.color;
                if (first > 65531) throw new InvalidOperationException("Text mesh exceeds CanvasRenderer's 65535 vertex limit");
                Vector2 sdf = new(t.Glyph.distanceScale, t.Glyph.distanceBias);
                vertices.Add(new UIVertex { position = new(min.x, min.y, 0), uv0 = uv.min, uv1 = sdf, color = tint });
                vertices.Add(new UIVertex { position = new(min.x, max.y, 0), uv0 = new(uv.min.x, uv.max.y), uv1 = sdf, color = tint });
                vertices.Add(new UIVertex { position = new(max.x, max.y, 0), uv0 = uv.max, uv1 = sdf, color = tint });
                vertices.Add(new UIVertex { position = new(max.x, min.y, 0), uv0 = new(uv.max.x, uv.min.y), uv1 = sdf, color = tint });
                var page = new TextPageKey(t.Glyph.generation, t.Glyph.page);
                if (ranges.Count > 0 && ranges[^1].page == page && ranges[^1].distanceScale == t.Glyph.distanceScale && ranges[^1].distanceBias == t.Glyph.distanceBias)
                    ranges[^1] = ranges[^1] with { indexCount = ranges[^1].indexCount + 6 };
                else ranges.Add(new(page, indices.Count, 6, t.Glyph.distanceScale, t.Glyph.distanceBias));
                indices.Add(first); indices.Add(first + 1); indices.Add(first + 2); indices.Add(first + 2); indices.Add(first + 3); indices.Add(first);
            }
            if (line.Newline is Token newline)
                characters.Add(new(newline.Start, newline.Length, newline.Scalar, number, new(offset + line.Width, baseline), 0));
            consumed += line.Advance;
        }
        return new(vertices, indices, ranges, characters, preferred, size, lines.Count, Fits(lines, rect), leases);
    }
}
