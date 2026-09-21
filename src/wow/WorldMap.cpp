#include "wow/WorldMap.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace qpe
{
namespace
{
std::uint32_t ReadU32(std::span<std::uint8_t const> bytes, std::size_t offset)
{
    if (offset + 4 > bytes.size())
        return 0;
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}
}

std::pair<float, float> WorldMapArea::WorldToNormalized(Point point) const
{
    auto const width = right - left;
    auto const height = bottom - top;
    if (std::abs(width) < std::numeric_limits<float>::epsilon() ||
        std::abs(height) < std::numeric_limits<float>::epsilon())
        return { 0.5f, 0.5f };
    return {
        (static_cast<float>(point.y) - left) / width,
        (static_cast<float>(point.x) - top) / height
    };
}

Point WorldMapArea::NormalizedToWorld(float x, float y) const
{
    return {
        static_cast<std::int32_t>(std::lround(top + y * (bottom - top))),
        static_cast<std::int32_t>(std::lround(left + x * (right - left)))
    };
}

DbcReader::DbcReader(std::span<std::uint8_t const> bytes) : bytes_(bytes)
{
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "WDBC", 4) != 0)
        return;
    recordCount_ = ReadU32(bytes, 4);
    fieldCount_ = ReadU32(bytes, 8);
    recordSize_ = ReadU32(bytes, 12);
    stringSize_ = ReadU32(bytes, 16);
    auto const required = 20ull + static_cast<std::uint64_t>(recordCount_) * recordSize_ + stringSize_;
    valid_ = fieldCount_ > 0 && recordSize_ >= fieldCount_ * 4 && required <= bytes.size();
}

std::uint32_t DbcReader::Uint(std::uint32_t row, std::uint32_t field) const
{
    if (!valid_ || row >= recordCount_ || field >= fieldCount_)
        return 0;
    return ReadU32(bytes_, 20ull + static_cast<std::size_t>(row) * recordSize_ + field * 4ull);
}

std::int32_t DbcReader::Int(std::uint32_t row, std::uint32_t field) const
{
    return std::bit_cast<std::int32_t>(Uint(row, field));
}

float DbcReader::Float(std::uint32_t row, std::uint32_t field) const
{
    return std::bit_cast<float>(Uint(row, field));
}

std::string DbcReader::String(std::uint32_t row, std::uint32_t field) const
{
    if (!valid_)
        return {};
    auto const offset = Uint(row, field);
    auto const table = 20ull + static_cast<std::size_t>(recordCount_) * recordSize_;
    if (offset >= stringSize_ || table + offset >= bytes_.size())
        return {};
    auto const* begin = reinterpret_cast<char const*>(bytes_.data() + table + offset);
    auto const maximum = stringSize_ - offset;
    auto const length = strnlen(begin, maximum);
    return std::string(begin, length);
}

std::vector<WorldMapArea> ParseWorldMapAreas(std::span<std::uint8_t const> dbc)
{
    DbcReader reader(dbc);
    std::vector<WorldMapArea> result;
    if (!reader.Valid() || reader.FieldCount() < 10)
        return result;
    result.reserve(reader.RecordCount());
    for (std::uint32_t row = 0; row < reader.RecordCount(); ++row)
    {
        WorldMapArea area;
        area.id = reader.Uint(row, 0);
        area.mapId = reader.Uint(row, 1);
        area.areaId = reader.Uint(row, 2);
        area.textureName = reader.String(row, 3);
        area.left = reader.Float(row, 4);
        area.right = reader.Float(row, 5);
        area.top = reader.Float(row, 6);
        area.bottom = reader.Float(row, 7);
        area.displayMapId = reader.Int(row, 8);
        area.defaultDungeonFloor = reader.Int(row, 9);
        if (!area.textureName.empty())
            result.push_back(std::move(area));
    }
    return result;
}

RgbaImage StitchMapTiles(std::vector<RgbaImage> const& tiles)
{
    constexpr std::uint32_t columns = 4;
    constexpr std::uint32_t rows = 3;
    constexpr std::uint32_t tileSize = 256;
    // The client lays twelve 256x256 textures on a 1002x668 canvas. The final
    // column and row are clipped by the FrameXML, so crop the padded texels too.
    RgbaImage output { 1002, 668 };
    output.pixels.assign(static_cast<std::size_t>(output.width) * output.height * 4, 0);
    for (std::size_t index = 0; index < tiles.size() && index < columns * rows; ++index)
    {
        auto const& tile = tiles[index];
        if (tile.Empty())
            continue;
        auto const dstX = static_cast<std::uint32_t>(index % columns) * tileSize;
        auto const dstY = static_cast<std::uint32_t>(index / columns) * tileSize;
        auto const copyWidth = std::min({ tile.width, tileSize, output.width - dstX });
        auto const copyHeight = std::min({ tile.height, tileSize, output.height - dstY });
        for (std::uint32_t y = 0; y < copyHeight; ++y)
        {
            auto const* source = tile.pixels.data() + static_cast<std::size_t>(y) * tile.width * 4;
            auto* destination = output.pixels.data() +
                (static_cast<std::size_t>(dstY + y) * output.width + dstX) * 4;
            std::memcpy(destination, source, static_cast<std::size_t>(copyWidth) * 4);
        }
    }
    return output;
}
}
