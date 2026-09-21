#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace qpe
{
struct Point
{
    std::int32_t x = 0;
    std::int32_t y = 0;

    bool operator==(Point const&) const = default;
};

struct Poi
{
    std::uint32_t id = 0;
    std::int32_t objectiveIndex = -1;
    std::uint32_t mapId = 0;
    std::uint32_t worldMapAreaId = 0;
    std::uint32_t floor = 0;
    std::uint32_t priority = 0;
    std::uint32_t flags = 0;
    std::int32_t verifiedBuild = 12340;
    std::vector<Point> points;

    bool operator==(Poi const&) const = default;
};

struct Quest
{
    std::uint32_t id = 0;
    std::string title;
    std::vector<Poi> pois;

    bool operator==(Quest const&) const = default;
};

struct QuestSummary
{
    std::uint32_t id = 0;
    std::string title;
};

struct DatabaseSettings
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 3306;
    std::string user = "trinity";
    std::string password;
    std::string database = "world";
};

struct AppSettings
{
    std::string clientPath;
    std::string locale = "enUS";
    int fontSize = 14;
    int windowWidth = 1600;
    int windowHeight = 900;
    DatabaseSettings database;
};

inline void NormalizePoiIds(Quest& quest)
{
    std::sort(quest.pois.begin(), quest.pois.end(), [](Poi const& a, Poi const& b) { return a.id < b.id; });
    for (std::size_t index = 0; index < quest.pois.size(); ++index)
        quest.pois[index].id = static_cast<std::uint32_t>(index);
}
}
