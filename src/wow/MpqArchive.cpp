#include "wow/MpqArchive.h"

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <fstream>
#include <format>
#include <regex>
#include <system_error>

namespace qpe
{
namespace
{
#if defined(_WIN32)
void* OpenLibrary(char const* name) { return LoadLibraryA(name); }
void* FindSymbol(void* library, char const* name)
{
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
}
void CloseLibrary(void* library) { FreeLibrary(static_cast<HMODULE>(library)); }
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
void* FindSymbol(void* library, char const* name) { return dlsym(library, name); }
void CloseLibrary(void* library) { dlclose(library); }
#endif

std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

int ArchiveRank(std::filesystem::path const& path)
{
    auto const name = Lower(path.filename().string());
    if (name.find("patch") != std::string::npos)
    {
        std::smatch match;
        if (std::regex_search(name, match, std::regex(R"(-([0-9]+)\.mpq$)")))
            return 30 + std::stoi(match[1].str());
        if (std::regex_search(name, match, std::regex(R"(-([a-z])\.mpq$)")))
            return 100 + (match[1].str()[0] - 'a');
        return 30;
    }
    if (name.find("locale") != std::string::npos)
        return 20;
    if (name.find("lichking") != std::string::npos)
        return 11;
    if (name.find("expansion") != std::string::npos)
        return 10;
    if (name.find("common-2") != std::string::npos)
        return 2;
    return 0;
}
}

MpqArchiveSet::~MpqArchiveSet() { Close(); }

bool MpqArchiveSet::Open(std::filesystem::path const& clientPath, std::string const& locale, std::string& error)
{
    Close();
    error.clear();
    #if defined(_WIN32)
    constexpr char const* libraries[] { "StormLib.dll" };
    #else
    constexpr char const* libraries[] { "libstorm.so", "libStorm.so" };
    #endif
    for (auto const* name : libraries)
    {
        library_ = OpenLibrary(name);
        if (library_)
            break;
    }
    if (!library_)
    {
        error = "The StormLib shared library was not found next to the editor or in the system library path";
        return false;
    }
    auto symbol = [this](char const* name) { return FindSymbol(library_, name); };
    openArchive_ = reinterpret_cast<OpenArchive>(symbol("SFileOpenArchive"));
    closeArchive_ = reinterpret_cast<CloseArchive>(symbol("SFileCloseArchive"));
    openFile_ = reinterpret_cast<OpenFile>(symbol("SFileOpenFileEx"));
    getFileSize_ = reinterpret_cast<GetFileSize>(symbol("SFileGetFileSize"));
    readFile_ = reinterpret_cast<ReadFile>(symbol("SFileReadFile"));
    closeFile_ = reinterpret_cast<CloseFile>(symbol("SFileCloseFile"));
    if (!openArchive_ || !closeArchive_ || !openFile_ || !getFileSize_ || !readFile_ || !closeFile_)
    {
        error = "The StormLib shared library does not expose the required read API";
        Close();
        return false;
    }

    auto const data = clientPath / "Data";
    auto const localized = data / locale;
    if (!std::filesystem::is_directory(localized))
    {
        error = "The selected WoW directory does not contain Data/" + locale;
        Close();
        return false;
    }

    std::vector<std::filesystem::path> paths;
    std::error_code ec;
    for (auto const& folder : { data, localized })
        for (auto const& entry : std::filesystem::directory_iterator(folder, ec))
            if ((entry.is_regular_file() || entry.is_directory()) && Lower(entry.path().extension().string()) == ".mpq")
                paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end(), [](auto const& a, auto const& b) {
        auto const rankA = ArchiveRank(a), rankB = ArchiveRank(b);
        return rankA == rankB ? Lower(a.filename().string()) < Lower(b.filename().string()) : rankA < rankB;
    });

    for (auto const& path : paths)
    {
        if (std::filesystem::is_directory(path))
        {
            sources_.push_back(Source { .looseRoot = path, .origin = path });
            continue;
        }
        void* archive = nullptr;
        #if defined(_WIN32)
        auto const nativePath = path.wstring();
        #else
        auto const nativePath = path.string();
        #endif
        if (openArchive_(nativePath.c_str(), 0, 0x00000100u, &archive)) // read-only
            sources_.push_back(Source { .archive = archive, .origin = path });
    }
    if (sources_.empty())
    {
        error = "No readable MPQ archives were found in the selected client";
        Close();
        return false;
    }
    return true;
}

void MpqArchiveSet::Close()
{
    if (closeArchive_)
        for (auto const& source : sources_)
            if (source.archive)
                closeArchive_(source.archive);
    sources_.clear();
    if (library_)
        CloseLibrary(library_);
    library_ = nullptr;
    openArchive_ = nullptr; closeArchive_ = nullptr; openFile_ = nullptr;
    getFileSize_ = nullptr; readFile_ = nullptr; closeFile_ = nullptr;
}

std::vector<std::uint8_t> MpqArchiveSet::Read(std::string const& archivePath, std::string& error) const
{
    error.clear();
    for (auto iterator = sources_.rbegin(); iterator != sources_.rend(); ++iterator)
    {
        if (!iterator->looseRoot.empty())
        {
            auto const path = iterator->looseRoot / std::filesystem::path(archivePath);
            std::error_code ec;
            auto const size = std::filesystem::file_size(path, ec);
            if (ec)
                continue;
            if (size > 256ull * 1024 * 1024)
            {
                error = std::format("Invalid size {} from loose source {} for {}", size,
                    iterator->origin.string(), archivePath);
                return {};
            }
            std::ifstream stream(path, std::ios::binary);
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            {
                error = "Could not completely read loose override " + iterator->origin.string() + " for " + archivePath;
                return {};
            }
            return bytes;
        }
        void* file = nullptr;
        if (!openFile_(iterator->archive, archivePath.c_str(), 0, &file))
            continue;
        auto const size = getFileSize_(file, nullptr);
        if (size == 0xffffffffu || size > 256u * 1024 * 1024)
        {
            closeFile_(file);
            error = std::format("Invalid size {} from {} for {}", size, iterator->origin.string(), archivePath);
            return {};
        }
        std::vector<std::uint8_t> bytes(size);
        std::uint32_t read = 0;
        auto const ok = readFile_(file, bytes.data(), size, &read, nullptr) != 0;
        closeFile_(file);
        if (!ok || read != size)
        {
            error = "Could not completely read " + archivePath + " from " + iterator->origin.string();
            return {};
        }
        return bytes;
    }
    error = "File not found in the loaded MPQs: " + archivePath;
    return {};
}
}
