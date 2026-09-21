#include "db/Database.h"

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <charconv>
#include <array>
#include <cstdlib>
#include <format>
#include <filesystem>
#include <unordered_map>

namespace qpe
{
namespace
{
#if defined(_WIN32)
void* OpenLibrary(char const* name)
{
    return LoadLibraryA(name);
}

void* FindSymbol(void* library, char const* name)
{
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
}

void CloseLibrary(void* library)
{
    FreeLibrary(static_cast<HMODULE>(library));
}
#else
void* OpenLibrary(char const* name)
{
    if (auto* library = dlopen(name, RTLD_NOW | RTLD_LOCAL))
        return library;
    std::string executable(4096, '\0');
    auto const length = readlink("/proc/self/exe", executable.data(), executable.size());
    if (length <= 0)
        return nullptr;
    executable.resize(static_cast<std::size_t>(length));
    auto const adjacent = std::filesystem::path(executable).parent_path() / name;
    return dlopen(adjacent.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void* FindSymbol(void* library, char const* name)
{
    return dlsym(library, name);
}

void CloseLibrary(void* library)
{
    dlclose(library);
}
#endif

template <typename T>
T Number(char const* value, T fallback = {})
{
    if (!value)
        return fallback;
    T result {};
    auto const end = value + std::char_traits<char>::length(value);
    auto [pointer, code] = std::from_chars(value, end, result);
    return code == std::errc {} && pointer == end ? result : fallback;
}
}

Database::~Database() { Disconnect(); }

bool Database::Connect(DatabaseSettings const& settings, std::string& error)
{
    Disconnect();
    error.clear();
    #if defined(_WIN32)
    constexpr char const* libraries[] { "libmariadb.dll", "libmysql.dll" };
    #else
    constexpr char const* libraries[] { "libmariadb.so.3", "libmariadb.so", "libmysqlclient.so" };
    #endif
    for (auto const* name : libraries)
    {
        library_ = OpenLibrary(name);
        if (library_)
            break;
    }
    if (!library_)
    {
        error = "A MariaDB/MySQL client library was not found next to the editor or in the system library path";
        return false;
    }
    auto symbol = [this](char const* name) { return FindSymbol(library_, name); };
    init_ = reinterpret_cast<Init>(symbol("mysql_init"));
    realConnect_ = reinterpret_cast<RealConnect>(symbol("mysql_real_connect"));
    close_ = reinterpret_cast<Close>(symbol("mysql_close"));
    query_ = reinterpret_cast<Query>(symbol("mysql_query"));
    storeResult_ = reinterpret_cast<StoreResult>(symbol("mysql_store_result"));
    fetchRow_ = reinterpret_cast<FetchRow>(symbol("mysql_fetch_row"));
    freeResult_ = reinterpret_cast<FreeResult>(symbol("mysql_free_result"));
    error_ = reinterpret_cast<Error>(symbol("mysql_error"));
    setCharset_ = reinterpret_cast<SetCharset>(symbol("mysql_set_character_set"));
    autoCommit_ = reinterpret_cast<AutoCommit>(symbol("mysql_autocommit"));
    commit_ = reinterpret_cast<Commit>(symbol("mysql_commit"));
    rollback_ = reinterpret_cast<Rollback>(symbol("mysql_rollback"));
    if (!init_ || !realConnect_ || !close_ || !query_ || !storeResult_ || !fetchRow_ ||
        !freeResult_ || !error_ || !setCharset_ || !autoCommit_ || !commit_ || !rollback_)
    {
        error = "The MariaDB/MySQL client library is missing required API exports";
        Disconnect();
        return false;
    }
    connection_ = init_(nullptr);
    if (!connection_)
    {
        error = "MariaDB could not allocate a connection";
        Disconnect();
        return false;
    }
    if (!realConnect_(connection_, settings.host.c_str(), settings.user.c_str(), settings.password.c_str(),
                      settings.database.c_str(), settings.port, nullptr, 0))
    {
        error = LastError();
        Disconnect();
        return false;
    }
    if (setCharset_(connection_, "utf8mb4") != 0)
    {
        error = LastError();
        Disconnect();
        return false;
    }
    return true;
}

void Database::Disconnect()
{
    if (connection_ && close_)
        close_(connection_);
    connection_ = nullptr;
    if (library_)
        CloseLibrary(library_);
    library_ = nullptr;
    init_ = nullptr; realConnect_ = nullptr; close_ = nullptr; query_ = nullptr;
    storeResult_ = nullptr; fetchRow_ = nullptr; freeResult_ = nullptr; error_ = nullptr;
    setCharset_ = nullptr; autoCommit_ = nullptr; commit_ = nullptr; rollback_ = nullptr;
}

bool Database::Execute(std::string const& sql, std::string& error)
{
    if (!connection_)
    {
        error = "Not connected to the world database";
        return false;
    }
    if (query_(connection_, sql.c_str()) != 0)
    {
        error = LastError();
        return false;
    }
    return true;
}

std::string Database::LastError() const
{
    if (!connection_ || !error_)
        return "Database connection is unavailable";
    auto const* text = error_(connection_);
    return text && *text ? text : "Unknown database error";
}

bool Database::LoadQuest(std::uint32_t questId, Quest& quest, std::string& error)
{
    quest = Quest { .id = questId };
    if (!Execute(std::format(
        "SELECT q.`LogTitle`,q.`RequiredPlayerKills`,"
        "q.`RequiredNpcOrGo1`,q.`RequiredNpcOrGo2`,q.`RequiredNpcOrGo3`,q.`RequiredNpcOrGo4`,"
        "q.`RequiredNpcOrGoCount1`,q.`RequiredNpcOrGoCount2`,q.`RequiredNpcOrGoCount3`,q.`RequiredNpcOrGoCount4`,"
        "q.`RequiredItemId1`,q.`RequiredItemId2`,q.`RequiredItemId3`,q.`RequiredItemId4`,q.`RequiredItemId5`,q.`RequiredItemId6`,"
        "q.`RequiredItemCount1`,q.`RequiredItemCount2`,q.`RequiredItemCount3`,q.`RequiredItemCount4`,q.`RequiredItemCount5`,q.`RequiredItemCount6`,"
        "COALESCE(q.`ObjectiveText1`,''),COALESCE(q.`ObjectiveText2`,''),COALESCE(q.`ObjectiveText3`,''),COALESCE(q.`ObjectiveText4`,''),"
        "COALESCE(a.`SpecialFlags`,0) FROM `quest_template` q LEFT JOIN `quest_template_addon` a ON a.`ID`=q.`ID` "
        "WHERE q.`ID`={} LIMIT 1", questId), error))
        return false;
    std::array<std::int32_t, 4> targetIds {};
    std::array<std::uint32_t, 4> targetCounts {};
    std::array<std::uint32_t, 6> itemIds {};
    std::array<std::uint32_t, 6> itemCounts {};
    std::array<std::string, 4> objectiveTexts {};
    std::uint32_t playerKills = 0;
    std::uint32_t specialFlags = 0;
    if (auto* result = storeResult_(connection_))
    {
        if (auto** row = fetchRow_(result); row)
        {
            quest.title = row[0] ? row[0] : "";
            playerKills = Number<std::uint32_t>(row[1]);
            for (std::size_t index = 0; index < targetIds.size(); ++index)
            {
                targetIds[index] = Number<std::int32_t>(row[2 + index]);
                targetCounts[index] = Number<std::uint32_t>(row[6 + index]);
                objectiveTexts[index] = row[22 + index] ? row[22 + index] : "";
            }
            for (std::size_t index = 0; index < itemIds.size(); ++index)
            {
                itemIds[index] = Number<std::uint32_t>(row[10 + index]);
                itemCounts[index] = Number<std::uint32_t>(row[16 + index]);
            }
            specialFlags = Number<std::uint32_t>(row[26]);
        }
        freeResult_(result);
    }

    auto loadName = [&](char const* table, std::uint32_t entry, std::string& name) {
        if (!Execute(std::format("SELECT `name` FROM `{}` WHERE `entry`={} LIMIT 1", table, entry), error))
            return false;
        if (auto* result = storeResult_(connection_))
        {
            if (auto** row = fetchRow_(result); row && row[0])
                name = row[0];
            freeResult_(result);
        }
        return true;
    };
    std::unordered_map<std::uint32_t, std::vector<MovementPoint>> waypointCache;
    auto loadSpawns = [&](QuestRequirement& requirement, bool itemSources) {
        std::vector<std::pair<std::string, SpawnKind>> queries;
        if (itemSources)
        {
            queries.emplace_back(std::format(
                "SELECT DISTINCT c.`id`,ct.`name`,c.`map`,c.`position_x`,c.`position_y`,c.`MovementType`,c.`wander_distance`,"
                "COALESCE(ca.`path_id`,cta.`path_id`,0) FROM `creature_questitem` qi "
                "JOIN `creature` c ON c.`id`=qi.`CreatureEntry` JOIN `creature_template` ct ON ct.`entry`=c.`id` "
                "LEFT JOIN `creature_addon` ca ON ca.`guid`=c.`guid` "
                "LEFT JOIN `creature_template_addon` cta ON cta.`entry`=c.`id` "
                "WHERE qi.`ItemId`={}", requirement.entry), SpawnKind::Creature);
            queries.emplace_back(std::format(
                "SELECT DISTINCT g.`id`,gt.`name`,g.`map`,g.`position_x`,g.`position_y` FROM `gameobject_questitem` qi "
                "JOIN `gameobject` g ON g.`id`=qi.`GameObjectEntry` JOIN `gameobject_template` gt ON gt.`entry`=g.`id` "
                "WHERE qi.`ItemId`={}", requirement.entry), SpawnKind::GameObject);
        }
        else if (requirement.kind == RequirementKind::Creature)
        {
            queries.emplace_back(std::format(
                "SELECT c.`id`,ct.`name`,c.`map`,c.`position_x`,c.`position_y`,c.`MovementType`,c.`wander_distance`,"
                "COALESCE(ca.`path_id`,cta.`path_id`,0) FROM `creature` c "
                "JOIN `creature_template` ct ON ct.`entry`=c.`id` "
                "LEFT JOIN `creature_addon` ca ON ca.`guid`=c.`guid` "
                "LEFT JOIN `creature_template_addon` cta ON cta.`entry`=c.`id` "
                "WHERE c.`id`={} OR ct.`KillCredit1`={} OR ct.`KillCredit2`={}",
                requirement.entry, requirement.entry, requirement.entry), SpawnKind::Creature);
        }
        else if (requirement.kind == RequirementKind::GameObject)
        {
            queries.emplace_back(std::format(
                "SELECT g.`id`,gt.`name`,g.`map`,g.`position_x`,g.`position_y` FROM `gameobject` g "
                "JOIN `gameobject_template` gt ON gt.`entry`=g.`id` WHERE g.`id`={}",
                requirement.entry), SpawnKind::GameObject);
        }

        for (auto const& [sql, kind] : queries)
        {
            if (!Execute(sql, error))
                return false;
            auto const firstSpawn = requirement.spawns.size();
            if (auto* result = storeResult_(connection_))
            {
                while (auto** row = fetchRow_(result))
                {
                    QuestSpawn spawn {
                        .kind = kind,
                        .entry = Number<std::uint32_t>(row[0]),
                        .name = row[1] ? row[1] : "",
                        .mapId = Number<std::uint32_t>(row[2]),
                        .x = Number<float>(row[3]),
                        .y = Number<float>(row[4])
                    };
                    if (kind == SpawnKind::Creature)
                    {
                        spawn.movementType = Number<std::uint32_t>(row[5]);
                        spawn.wanderDistance = Number<float>(row[6]);
                        spawn.pathId = Number<std::uint32_t>(row[7]);
                    }
                    requirement.spawns.push_back(std::move(spawn));
                }
                freeResult_(result);
            }
            for (auto index = firstSpawn; index < requirement.spawns.size(); ++index)
            {
                auto& spawn = requirement.spawns[index];
                if (spawn.kind != SpawnKind::Creature || spawn.pathId == 0)
                    continue;
                if (auto found = waypointCache.find(spawn.pathId); found != waypointCache.end())
                {
                    spawn.pathPoints = found->second;
                    continue;
                }
                std::vector<MovementPoint> points;
                if (!Execute(std::format(
                    "SELECT `position_x`,`position_y` FROM `waypoint_data` WHERE `id`={} ORDER BY `point`",
                    spawn.pathId), error))
                    return false;
                if (auto* result = storeResult_(connection_))
                {
                    while (auto** row = fetchRow_(result))
                        points.push_back({ Number<float>(row[0]), Number<float>(row[1]) });
                    freeResult_(result);
                }
                spawn.pathPoints = points;
                waypointCache.emplace(spawn.pathId, std::move(points));
            }
        }
        return true;
    };

    for (std::size_t index = 0; index < targetIds.size(); ++index)
    {
        if (targetIds[index] == 0)
        {
            if (!objectiveTexts[index].empty())
                quest.requirements.push_back(QuestRequirement {
                    .kind = RequirementKind::ObjectiveText,
                    .objectiveIndex = static_cast<std::int32_t>(index),
                    .name = objectiveTexts[index],
                    .objectiveText = objectiveTexts[index]
                });
            continue;
        }
        auto const creature = targetIds[index] > 0;
        auto const entry = static_cast<std::uint32_t>(creature ? targetIds[index] : -static_cast<std::int64_t>(targetIds[index]));
        QuestRequirement requirement {
            .kind = creature ? RequirementKind::Creature : RequirementKind::GameObject,
            .objectiveIndex = static_cast<std::int32_t>(index),
            .entry = entry,
            .count = targetCounts[index],
            .objectiveText = objectiveTexts[index]
        };
        if (!loadName(creature ? "creature_template" : "gameobject_template", entry, requirement.name) ||
            !loadSpawns(requirement, false))
            return false;
        if (requirement.name.empty())
            requirement.name = std::format("{} {}", creature ? "Creature" : "Game object", entry);
        quest.requirements.push_back(std::move(requirement));
    }

    for (std::size_t index = 0; index < itemIds.size(); ++index)
    {
        if (itemIds[index] == 0)
            continue;
        QuestRequirement requirement {
            .kind = RequirementKind::Item,
            .entry = itemIds[index],
            .count = itemCounts[index]
        };
        if (!loadName("item_template", requirement.entry, requirement.name) || !loadSpawns(requirement, true))
            return false;
        if (requirement.name.empty())
            requirement.name = std::format("Item {}", requirement.entry);
        quest.requirements.push_back(std::move(requirement));
    }
    if (playerKills > 0)
        quest.requirements.push_back(QuestRequirement {
            .kind = RequirementKind::PlayerKills,
            .count = playerKills,
            .name = "Enemy players"
        });
    if ((specialFlags & 0x002u) != 0)
        quest.requirements.push_back(QuestRequirement {
            .kind = RequirementKind::ExplorationOrEvent,
            .count = 1,
            .name = "Explore an area or complete a scripted event"
        });

    if (!Execute(std::format(
        "SELECT `id`,`ObjectiveIndex`,`MapID`,`WorldMapAreaId`,`Floor`,`Priority`,`Flags`,COALESCE(`VerifiedBuild`,12340) "
        "FROM `quest_poi` WHERE `QuestID`={} ORDER BY `id`", questId), error))
        return false;
    if (auto* result = storeResult_(connection_))
    {
        while (auto** row = fetchRow_(result))
        {
            quest.pois.push_back(Poi {
                .id = Number<std::uint32_t>(row[0]),
                .objectiveIndex = Number<std::int32_t>(row[1], -1),
                .mapId = Number<std::uint32_t>(row[2]),
                .worldMapAreaId = Number<std::uint32_t>(row[3]),
                .floor = Number<std::uint32_t>(row[4]),
                .priority = Number<std::uint32_t>(row[5]),
                .flags = Number<std::uint32_t>(row[6]),
                .verifiedBuild = Number<std::int32_t>(row[7], 12340)
            });
        }
        freeResult_(result);
    }

    std::unordered_map<std::uint32_t, std::size_t> poiById;
    for (std::size_t index = 0; index < quest.pois.size(); ++index)
        poiById.emplace(quest.pois[index].id, index);
    if (!Execute(std::format(
        "SELECT `Idx1`,`X`,`Y` FROM `quest_poi_points` WHERE `QuestID`={} ORDER BY `Idx1`,`Idx2`", questId), error))
        return false;
    if (auto* result = storeResult_(connection_))
    {
        while (auto** row = fetchRow_(result))
            if (auto found = poiById.find(Number<std::uint32_t>(row[0])); found != poiById.end())
                quest.pois[found->second].points.push_back({ Number<std::int32_t>(row[1]), Number<std::int32_t>(row[2]) });
        freeResult_(result);
    }
    return true;
}

bool Database::LoadQuestList(std::vector<QuestSummary>& quests, std::string& error)
{
    quests.clear();
    if (!Execute("SELECT `ID`,COALESCE(`LogTitle`,'') FROM `quest_template` ORDER BY `ID`", error))
        return false;
    auto* result = storeResult_(connection_);
    if (!result)
    {
        error = LastError();
        return false;
    }
    while (auto** row = fetchRow_(result))
        quests.push_back(QuestSummary { Number<std::uint32_t>(row[0]), row[1] ? row[1] : "" });
    freeResult_(result);
    return true;
}

bool Database::SaveQuest(Quest const& quest, std::string& error)
{
    if (!connection_)
    {
        error = "Not connected to the world database";
        return false;
    }
    if (autoCommit_(connection_, 0) != 0)
    {
        error = LastError();
        return false;
    }
    auto fail = [&](std::string const& message) {
        rollback_(connection_);
        autoCommit_(connection_, 1);
        error = message;
        return false;
    };
    std::string dbError;
    if (!Execute(std::format("DELETE FROM `quest_poi_points` WHERE `QuestID`={}", quest.id), dbError) ||
        !Execute(std::format("DELETE FROM `quest_poi` WHERE `QuestID`={}", quest.id), dbError))
        return fail(dbError);

    for (auto const& poi : quest.pois)
    {
        auto sql = std::format(
            "INSERT INTO `quest_poi` (`QuestID`,`id`,`ObjectiveIndex`,`MapID`,`WorldMapAreaId`,`Floor`,`Priority`,`Flags`,`VerifiedBuild`) "
            "VALUES ({},{},{},{},{},{},{},{},{})",
            quest.id, poi.id, poi.objectiveIndex, poi.mapId, poi.worldMapAreaId, poi.floor,
            poi.priority, poi.flags, poi.verifiedBuild);
        if (!Execute(sql, dbError))
            return fail(dbError);
        for (std::size_t index = 0; index < poi.points.size(); ++index)
        {
            auto const& point = poi.points[index];
            sql = std::format(
                "INSERT INTO `quest_poi_points` (`QuestID`,`Idx1`,`Idx2`,`X`,`Y`,`VerifiedBuild`) "
                "VALUES ({},{},{},{},{},{})",
                quest.id, poi.id, index, point.x, point.y, poi.verifiedBuild);
            if (!Execute(sql, dbError))
                return fail(dbError);
        }
    }
    if (commit_(connection_) != 0)
        return fail(LastError());
    autoCommit_(connection_, 1);
    return true;
}
}
