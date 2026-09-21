#include "Models.h"
#include "Platform.h"
#include "db/Database.h"

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

namespace
{
using Init = void*(QPE_CALL*)(void*);
using RealConnect = void*(QPE_CALL*)(void*, char const*, char const*, char const*, char const*, unsigned int, char const*, unsigned long);
using Close = void(QPE_CALL*)(void*);
using Query = int(QPE_CALL*)(void*, char const*);
using Error = char const*(QPE_CALL*)(void*);

#if defined(_WIN32)
void* OpenLibrary(char const* name) { return LoadLibraryA(name); }
void* FindSymbol(void* library, char const* name)
{
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
}
void CloseLibrary(void* library) { FreeLibrary(static_cast<HMODULE>(library)); }
unsigned long ProcessId() { return GetCurrentProcessId(); }
#else
void* OpenLibrary(char const* name) { return dlopen(name, RTLD_NOW | RTLD_LOCAL); }
void* FindSymbol(void* library, char const* name) { return dlsym(library, name); }
void CloseLibrary(void* library) { dlclose(library); }
unsigned long ProcessId() { return static_cast<unsigned long>(getpid()); }
#endif

struct AdminConnection
{
    void* library = nullptr;
    void* connection = nullptr;
    Close close = nullptr;
    Query query = nullptr;
    Error error = nullptr;

    ~AdminConnection()
    {
        if (connection && close)
            close(connection);
        if (library)
            CloseLibrary(library);
    }

    bool Execute(std::string const& sql)
    {
        if (query(connection, sql.c_str()) == 0)
            return true;
        std::cerr << "SQL failed: " << (error ? error(connection) : "unknown error") << '\n';
        return false;
    }
};

unsigned int PortFromEnvironment()
{
    auto const* value = std::getenv("QPE_TEST_DB_PORT");
    if (!value)
        return 3306;
    unsigned int port = 3306;
    auto const end = value + std::char_traits<char>::length(value);
    auto const [pointer, code] = std::from_chars(value, end, port);
    return code == std::errc {} && pointer == end ? port : 3306;
}
}

int main()
{
    auto const* host = std::getenv("QPE_TEST_DB_HOST");
    auto const* user = std::getenv("QPE_TEST_DB_USER");
    auto const* password = std::getenv("QPE_TEST_DB_PASSWORD");
    if (!host || !user)
    {
        std::cout << "Database integration test skipped. Set QPE_TEST_DB_HOST and QPE_TEST_DB_USER to run it.\n";
        return 0;
    }
    if (!password)
        password = "";

    AdminConnection admin;
    #if defined(_WIN32)
    constexpr char const* libraries[] { "libmariadb.dll", "libmysql.dll" };
    #else
    constexpr char const* libraries[] { "libmariadb.so.3", "libmariadb.so", "libmysqlclient.so" };
    #endif
    for (auto const* name : libraries)
        if ((admin.library = OpenLibrary(name)))
            break;
    if (!admin.library)
    {
        std::cerr << "MariaDB/MySQL client library was not found.\n";
        return 1;
    }
    auto symbol = [&](char const* name) { return FindSymbol(admin.library, name); };
    auto init = reinterpret_cast<Init>(symbol("mysql_init"));
    auto connect = reinterpret_cast<RealConnect>(symbol("mysql_real_connect"));
    admin.close = reinterpret_cast<Close>(symbol("mysql_close"));
    admin.query = reinterpret_cast<Query>(symbol("mysql_query"));
    admin.error = reinterpret_cast<Error>(symbol("mysql_error"));
    if (!init || !connect || !admin.close || !admin.query || !admin.error)
    {
        std::cerr << "Client library does not expose the required test API.\n";
        return 1;
    }
    admin.connection = init(nullptr);
    auto const port = PortFromEnvironment();
    if (!admin.connection || !connect(admin.connection, host, user, password, nullptr, port, nullptr, 0))
    {
        std::cerr << "Administrative test connection failed: "
                  << (admin.connection ? admin.error(admin.connection) : "mysql_init failed") << '\n';
        return 1;
    }

    auto const timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto const databaseName = std::format("qpe_test_{}_{}", ProcessId(), timestamp);
    auto cleanup = [&] { admin.Execute("DROP DATABASE IF EXISTS `" + databaseName + "`"); };
    if (!admin.Execute("CREATE DATABASE `" + databaseName + "` CHARACTER SET utf8mb4") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`quest_template` ("
            "`ID` INT UNSIGNED NOT NULL PRIMARY KEY, `LogTitle` TEXT NOT NULL, `RequiredPlayerKills` INT UNSIGNED NOT NULL DEFAULT 0, "
            "`RequiredNpcOrGo1` INT NOT NULL DEFAULT 0, `RequiredNpcOrGo2` INT NOT NULL DEFAULT 0, "
            "`RequiredNpcOrGo3` INT NOT NULL DEFAULT 0, `RequiredNpcOrGo4` INT NOT NULL DEFAULT 0, "
            "`RequiredNpcOrGoCount1` INT UNSIGNED NOT NULL DEFAULT 0, `RequiredNpcOrGoCount2` INT UNSIGNED NOT NULL DEFAULT 0, "
            "`RequiredNpcOrGoCount3` INT UNSIGNED NOT NULL DEFAULT 0, `RequiredNpcOrGoCount4` INT UNSIGNED NOT NULL DEFAULT 0, "
            "`RequiredItemId1` INT UNSIGNED NOT NULL DEFAULT 0, `RequiredItemId2` INT UNSIGNED NOT NULL DEFAULT 0, "
            "`RequiredItemId3` INT UNSIGNED NOT NULL DEFAULT 0, `RequiredItemId4` INT UNSIGNED NOT NULL DEFAULT 0, "
            "`RequiredItemId5` INT UNSIGNED NOT NULL DEFAULT 0, `RequiredItemId6` INT UNSIGNED NOT NULL DEFAULT 0, "
            "`RequiredItemCount1` INT UNSIGNED NOT NULL DEFAULT 0, `RequiredItemCount2` INT UNSIGNED NOT NULL DEFAULT 0, "
            "`RequiredItemCount3` INT UNSIGNED NOT NULL DEFAULT 0, `RequiredItemCount4` INT UNSIGNED NOT NULL DEFAULT 0, "
            "`RequiredItemCount5` INT UNSIGNED NOT NULL DEFAULT 0, `RequiredItemCount6` INT UNSIGNED NOT NULL DEFAULT 0, "
            "`ObjectiveText1` TEXT, `ObjectiveText2` TEXT, `ObjectiveText3` TEXT, `ObjectiveText4` TEXT) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`quest_template_addon` ("
            "`ID` INT UNSIGNED NOT NULL PRIMARY KEY, `SpecialFlags` INT UNSIGNED NOT NULL DEFAULT 0) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`creature_template` ("
            "`entry` INT UNSIGNED NOT NULL PRIMARY KEY, `KillCredit1` INT UNSIGNED NOT NULL DEFAULT 0, "
            "`KillCredit2` INT UNSIGNED NOT NULL DEFAULT 0, `name` VARCHAR(100) NOT NULL) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`gameobject_template` ("
            "`entry` INT UNSIGNED NOT NULL PRIMARY KEY, `name` VARCHAR(100) NOT NULL) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`item_template` ("
            "`entry` INT UNSIGNED NOT NULL PRIMARY KEY, `name` VARCHAR(100) NOT NULL) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`creature` ("
            "`guid` INT UNSIGNED NOT NULL PRIMARY KEY, `id` INT UNSIGNED NOT NULL, `map` INT UNSIGNED NOT NULL, "
            "`position_x` FLOAT NOT NULL, `position_y` FLOAT NOT NULL, `wander_distance` FLOAT NOT NULL DEFAULT 0, "
            "`MovementType` TINYINT UNSIGNED NOT NULL DEFAULT 0) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`creature_addon` ("
            "`guid` INT UNSIGNED NOT NULL PRIMARY KEY, `path_id` INT UNSIGNED NOT NULL DEFAULT 0) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`creature_template_addon` ("
            "`entry` INT UNSIGNED NOT NULL PRIMARY KEY, `path_id` INT UNSIGNED NOT NULL DEFAULT 0) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`waypoint_data` ("
            "`id` INT UNSIGNED NOT NULL, `point` INT UNSIGNED NOT NULL, `position_x` FLOAT NOT NULL, "
            "`position_y` FLOAT NOT NULL, PRIMARY KEY (`id`,`point`)) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`gameobject` ("
            "`guid` INT UNSIGNED NOT NULL PRIMARY KEY, `id` INT UNSIGNED NOT NULL, `map` INT UNSIGNED NOT NULL, "
            "`position_x` FLOAT NOT NULL, `position_y` FLOAT NOT NULL) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`creature_questitem` ("
            "`CreatureEntry` INT UNSIGNED NOT NULL, `Idx` INT UNSIGNED NOT NULL, `ItemId` INT UNSIGNED NOT NULL, "
            "PRIMARY KEY (`CreatureEntry`,`Idx`)) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`gameobject_questitem` ("
            "`GameObjectEntry` INT UNSIGNED NOT NULL, `Idx` INT UNSIGNED NOT NULL, `ItemId` INT UNSIGNED NOT NULL, "
            "PRIMARY KEY (`GameObjectEntry`,`Idx`)) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`quest_poi` ("
            "`QuestID` INT UNSIGNED NOT NULL, `id` INT UNSIGNED NOT NULL, `ObjectiveIndex` INT NOT NULL, "
            "`MapID` INT UNSIGNED NOT NULL, `WorldMapAreaId` INT UNSIGNED NOT NULL, `Floor` INT UNSIGNED NOT NULL, "
            "`Priority` INT UNSIGNED NOT NULL, `Flags` INT UNSIGNED NOT NULL, `VerifiedBuild` INT NOT NULL, "
            "PRIMARY KEY (`QuestID`,`id`)) ENGINE=InnoDB") ||
        !admin.Execute("CREATE TABLE `" + databaseName + "`.`quest_poi_points` ("
            "`QuestID` INT UNSIGNED NOT NULL, `Idx1` INT UNSIGNED NOT NULL, `Idx2` INT UNSIGNED NOT NULL, "
            "`X` INT NOT NULL, `Y` INT NOT NULL, `VerifiedBuild` INT NOT NULL, "
            "PRIMARY KEY (`QuestID`,`Idx1`,`Idx2`)) ENGINE=InnoDB") ||
        !admin.Execute("INSERT INTO `" + databaseName + "`.`quest_template` "
            "(`ID`,`LogTitle`,`RequiredPlayerKills`,`RequiredNpcOrGo1`,`RequiredNpcOrGo2`,"
            "`RequiredNpcOrGoCount1`,`RequiredNpcOrGoCount2`,`RequiredItemId1`,`RequiredItemCount1`,`ObjectiveText1`,`ObjectiveText2`) "
            "VALUES (9000000,'Quest POI integration test',2,100,-200,3,1,300,2,'Defeat Test Creature','Use Test Object')") ||
        !admin.Execute("INSERT INTO `" + databaseName + "`.`quest_template_addon` VALUES (9000000,2)") ||
        !admin.Execute("INSERT INTO `" + databaseName + "`.`creature_template` VALUES "
            "(100,0,0,'Test Creature'),(101,0,0,'Test Item Carrier')") ||
        !admin.Execute("INSERT INTO `" + databaseName + "`.`gameobject_template` VALUES (200,'Test Object')") ||
        !admin.Execute("INSERT INTO `" + databaseName + "`.`item_template` VALUES (300,'Test Quest Item')") ||
        !admin.Execute("INSERT INTO `" + databaseName + "`.`creature` VALUES "
            "(1,100,571,-1000,2500,0,2),(2,101,571,-1200,2300,12,1)") ||
        !admin.Execute("INSERT INTO `" + databaseName + "`.`creature_addon` VALUES (1,900)") ||
        !admin.Execute("INSERT INTO `" + databaseName + "`.`waypoint_data` VALUES "
            "(900,1,-1000,2500),(900,2,-980,2520)") ||
        !admin.Execute("INSERT INTO `" + databaseName + "`.`gameobject` VALUES (3,200,571,-1100,2400)") ||
        !admin.Execute("INSERT INTO `" + databaseName + "`.`creature_questitem` VALUES (101,0,300)"))
    {
        cleanup();
        return 1;
    }

    int resultCode = 0;
    {
        qpe::Database database;
        qpe::DatabaseSettings settings {
            .host = host,
            .port = static_cast<std::uint16_t>(port),
            .user = user,
            .password = password,
            .database = databaseName
        };
        std::string error;
        if (!database.Connect(settings, error))
        {
            std::cerr << "Editor database connection failed: " << error << '\n';
            resultCode = 1;
        }
        else
        {
            qpe::Quest expected {
                .id = 9000000,
                .title = "Quest POI integration test",
                .pois = {
                    qpe::Poi { .id = 0, .objectiveIndex = -1, .mapId = 0, .worldMapAreaId = 12,
                        .floor = 0, .priority = 0, .flags = 1, .verifiedBuild = 12340,
                        .points = { { 100, 200 }, { 300, 400 }, { 500, 600 } } },
                    qpe::Poi { .id = 1, .objectiveIndex = 0, .mapId = 571, .worldMapAreaId = 491,
                        .floor = 1, .priority = 2, .flags = 3, .verifiedBuild = 12340,
                        .points = { { -1000, 2500 } } }
                },
                .requirements = {
                    qpe::QuestRequirement { .kind = qpe::RequirementKind::Creature, .objectiveIndex = 0,
                        .entry = 100, .count = 3, .name = "Test Creature", .objectiveText = "Defeat Test Creature",
                        .spawns = { qpe::QuestSpawn { .kind = qpe::SpawnKind::Creature, .entry = 100,
                            .name = "Test Creature", .mapId = 571, .x = -1000.0f, .y = 2500.0f,
                            .movementType = 2, .pathId = 900,
                            .pathPoints = { { -1000.0f, 2500.0f }, { -980.0f, 2520.0f } } } } },
                    qpe::QuestRequirement { .kind = qpe::RequirementKind::GameObject, .objectiveIndex = 1,
                        .entry = 200, .count = 1, .name = "Test Object", .objectiveText = "Use Test Object",
                        .spawns = { qpe::QuestSpawn { .kind = qpe::SpawnKind::GameObject, .entry = 200,
                            .name = "Test Object", .mapId = 571, .x = -1100.0f, .y = 2400.0f } } },
                    qpe::QuestRequirement { .kind = qpe::RequirementKind::Item, .entry = 300, .count = 2,
                        .name = "Test Quest Item",
                        .spawns = { qpe::QuestSpawn { .kind = qpe::SpawnKind::Creature, .entry = 101,
                            .name = "Test Item Carrier", .mapId = 571, .x = -1200.0f, .y = 2300.0f,
                            .movementType = 1, .wanderDistance = 12.0f } } },
                    qpe::QuestRequirement { .kind = qpe::RequirementKind::PlayerKills, .count = 2,
                        .name = "Enemy players" },
                    qpe::QuestRequirement { .kind = qpe::RequirementKind::ExplorationOrEvent, .count = 1,
                        .name = "Explore an area or complete a scripted event" }
                }
            };
            qpe::Quest loaded;
            if (!database.SaveQuest(expected, error) || !database.LoadQuest(expected.id, loaded, error) || loaded != expected)
            {
                std::cerr << "Save/load round trip failed: " << error << '\n';
                resultCode = 1;
            }
            else
            {
                auto invalid = expected;
                invalid.pois[1].id = invalid.pois[0].id;
                if (database.SaveQuest(invalid, error))
                {
                    std::cerr << "Rollback test unexpectedly accepted duplicate POI IDs.\n";
                    resultCode = 1;
                }
                else
                {
                    loaded = {};
                    if (!database.LoadQuest(expected.id, loaded, error) || loaded != expected)
                    {
                        std::cerr << "Rollback did not preserve the previously committed POIs: " << error << '\n';
                        resultCode = 1;
                    }
                }
            }
            database.Disconnect();
        }
    }
    cleanup();
    if (resultCode == 0)
        std::cout << "Database integration test passed: save/load round trip and rollback verified.\n";
    return resultCode;
}
