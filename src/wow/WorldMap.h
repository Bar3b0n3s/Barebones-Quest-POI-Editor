#pragma once

#include "Models.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qpe
{
struct RgbaImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
    [[nodiscard]] bool Empty() const { return width == 0 || height == 0 || pixels.empty(); }
};

struct WorldMapArea
{
    std::uint32_t id = 0;
    std::uint32_t mapId = 0;
    std::uint32_t areaId = 0;
    std::string textureName;
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    std::int32_t displayMapId = -1;
    std::int32_t defaultDungeonFloor = 0;

    [[nodiscard]] std::pair<float, float> WorldToNormalized(Point point) const;
    [[nodiscard]] Point NormalizedToWorld(float x, float y) const;
};

struct WorldMapOverlay
{
    std::uint32_t id = 0;
    std::uint32_t mapAreaId = 0;
    std::array<std::uint32_t, 4> areaIds {};
    std::string textureName;
    std::uint32_t textureWidth = 0;
    std::uint32_t textureHeight = 0;
    std::int32_t offsetX = 0;
    std::int32_t offsetY = 0;
};

class DbcReader
{
public:
    explicit DbcReader(std::span<std::uint8_t const> bytes);
    [[nodiscard]] bool Valid() const { return valid_; }
    [[nodiscard]] std::uint32_t RecordCount() const { return recordCount_; }
    [[nodiscard]] std::uint32_t FieldCount() const { return fieldCount_; }
    [[nodiscard]] std::uint32_t Uint(std::uint32_t row, std::uint32_t field) const;
    [[nodiscard]] std::int32_t Int(std::uint32_t row, std::uint32_t field) const;
    [[nodiscard]] float Float(std::uint32_t row, std::uint32_t field) const;
    [[nodiscard]] std::string String(std::uint32_t row, std::uint32_t field) const;

private:
    std::span<std::uint8_t const> bytes_;
    std::uint32_t recordCount_ = 0;
    std::uint32_t fieldCount_ = 0;
    std::uint32_t recordSize_ = 0;
    std::uint32_t stringSize_ = 0;
    bool valid_ = false;
};

[[nodiscard]] std::vector<WorldMapArea> ParseWorldMapAreas(std::span<std::uint8_t const> dbc);
[[nodiscard]] std::vector<WorldMapOverlay> ParseWorldMapOverlays(std::span<std::uint8_t const> dbc);
[[nodiscard]] RgbaImage StitchMapTiles(std::vector<RgbaImage> const& tiles);
[[nodiscard]] RgbaImage StitchImageTiles(std::vector<RgbaImage> const& tiles,
    std::uint32_t width, std::uint32_t height);
void AlphaComposite(RgbaImage& destination, RgbaImage const& source, std::int32_t offsetX, std::int32_t offsetY);
}
