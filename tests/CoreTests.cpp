#include "Models.h"
#include "wow/BlpDecoder.h"
#include "wow/MpqArchive.h"
#include "wow/WorldMap.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>

int main()
{
    qpe::WorldMapArea area;
    area.left = -1000.0f;
    area.right = 1000.0f;
    area.top = 2000.0f;
    area.bottom = 0.0f;

    qpe::Point original { 500, 250 };
    auto [x, y] = area.WorldToNormalized(original);
    auto roundTrip = area.NormalizedToWorld(x, y);
    assert(std::abs(roundTrip.x - original.x) <= 1);
    assert(std::abs(roundTrip.y - original.y) <= 1);

    qpe::Quest quest;
    quest.pois = { qpe::Poi { .id = 8 }, qpe::Poi { .id = 2 } };
    qpe::NormalizePoiIds(quest);
    assert(quest.pois[0].id == 0 && quest.pois[1].id == 1);

    // One 4x4 DXT1 block whose fourth selector must remain transparent.
    std::vector<std::uint8_t> blp(148 + 8, 0);
    std::memcpy(blp.data(), "BLP2", 4);
    blp[8] = 2; // DXT
    blp[9] = 1; // 1-bit alpha
    blp[10] = 0; // DXT1
    auto put32 = [&blp](std::size_t at, std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) blp[at + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    };
    put32(12, 4); put32(16, 4); put32(20, 148); put32(84, 8);
    // c0 <= c1 selects three-color + transparent mode; all selectors are 3.
    blp[148] = 0; blp[149] = 0; blp[150] = 0xff; blp[151] = 0xff;
    put32(152, 0xffffffffu);
    std::string decodeError;
    auto decoded = qpe::DecodeBlp(blp, decodeError);
    assert(!decoded.Empty() && decoded.pixels[3] == 0);

    std::vector<std::uint8_t> overlayDbcFixture(20 + 17 * 4 + 11, 0);
    std::memcpy(overlayDbcFixture.data(), "WDBC", 4);
    auto putDbc32 = [&overlayDbcFixture](std::size_t at, std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte)
            overlayDbcFixture[at + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    };
    putDbc32(4, 1); putDbc32(8, 17); putDbc32(12, 17 * 4); putDbc32(16, 11);
    auto const record = 20u;
    putDbc32(record + 0 * 4, 7); putDbc32(record + 1 * 4, 42); putDbc32(record + 2 * 4, 8);
    putDbc32(record + 8 * 4, 1); putDbc32(record + 9 * 4, 160); putDbc32(record + 10 * 4, 210);
    putDbc32(record + 11 * 4, 382); putDbc32(record + 12 * 4, 281);
    std::memcpy(overlayDbcFixture.data() + 20 + 17 * 4 + 1, "Astranaar", 9);
    auto parsedOverlays = qpe::ParseWorldMapOverlays(overlayDbcFixture);
    assert(parsedOverlays.size() == 1 && parsedOverlays[0].id == 7 && parsedOverlays[0].mapAreaId == 42);
    assert(parsedOverlays[0].areaIds[0] == 8 && parsedOverlays[0].textureName == "Astranaar");
    assert(parsedOverlays[0].textureWidth == 160 && parsedOverlays[0].textureHeight == 210);
    assert(parsedOverlays[0].offsetX == 382 && parsedOverlays[0].offsetY == 281);

    qpe::RgbaImage base { 2, 1, { 20, 40, 60, 255, 10, 20, 30, 255 } };
    qpe::RgbaImage overlay { 2, 1, { 220, 140, 60, 128, 255, 0, 0, 0 } };
    qpe::AlphaComposite(base, overlay, 0, 0);
    assert(base.pixels[0] == 120 && base.pixels[1] == 90 && base.pixels[2] == 60 && base.pixels[3] == 255);
    assert(base.pixels[4] == 10 && base.pixels[5] == 20 && base.pixels[6] == 30 && base.pixels[7] == 255);

    qpe::RgbaImage redTile { 256, 1 };
    redTile.pixels.assign(256 * 4, 0);
    redTile.pixels[0] = redTile.pixels[3] = 255;
    qpe::RgbaImage greenTile { 256, 1 };
    greenTile.pixels.assign(256 * 4, 0);
    greenTile.pixels[1] = greenTile.pixels[3] = 255;
    auto tiled = qpe::StitchImageTiles({ redTile, greenTile }, 300, 1);
    assert(tiled.width == 300 && tiled.height == 1);
    assert(tiled.pixels[0] == 255 && tiled.pixels[256 * 4 + 1] == 255);

    if (auto const* client = std::getenv("QPE_CLIENT_PATH"))
    {
        qpe::MpqArchiveSet archives;
        std::string error;
        auto const* configuredLocale = std::getenv("QPE_CLIENT_LOCALE");
        auto const locale = configuredLocale ? configuredLocale : "enUS";
        assert(archives.Open(std::filesystem::path(client), locale, error));
        auto dbc = archives.Read("DBFilesClient\\WorldMapArea.dbc", error);
        assert(!dbc.empty());
        auto areas = qpe::ParseWorldMapAreas(dbc);
        assert(!areas.empty());
        auto overlayDbc = archives.Read("DBFilesClient\\WorldMapOverlay.dbc", error);
        assert(!overlayDbc.empty());
        auto overlays = qpe::ParseWorldMapOverlays(overlayDbc);
        assert(!overlays.empty());
        std::cout << "Client integration: " << archives.ArchiveCount() << " sources, "
                  << areas.size() << " world-map areas, " << overlays.size() << " reveal overlays\n";

        std::vector<qpe::RgbaImage> grizzlyTiles;
        for (int tile = 1; tile <= 12; ++tile)
        {
            auto const path = "Interface\\WorldMap\\GrizzlyHills\\GrizzlyHills" + std::to_string(tile) + ".blp";
            auto bytes = archives.Read(path, error);
            assert(!bytes.empty());
            auto image = qpe::DecodeBlp(bytes, error);
            assert(!image.Empty());
            grizzlyTiles.push_back(std::move(image));
        }
        auto grizzlyMap = qpe::StitchMapTiles(grizzlyTiles);
        assert(grizzlyMap.width == 1002 && grizzlyMap.height == 668 && !grizzlyMap.Empty());
        std::cout << "Client map decode: GrizzlyHills (12 tiles)\n";

        bool decodedOverlay = false;
        for (auto const& overlay : overlays)
        {
            auto area = std::find_if(areas.begin(), areas.end(), [&overlay](auto const& candidate) {
                return candidate.id == overlay.mapAreaId;
            });
            if (area == areas.end())
                continue;
            auto const columns = (overlay.textureWidth + 255u) / 256u;
            auto const rows = (overlay.textureHeight + 255u) / 256u;
            auto const count = static_cast<std::size_t>(columns) * rows;
            std::vector<qpe::RgbaImage> overlayTiles;
            for (std::size_t tile = 1; tile <= count; ++tile)
            {
                auto const path = "Interface\\WorldMap\\" + area->textureName + "\\" +
                    overlay.textureName + std::to_string(tile) + ".blp";
                auto bytes = archives.Read(path, error);
                if (bytes.empty())
                    break;
                auto image = qpe::DecodeBlp(bytes, error);
                if (image.Empty())
                    break;
                overlayTiles.push_back(std::move(image));
            }
            if (overlayTiles.size() != count)
                continue;
            auto image = qpe::StitchImageTiles(overlayTiles, overlay.textureWidth, overlay.textureHeight);
            assert(!image.Empty());
            qpe::AlphaComposite(grizzlyMap, image, overlay.offsetX, overlay.offsetY);
            std::cout << "Client overlay decode: " << area->textureName << "/" << overlay.textureName
                      << " (" << count << " tile(s), " << overlay.textureWidth << "x"
                      << overlay.textureHeight << " at " << overlay.offsetX << "," << overlay.offsetY << ")\n";
            decodedOverlay = true;
            break;
        }
        assert(decodedOverlay);

        bool decodedMap = false;
        for (auto const& area : areas)
        {
            std::vector<qpe::RgbaImage> tiles;
            for (int tile = 1; tile <= 12; ++tile)
            {
                auto path = "Interface\\WorldMap\\" + area.textureName + "\\" +
                    area.textureName + std::to_string(tile) + ".blp";
                auto bytes = archives.Read(path, error);
                if (bytes.empty())
                    break;
                auto image = qpe::DecodeBlp(bytes, error);
                if (image.Empty())
                    break;
                tiles.push_back(std::move(image));
            }
            if (tiles.size() == 12)
            {
                auto stitched = qpe::StitchMapTiles(tiles);
                assert(stitched.width == 1002 && stitched.height == 668 && !stitched.Empty());
                std::cout << "Client map decode: " << area.textureName << " (12 tiles)\n";
                decodedMap = true;
                break;
            }
        }
        assert(decodedMap);
    }

    std::cout << "Core tests passed\n";
}
