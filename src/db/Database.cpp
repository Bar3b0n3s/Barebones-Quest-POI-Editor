#include "db/Database.h"

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <charconv>
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
    if (!Execute(std::format("SELECT `LogTitle` FROM `quest_template` WHERE `ID`={} LIMIT 1", questId), error))
        return false;
    if (auto* result = storeResult_(connection_))
    {
        if (auto** row = fetchRow_(result); row && row[0])
            quest.title = row[0];
        freeResult_(result);
    }

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
