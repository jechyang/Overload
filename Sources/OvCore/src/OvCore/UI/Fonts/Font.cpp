#include <OvCore/UI/Fonts/Font.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_MULTIPLE_MASTERS_H
#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace OvCore::UI::Fonts
{
    namespace
    {
        std::atomic<uint64_t> nextGeneration{1};
        void Check(FT_Error error, const char* operation)
        {
            if (error) throw std::runtime_error(operation);
        }
        struct Point { double x, y; };
        Point Mid(Point a, Point b) { return {(a.x + b.x) * .5, (a.y + b.y) * .5}; }
        double DistanceSquared(Point p, Point a, Point b)
        {
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double length = dx * dx + dy * dy;
            const double t = length == 0 ? 0 : std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length, 0., 1.);
            const double x = p.x - a.x - dx * t, y = p.y - a.y - dy * t;
            return x * x + y * y;
        }
        struct Edge { Point a, b; };
        struct Outline
        {
            std::vector<Edge> edges;
            Point current{}, start{};
            bool open = false;
            std::exception_ptr failure;
            static Point Convert(const FT_Vector* p) { return {p->x / 64., p->y / 64.}; }
            void Line(Point p)
            {
                if (edges.size() >= 65536) throw std::length_error("Font outline edge limit");
                if (p.x != current.x || p.y != current.y) edges.push_back({current, p});
                current = p;
            }
            void Close() { if (open) Line(start); open = false; }
            void Quadratic(Point a, Point b, Point c, int depth = 0)
            {
                if (depth == 16 || DistanceSquared(b, a, c) <= .0004) { Line(c); return; }
                Point ab = Mid(a, b), bc = Mid(b, c), m = Mid(ab, bc);
                Quadratic(a, ab, m, depth + 1); Quadratic(m, bc, c, depth + 1);
            }
            void Cubic(Point a, Point b, Point c, Point d, int depth = 0)
            {
                if (depth == 16 || std::max(DistanceSquared(b, a, d), DistanceSquared(c, a, d)) <= .0004) { Line(d); return; }
                Point ab = Mid(a, b), bc = Mid(b, c), cd = Mid(c, d), abc = Mid(ab, bc), bcd = Mid(bc, cd), m = Mid(abc, bcd);
                Cubic(a, ab, abc, m, depth + 1); Cubic(m, bcd, cd, d, depth + 1);
            }
            // Exceptions must not unwind through FreeType's C callbacks.
            template<class F> static int Invoke(void* user, F action)
            {
                auto& o = *static_cast<Outline*>(user);
                try { action(o); return 0; } catch (...) { o.failure = std::current_exception(); return 1; }
            }
            static int Move(const FT_Vector* p, void* u) { return Invoke(u, [&](Outline& o) { o.Close(); o.current = o.start = Convert(p); o.open = true; }); }
            static int LineTo(const FT_Vector* p, void* u) { return Invoke(u, [&](Outline& o) { o.Line(Convert(p)); }); }
            static int Conic(const FT_Vector* c, const FT_Vector* p, void* u) { return Invoke(u, [&](Outline& o) { o.Quadratic(o.current, Convert(c), Convert(p)); }); }
            static int Bezier(const FT_Vector* a, const FT_Vector* b, const FT_Vector* p, void* u) { return Invoke(u, [&](Outline& o) { o.Cubic(o.current, Convert(a), Convert(b), Convert(p)); }); }
        };
        struct Bitmap { uint32_t width = 0, height = 0; std::vector<uint8_t> pixels; Bounds plane; };
        Bitmap Generate(FT_Outline& source, SdfSettings settings)
        {
            if (source.n_points == 0) return {};
            Outline outline;
            FT_Outline_Funcs callbacks{Outline::Move, Outline::LineTo, Outline::Conic, Outline::Bezier, 0, 0};
            auto error = FT_Outline_Decompose(&source, &callbacks, &outline);
            if (outline.failure) std::rethrow_exception(outline.failure);
            Check(error, "FreeType outline decomposition failed");
            outline.Close();
            if (outline.edges.empty()) return {};
            FT_BBox box; FT_Outline_Get_CBox(&source, &box);
            // Two extra zero texels protect linear filtering at the allocation edge.
            const double padding = settings.spread + 2.;
            const double left = std::floor(box.xMin / 64. - padding), bottom = std::floor(box.yMin / 64. - padding);
            const double right = std::ceil(box.xMax / 64. + padding), top = std::ceil(box.yMax / 64. + padding);
            if (right - left > 1024 || top - bottom > 1024) throw std::length_error("Font glyph bitmap limit");
            Bitmap result;
            result.width = static_cast<uint32_t>(right - left); result.height = static_cast<uint32_t>(top - bottom);
            // Bound pathological fonts before entering the distance loop.
            if (uint64_t(result.width) * result.height * outline.edges.size() > 200000000)
                throw std::length_error("Font outline work limit");
            const float em = static_cast<float>(settings.emSize);
            result.plane = {static_cast<float>(left) / em, static_cast<float>(bottom) / em,
                static_cast<float>(right) / em, static_cast<float>(top) / em};
            result.pixels.resize(size_t(result.width) * result.height);
            for (uint32_t y = 0; y < result.height; ++y) for (uint32_t x = 0; x < result.width; ++x)
            {
                Point p{left + x + .5, bottom + y + .5};
                double distance = double(settings.spread) * settings.spread;
                int winding = 0;
                for (const auto& edge : outline.edges)
                {
                    distance = std::min(distance, DistanceSquared(p, edge.a, edge.b));
                    const double cross = (edge.b.x - edge.a.x) * (p.y - edge.a.y) - (p.x - edge.a.x) * (edge.b.y - edge.a.y);
                    if (edge.a.y <= p.y && edge.b.y > p.y && cross > 0) ++winding;
                    if (edge.a.y > p.y && edge.b.y <= p.y && cross < 0) --winding;
                }
                bool inside = (source.flags & FT_OUTLINE_EVEN_ODD_FILL) ? (winding % 2 != 0) : winding != 0;
                const double signedDistance = std::sqrt(distance) * (inside ? 1 : -1);
                result.pixels[size_t(y) * result.width + x] = static_cast<uint8_t>(std::lround(std::clamp(.5 + signedDistance / (2 * settings.spread), 0., 1.) * 255));
            }
            return result;
        }
        struct Shelf { uint32_t x = 0, y = 0, height = 0; };
        using Key = std::tuple<uint32_t, Style, uint32_t, uint32_t>;
    }

    struct Font::Impl
    {
        std::thread::id owner = std::this_thread::get_id();
        FT_Library library = nullptr;
        FT_Face face = nullptr;
        std::vector<uint8_t> bytes;
        Limits limits;
        int32_t faceIndex;
        std::vector<float> variations;
        uint64_t generation = nextGeneration.fetch_add(1);
        Statistics statistics;
        std::map<Key, Glyph> cache;
        std::vector<std::shared_ptr<const AtlasPage>> pages;
        std::vector<Shelf> shelves;

        Impl(const std::filesystem::path& path, Limits bounds, int32_t index, std::vector<float> coordinates)
            : limits(bounds), faceIndex(index), variations(std::move(coordinates))
        {
            if (limits.pageSize < 64 || limits.pageSize > 2048 || !limits.maxPages || limits.maxPages > 64 ||
                uint64_t(limits.pageSize) * limits.pageSize * limits.maxPages > 64 * 1024 * 1024 ||
                !limits.maxGlyphs || limits.maxGlyphs > 65536 || !limits.maxBatch || limits.maxBatch > 65536 || index < 0)
                throw std::invalid_argument("Font limits/face index invalid");
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) throw std::runtime_error("Cannot open font");
            const auto size = stream.tellg();
            if (size <= 0 || size > 64 * 1024 * 1024) throw std::length_error("Font file limit");
            bytes.resize(static_cast<size_t>(size)); stream.seekg(0);
            if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) throw std::runtime_error("Cannot read font");
            try
            {
                Check(FT_Init_FreeType(&library), "FreeType initialization failed");
                Check(FT_New_Memory_Face(library, bytes.data(), static_cast<FT_Long>(bytes.size()), index, &face), "Invalid font face");
                if (!FT_IS_SCALABLE(face) || face->units_per_EM == 0) throw std::invalid_argument("Outline font required");
                Check(FT_Select_Charmap(face, FT_ENCODING_UNICODE), "Unicode font required");
                if (!variations.empty())
                {
                    FT_MM_Var* descriptor = nullptr;
                    Check(FT_Get_MM_Var(face, &descriptor), "Font has no variation axes");
                    try
                    {
                        if (variations.size() != descriptor->num_axis) throw std::invalid_argument("Font variation axis count");
                        std::vector<FT_Fixed> values;
                        for (size_t i = 0; i < variations.size(); ++i)
                        {
                            const double value = double(variations[i]) * 65536;
                            if (!std::isfinite(value) || value < descriptor->axis[i].minimum || value > descriptor->axis[i].maximum)
                                throw std::invalid_argument("Font variation coordinate out of range");
                            values.push_back(static_cast<FT_Fixed>(std::lround(value)));
                        }
                        Check(FT_Set_Var_Design_Coordinates(face, static_cast<FT_UInt>(values.size()), values.data()), "Font variation failed");
                    }
                    catch (...) { FT_Done_MM_Var(library, descriptor); throw; }
                    FT_Done_MM_Var(library, descriptor);
                }
            }
            catch (...) { if (face) FT_Done_Face(face); if (library) FT_Done_FreeType(library); throw; }
        }
        ~Impl() { if (face) FT_Done_Face(face); if (library) FT_Done_FreeType(library); }
        void RequireOwner() const
        {
            if (owner != std::this_thread::get_id()) throw std::logic_error("Font operation requires resource owner thread");
        }
        Metrics GetMetrics() const
        {
            const float em = static_cast<float>(face->units_per_EM);
            return {face->ascender / em, face->descender / em, face->height / em};
        }
    };

    Font::Font(const std::filesystem::path& path, Limits limits, int32_t faceIndex, std::vector<float> variations)
        : m_impl(std::make_unique<Impl>(path, limits, faceIndex, std::move(variations))) {}
    Font::~Font() = default;
    bool Font::IsUnicodeScalar(uint32_t value) { return value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff); }
    void Font::Reload(const std::filesystem::path& path)
    {
        m_impl->RequireOwner();
        auto replacement = std::make_unique<Impl>(path, m_impl->limits, m_impl->faceIndex, m_impl->variations);
        m_impl.swap(replacement); // Strong guarantee: a bad replacement preserves the current resource.
    }
    Metrics Font::GetMetrics() const { m_impl->RequireOwner(); return m_impl->GetMetrics(); }
    uint64_t Font::Generation() const { m_impl->RequireOwner(); return m_impl->generation; }
    Statistics Font::GetStatistics() const { m_impl->RequireOwner(); return m_impl->statistics; }

    Batch Font::EnsureGlyphs(std::span<const uint32_t> scalars, Style style, SdfSettings sdf)
    {
        auto& state = *m_impl; state.RequireOwner();
        if (scalars.size() > state.limits.maxBatch || static_cast<uint32_t>(style) > 3 || sdf.emSize < 16 || sdf.emSize > 256 || sdf.spread < 2 || sdf.spread > 32)
            throw std::invalid_argument("Font batch/style/SDF settings invalid");
        for (auto cp : scalars) if (!IsUnicodeScalar(cp)) throw std::invalid_argument("Invalid Unicode scalar");
        Batch result; result.generation = state.generation; result.metrics = state.GetMetrics();
        // Transactional append: failed requests publish no cache entries or pages.
        auto pages = state.pages;
        auto shelves = state.shelves;
        std::map<Key, Glyph> additions;
        std::map<uint32_t, std::shared_ptr<AtlasPage>> changed;
        Statistics statistics = state.statistics;
        std::set<uint32_t> seen;
        Check(FT_Set_Pixel_Sizes(state.face, 0, sdf.emSize), "Cannot set SDF design size");
        for (uint32_t cp : scalars)
        {
            if (!seen.insert(cp).second) continue;
            const uint32_t id = FT_Get_Char_Index(state.face, cp);
            const Key key{id, style, sdf.emSize, sdf.spread};
            Glyph glyph;
            auto existing = state.cache.find(key), added = additions.find(key);
            if (existing != state.cache.end() || added != additions.end())
            {
                glyph = existing != state.cache.end() ? existing->second : added->second;
                ++statistics.hits;
            }
            else
            {
                if (state.cache.size() + additions.size() >= state.limits.maxGlyphs) throw std::length_error("Font glyph cache full");
                Check(FT_Load_Glyph(state.face, id, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP), "Cannot load font outline");
                if (state.face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) throw std::runtime_error("Glyph has no scalable outline");
                auto& outline = state.face->glyph->outline;
                const bool bold = style == Style::Bold || style == Style::BoldItalic;
                if (bold) Check(FT_Outline_Embolden(&outline, static_cast<FT_Pos>(sdf.emSize)), "Cannot embolden font outline");
                if (style == Style::Italic || style == Style::BoldItalic)
                {
                    FT_Matrix transform{65536, 13930, 0, 65536}; FT_Outline_Transform(&outline, &transform);
                }
                Bitmap bitmap = Generate(outline, sdf);
                glyph.glyphId = id; glyph.generation = state.generation; glyph.style = style; glyph.sdf = sdf;
                glyph.distanceScale = 2.f * sdf.spread; glyph.distanceBias = -static_cast<float>(sdf.spread);
                const float unit = 64.f * sdf.emSize;
                glyph.advance = state.face->glyph->advance.x / unit + (bold ? 1.f / 64 : 0);
                FT_BBox box; FT_Outline_Get_CBox(&outline, &box);
                glyph.bearingX = box.xMin / unit; glyph.bearingY = box.yMax / unit;
                glyph.plane = bitmap.plane;
                if (bitmap.width && bitmap.height)
                {
                    const uint32_t size = state.limits.pageSize;
                    if (bitmap.width > size || bitmap.height > size) throw std::length_error("Glyph exceeds atlas page");
                    uint32_t index = 0;
                    Shelf placement;
                    for (; index < shelves.size(); ++index)
                    {
                        placement = shelves[index];
                        if (placement.x + bitmap.width > size) { placement.x = 0; placement.y += placement.height; placement.height = 0; }
                        if (placement.y + bitmap.height <= size) break;
                    }
                    if (index == shelves.size())
                    {
                        if (pages.size() >= state.limits.maxPages) throw std::length_error("Font atlas page limit");
                        placement = {};
                        shelves.push_back({}); pages.push_back(nullptr);
                    }
                    auto& writable = changed[index];
                    if (!writable)
                    {
                        writable = pages[index] ? std::make_shared<AtlasPage>(*pages[index]) : std::make_shared<AtlasPage>();
                        writable->generation = state.generation; writable->index = index; writable->size = size;
                        ++writable->revision; writable->pixels.resize(size_t(size) * size, 0);
                        pages[index] = writable;
                    }
                    for (uint32_t y = 0; y < bitmap.height; ++y)
                        std::copy_n(bitmap.pixels.data() + size_t(y) * bitmap.width, bitmap.width,
                            writable->pixels.data() + size_t(placement.y + y) * size + placement.x);
                    glyph.page = index;
                    const float side = static_cast<float>(size);
                    glyph.uv = {placement.x / side, placement.y / side, (placement.x + bitmap.width) / side, (placement.y + bitmap.height) / side};
                    shelves[index] = {placement.x + bitmap.width, placement.y, std::max(placement.height, bitmap.height)};
                }
                additions.emplace(key, glyph); ++statistics.generated;
            }
            result.glyphs.push_back({cp, id == 0, glyph});
        }
        // Copy potentially-allocating result first; map merge below transfers nodes.
        result.pages = pages;
        state.cache.merge(additions); state.pages.swap(pages); state.shelves.swap(shelves);
        ++statistics.batches; statistics.pageUpdates += changed.size(); state.statistics = statistics;
        return result;
    }

    std::vector<float> Font::GetKerning(std::span<const KerningPair> pairs) const
    {
        auto& state = *m_impl; state.RequireOwner();
        if (pairs.size() > state.limits.maxBatch) throw std::length_error("Font kerning batch limit");
        std::vector<float> result; result.reserve(pairs.size());
        for (auto pair : pairs)
        {
            if (pair.left >= static_cast<uint32_t>(state.face->num_glyphs) || pair.right >= static_cast<uint32_t>(state.face->num_glyphs))
                throw std::invalid_argument("Invalid kerning glyph id");
            FT_Vector value{};
            Check(FT_Get_Kerning(state.face, pair.left, pair.right, FT_KERNING_UNSCALED, &value), "Cannot query font kerning");
            result.push_back(static_cast<float>(value.x) / state.face->units_per_EM);
        }
        return result;
    }
}
