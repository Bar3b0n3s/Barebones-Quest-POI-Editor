#include "Models.h"
#include "wow/BlpDecoder.h"
#include "wow/MpqArchive.h"
#include "wow/WorldMap.h"

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
        std::cout << "Client integration: " << archives.ArchiveCount() << " sources, "
                  << areas.size() << " world-map areas\n";

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
