#include <OvCore/UI/Fonts/Font.h>
#include <OvCore/ResourceManagement/FontManager.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace OvCore::UI::Fonts;
namespace
{
    int checks = 0;
    void Require(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
    template<class F> void Reject(F action, const char* message)
    {
        bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
        Require(rejected, message);
    }
    Batch Request(Font& font, std::initializer_list<uint32_t> cps, Style style = Style::Normal, SdfSettings sdf = {})
    {
        return font.EnsureGlyphs(std::span(cps.begin(), cps.size()), style, sdf);
    }
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("Expected font path and evidence directory");
        const std::filesystem::path path(argv[1]), output(argv[2]);
        Font font(path);
        auto first = Request(font, {'I', 'A', 'V', ' ', 'I', 0x1f600, 0x10ffff});
        Require(first.glyphs.size() == 6, "Stable scalar deduplication");
        Require(first.glyphs[0].scalar == 'I' && first.glyphs[1].scalar == 'A', "First occurrence ordering");
        Require(first.glyphs[3].glyph.page == UINT32_MAX && first.glyphs[3].glyph.advance > 0, "Space metrics without quad");
        Require(first.glyphs.back().missing, "Missing glyph flag");
        Require(first.metrics.ascender > 0 && first.metrics.descender < 0 && first.metrics.lineHeight > 0, "Font metrics");
        auto before = font.GetStatistics();
        auto repeat = Request(font, {'I', 'A', 'V', ' ', 'I', 0x1f600, 0x10ffff});
        Require(font.GetStatistics().generated == before.generated && font.GetStatistics().pageUpdates == before.pageUpdates, "Cache hit performs no rasterization/update");
        Require(first.pages[0] == repeat.pages[0], "Cache hit reuses immutable page");
        const auto& glyph = first.glyphs[0].glyph;
        const auto& page = *first.pages[glyph.page];
        const uint32_t x0 = static_cast<uint32_t>(std::lround(glyph.uv.left * page.size));
        const uint32_t x1 = static_cast<uint32_t>(std::lround(glyph.uv.right * page.size));
        const uint32_t y = static_cast<uint32_t>(std::lround((glyph.uv.bottom + glyph.uv.top) * .5f * page.size));
        bool inside = false, outside = false, contour = false, exteriorRamp = false;
        for (uint32_t x = x0; x < x1; ++x)
        {
            auto value = page.pixels[size_t(y) * page.size + x];
            inside |= value > 140; outside |= value == 0; contour |= value > 112 && value < 144;
            // An alpha bitmap has no multi-pixel distance ramp outside its contour.
            if (x + 1 < x1)
            {
                int next = page.pixels[size_t(y) * page.size + x + 1];
                if (value > 8 && value < 95 && next > value && next < 120)
                {
                    const float step = (next - value) * glyph.distanceScale / 255;
                    exteriorRamp |= std::abs(step - 1) < .1f;
                }
            }
        }
        Require(inside && outside && contour && exteriorRamp, "Outline signed distance (inside/contour/outside, one-pixel distance gradient)");
        std::ofstream pgm(output / "atlas.pgm", std::ios::binary);
        pgm << "P5\n" << page.size << ' ' << page.size << "\n255\n";
        pgm.write(reinterpret_cast<const char*>(page.pixels.data()), static_cast<std::streamsize>(page.pixels.size()));
        const auto oldPixels = page.pixels;
        auto appended = Request(font, {'B', 'C'});
        Require(first.pages[0]->pixels == oldPixels && appended.pages[0] != first.pages[0], "Append leaves old snapshot immutable");
        auto original = Request(font, {'I'});
        Require(original.glyphs[0].glyph.uv.left == glyph.uv.left && original.glyphs[0].glyph.uv.top == glyph.uv.top, "Append never relocates published UVs");
        auto bold = Request(font, {'I'}, Style::Bold);
        auto italic = Request(font, {'I'}, Style::Italic);
        auto quality = Request(font, {'I'}, Style::Normal, {128, 12});
        Require(bold.glyphs[0].glyph.advance > glyph.advance, "Bold outline and metrics");
        Require(italic.glyphs[0].glyph.plane.right > glyph.plane.right, "Italic outline transform");
        Require(quality.glyphs[0].glyph.sdf.emSize == 128 && quality.glyphs[0].glyph.distanceScale == 24, "SDF quality key");
        Require(std::abs(quality.glyphs[0].glyph.advance - glyph.advance) < .002f, "Metrics independent of SDF quality tier");
        KerningPair pairs[] = {{first.glyphs[1].glyph.glyphId, first.glyphs[2].glyph.glyphId}, {0, 0}};
        auto kern = font.GetKerning(pairs);
        Require(kern.size() == 2 && std::isfinite(kern[0]) && kern[1] == 0, "Batch kerning");
        Reject([&] { Request(font, {0xd800}); }, "Reject surrogate scalar");
        Reject([&] { Request(font, {0x110000}); }, "Reject out-of-range scalar");
        Reject([&] { Request(font, {'A'}, Style::Normal, {0, 8}); }, "Reject invalid SDF settings");
        Reject([&] { Request(font, {'A'}, static_cast<Style>(4)); }, "Reject invalid style");
        Reject([&] { KerningPair invalid[] = {{UINT32_MAX, 0}}; font.GetKerning(invalid); }, "Reject invalid kerning glyph");
        const auto generation = font.Generation();
        Reject([&] { font.Reload(output / "absent.ttf"); }, "Reject invalid reload");
        Require(font.Generation() == generation, "Failed reload preserves font");
        font.Reload(path);
        auto replacement = Request(font, {'I'});
        Require(replacement.generation != generation && replacement.pages[0]->generation != page.generation, "Reload creates new font and atlas generation");
        Require(first.pages[0]->pixels == oldPixels, "Reload preserves in-flight pages");
        bool rejected = false;
        std::thread worker([&] { try { Request(font, {'X'}); } catch (const std::logic_error&) { rejected = true; } }); worker.join();
        Require(rejected, "Worker cannot rasterize or pack");
        rejected = false;
        std::thread metricsWorker([&] { try { font.GetKerning(pairs); } catch (const std::logic_error&) { rejected = true; } }); metricsWorker.join();
        Require(rejected, "Worker cannot access FreeType metrics");
        uint64_t checksum = 0;
        std::thread render([&] { for (auto pixel : first.pages[0]->pixels) checksum += pixel; }); render.join();
        Require(checksum != 0, "Render worker reads immutable lease without Font access");
        Font small(path, {64, 16, 256, 256});
        std::vector<uint32_t> scalars;
        for (uint32_t cp = 'A'; cp <= 'Z'; ++cp) scalars.push_back(cp);
        auto multiple = small.EnsureGlyphs(scalars, Style::Normal, {32, 2});
        Require(multiple.pages.size() > 1, "Atlas grows to multiple bounded pages");
        for (const auto& record : multiple.glyphs) Require(record.glyph.page < multiple.pages.size(), "Glyph addresses valid page");
        Font tiny(path, {64, 1, 256, 256});
        auto tinyStart = Request(tiny, {'I'}, Style::Normal, {32, 2});
        auto tinyStats = tiny.GetStatistics();
        Reject([&] { tiny.EnsureGlyphs(scalars, Style::Normal, {32, 2}); }, "Atlas capacity is bounded");
        Require(tiny.GetStatistics().generated == tinyStats.generated && tiny.GetStatistics().pageUpdates == tinyStats.pageUpdates, "Capacity failure rolls back entire batch");
        auto tinyAfter = Request(tiny, {'I'}, Style::Normal, {32, 2});
        Require(tinyAfter.pages[0] == tinyStart.pages[0], "Failed batch leaves page revision untouched");
        Reject([&] { Font invalid(path, {2048, 64}); }, "Reject excessive atlas memory budget");
        Reject([&] { Font invalid(path, {}, 10000); }, "Reject invalid face index");
        Reject([&] { Font invalid(path, {}, 0, {1}); }, "Reject variations on static font");
        std::weak_ptr<const AtlasPage> weak;
        Batch lease;
        {
            Font temporary(path); lease = Request(temporary, {'A'}); weak = lease.pages[0];
        }
        Require(!weak.expired() && lease.pages[0]->pixels.size() > 0, "Lease outlives font face/file data");
        lease = {};
        Require(weak.expired(), "Last in-flight lease releases atlas memory");
        OvCore::ResourceManagement::FontManager manager;
        auto* managed = manager.LoadResource(path);
        auto managedBatch = Request(*managed, {'A'});
        manager.ReloadResource(path);
        Require(managed->Generation() != managedBatch.generation, "Existing resource manager hot reload");
        manager.UnloadResource(path);
        Require(managedBatch.pages[0]->pixels.size() > 0 && manager.GetResource(path, false) == nullptr, "Resource unload retains frame lease");
        Font fixture(output / "outline-fixture.ttf");
        auto outlined = Request(fixture, {'A', 'O', 0x4e2d, 0x1f600, 0x301});
        Require(std::all_of(outlined.glyphs.begin(), outlined.glyphs.end(), [](const auto& record) { return !record.missing; }), "CJK/astral/combining Unicode cmap lookup");
        Require(outlined.glyphs[0].glyph.glyphId == outlined.glyphs[2].glyph.glyphId && fixture.GetStatistics().generated == 2, "Aliases share glyph cache key");
        KerningPair known[] = {{1, 1}};
        Require(std::abs(fixture.GetKerning(known)[0] + .1f) < .00001f, "Exact unscaled kerning from font units");
        const auto& rectangle = outlined.glyphs[0].glyph;
        const auto& rectPage = *outlined.pages[rectangle.page];
        const int rx = static_cast<int>(std::lround(rectangle.uv.left * rectPage.size));
        const int ry = static_cast<int>(std::lround(rectangle.uv.bottom * rectPage.size));
        const int rw = static_cast<int>(std::lround(rectangle.plane.right * 64 - rectangle.plane.left * 64));
        const int rh = static_cast<int>(std::lround(rectangle.plane.top * 64 - rectangle.plane.bottom * 64));
        // Compare every texel with analytic rectangle SDF, including corners.
        float maximumError = 0;
        for (int row = 0; row < rh; ++row) for (int x = 0; x < rw; ++x)
        {
            float px = rectangle.plane.left * 64 + x + .5f, py = rectangle.plane.bottom * 64 + row + .5f;
            float dx = std::max(std::max(-px, px - 32), 0.f), dy = std::max(std::max(-py, py - 44.796875f), 0.f);
            float distance = dx > 0 || dy > 0 ? -std::sqrt(dx * dx + dy * dy) : std::min({px, 32 - px, py, 44.796875f - py});
            distance = std::clamp(distance, -8.f, 8.f);
            float actual = rectPage.pixels[size_t(ry + row) * rectPage.size + rx + x] * (16.f / 255) - 8;
            maximumError = std::max(maximumError, std::abs(actual - distance));
        }
        Require(maximumError < .04f, "All rectangle texels match analytic signed distance within R8 quantization");
        const auto& ring = outlined.glyphs[1].glyph;
        const auto& ringPage = *outlined.pages[ring.page];
        const int hx = static_cast<int>(std::lround(ring.uv.left * ringPage.size + (16 - ring.plane.left * 64)));
        const int hy = static_cast<int>(std::lround(ring.uv.bottom * ringPage.size + (22 - ring.plane.bottom * 64)));
        Require(ringPage.pixels[size_t(hy) * ringPage.size + hx] < 32, "Counter contour is outside, not filled coverage");
        const auto oldFixture = outlined.pages[0];
        fixture.Reload(path);
        Require(Request(fixture, {'A'}).glyphs[0].glyph.advance != rectangle.advance && oldFixture->generation == outlined.generation, "Switch to different font preserves old metrics/page generation");
        std::cout << "PASS " << checks << " native SDF checks\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
