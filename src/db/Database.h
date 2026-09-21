#pragma once

#include "Models.h"
#include "Platform.h"

#include <string>

namespace qpe
{
class Database
{
public:
    Database() = default;
    ~Database();
    Database(Database const&) = delete;
    Database& operator=(Database const&) = delete;

    [[nodiscard]] bool Connect(DatabaseSettings const& settings, std::string& error);
    void Disconnect();
    [[nodiscard]] bool IsConnected() const { return connection_ != nullptr; }
    [[nodiscard]] bool LoadQuestList(std::vector<QuestSummary>& quests, std::string& error);
    [[nodiscard]] bool LoadQuest(std::uint32_t questId, Quest& quest, std::string& error);
    [[nodiscard]] bool SaveQuest(Quest const& quest, std::string& error);

private:
    [[nodiscard]] bool Execute(std::string const& sql, std::string& error);
    [[nodiscard]] std::string LastError() const;

    void* library_ = nullptr;
    void* connection_ = nullptr;
    using Init = void*(QPE_CALL*)(void*);
    using RealConnect = void*(QPE_CALL*)(void*, char const*, char const*, char const*, char const*, unsigned int, char const*, unsigned long);
    using Close = void(QPE_CALL*)(void*);
    using Query = int(QPE_CALL*)(void*, char const*);
    using StoreResult = void*(QPE_CALL*)(void*);
    using FetchRow = char**(QPE_CALL*)(void*);
    using FreeResult = void(QPE_CALL*)(void*);
    using Error = char const*(QPE_CALL*)(void*);
    using SetCharset = int(QPE_CALL*)(void*, char const*);
    using AutoCommit = int(QPE_CALL*)(void*, unsigned char);
    using Commit = int(QPE_CALL*)(void*);
    using Rollback = int(QPE_CALL*)(void*);
    Init init_ = nullptr;
    RealConnect realConnect_ = nullptr;
    Close close_ = nullptr;
    Query query_ = nullptr;
    StoreResult storeResult_ = nullptr;
    FetchRow fetchRow_ = nullptr;
    FreeResult freeResult_ = nullptr;
    Error error_ = nullptr;
    SetCharset setCharset_ = nullptr;
    AutoCommit autoCommit_ = nullptr;
    Commit commit_ = nullptr;
    Rollback rollback_ = nullptr;
};
}
