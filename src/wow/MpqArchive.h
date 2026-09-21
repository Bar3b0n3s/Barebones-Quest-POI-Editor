#pragma once

#include "Platform.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace qpe
{
class MpqArchiveSet
{
public:
    MpqArchiveSet() = default;
    ~MpqArchiveSet();
    MpqArchiveSet(MpqArchiveSet const&) = delete;
    MpqArchiveSet& operator=(MpqArchiveSet const&) = delete;

    [[nodiscard]] bool Open(std::filesystem::path const& clientPath, std::string const& locale, std::string& error);
    void Close();
    [[nodiscard]] std::vector<std::uint8_t> Read(std::string const& archivePath, std::string& error) const;
    [[nodiscard]] std::size_t ArchiveCount() const { return sources_.size(); }

private:
    void* library_ = nullptr;
    struct Source
    {
        void* archive = nullptr;
        std::filesystem::path looseRoot;
        std::filesystem::path origin;
    };
    std::vector<Source> sources_;
#if defined(_WIN32)
    // The bundled StormLib Visual Studio project is built with CharacterSet=Unicode.
    using ArchivePathChar = wchar_t;
#else
    using ArchivePathChar = char;
#endif
    using OpenArchive = bool(QPE_CALL*)(ArchivePathChar const*, std::uint32_t, std::uint32_t, void**);
    using CloseArchive = bool(QPE_CALL*)(void*);
    using OpenFile = bool(QPE_CALL*)(void*, char const*, std::uint32_t, void**);
    using GetFileSize = std::uint32_t(QPE_CALL*)(void*, std::uint32_t*);
    using ReadFile = bool(QPE_CALL*)(void*, void*, std::uint32_t, std::uint32_t*, void*);
    using CloseFile = bool(QPE_CALL*)(void*);
    OpenArchive openArchive_ = nullptr;
    CloseArchive closeArchive_ = nullptr;
    OpenFile openFile_ = nullptr;
    GetFileSize getFileSize_ = nullptr;
    ReadFile readFile_ = nullptr;
    CloseFile closeFile_ = nullptr;
};
}
