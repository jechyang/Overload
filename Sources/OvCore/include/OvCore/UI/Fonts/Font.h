#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace OvCore::UI::Fonts
{
    enum class Style : uint32_t { Normal, Bold, Italic, BoldItalic };

    struct SdfSettings
    {
        uint32_t emSize = 64;
        uint32_t spread = 8;
        bool operator==(const SdfSettings&) const = default;
    };

    struct Limits
    {
        uint32_t pageSize = 512;
        uint32_t maxPages = 16;
        uint32_t maxGlyphs = 4096;
        uint32_t maxBatch = 16384;
    };

    struct Bounds { float left = 0, bottom = 0, right = 0, top = 0; };
    struct Metrics { float ascender = 0, descender = 0, lineHeight = 0; };

    // Metrics and plane bounds are in em units, independent of requested text size.
    // Texture rows and UVs use bottom-left origin. Positive distance is inside.
    struct Glyph
    {
        uint32_t glyphId = 0;
        uint32_t page = UINT32_MAX; // Spaces/empty outlines have no texture or quad.
        uint64_t generation = 0;
        Style style = Style::Normal;
        SdfSettings sdf;
        float advance = 0, bearingX = 0, bearingY = 0;
        Bounds plane, uv;
        float distanceScale = 16, distanceBias = -8; // texel * scale + bias = SDF pixels
    };

    struct GlyphMapping
    {
        uint32_t scalar = 0;
        bool missing = false;
        Glyph glyph;
    };

    // Immutable CPU upload payload. A new revision never mutates an older lease.
    // GPU integration keys textures by (generation, index, revision) and must pin
    // its GPU object in B's frame snapshot, never query Font from RenderThread.
    struct AtlasPage
    {
        uint64_t generation = 0, revision = 0;
        uint32_t index = 0, size = 0;
        std::vector<uint8_t> pixels; // R8 UNORM, linear, no sRGB or mipmaps
    };

    struct Batch
    {
        uint64_t generation = 0;
        Metrics metrics;
        std::vector<GlyphMapping> glyphs; // unique scalars, first occurrence order
        std::vector<std::shared_ptr<const AtlasPage>> pages;
    };

    struct KerningPair { uint32_t left = 0, right = 0; }; // glyph ids, not scalars
    struct Statistics { uint64_t batches = 0, generated = 0, hits = 0, pageUpdates = 0; };

    // Native resource; only the construction thread may use FreeType/pack/update.
    // Batch/Page leases are standalone and safe to read after Font destruction.
    class Font final
    {
    public:
        explicit Font(const std::filesystem::path& path, Limits limits = {}, int32_t faceIndex = 0,
            std::vector<float> variationCoordinates = {});
        ~Font();
        Font(const Font&) = delete;
        Font& operator=(const Font&) = delete;
        void Reload(const std::filesystem::path& path);
        Batch EnsureGlyphs(std::span<const uint32_t> scalars, Style style = Style::Normal, SdfSettings sdf = {});
        std::vector<float> GetKerning(std::span<const KerningPair> pairs) const;
        Metrics GetMetrics() const;
        uint64_t Generation() const;
        Statistics GetStatistics() const;
        static bool IsUnicodeScalar(uint32_t value);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
