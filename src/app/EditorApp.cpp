#include "app/EditorApp.h"

#include "wow/BlpDecoder.h"

#if defined(_WIN32)
#include <Windows.h>
#include <ShObjIdl.h>
#include <ShlObj.h>
#endif
#include <GL/gl.h>
#include <imgui.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <format>
#include <sstream>
#include <string_view>

namespace qpe
{
namespace
{
template <std::size_t N>
void Copy(std::array<char, N>& destination, std::string const& source)
{
    std::memset(destination.data(), 0, destination.size());
    std::memcpy(destination.data(), source.data(), std::min(source.size(), destination.size() - 1));
}

std::filesystem::path SettingsPath()
{
#if defined(_WIN32)
    wchar_t* appData = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
    {
        path = std::filesystem::path(appData) / "BarebonesQuestPoiEditor" / "settings.ini";
        CoTaskMemFree(appData);
    }
    return path;
#else
    if (auto const* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "barebones-quest-poi-editor" / "settings.ini";
    if (auto const* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config" / "barebones-quest-poi-editor" / "settings.ini";
    return {};
#endif
}

std::optional<std::filesystem::path> PickFolder()
{
#if defined(_WIN32)
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return std::nullopt;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Choose the World of Warcraft 3.3.5a folder");
    std::optional<std::filesystem::path> result;
    if (SUCCEEDED(dialog->Show(GetActiveWindow())))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            wchar_t* value = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value)))
            {
                result = value;
                CoTaskMemFree(value);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
#else
    auto runPicker = [](char const* command) -> std::optional<std::filesystem::path> {
        auto* pipe = popen(command, "r");
        if (!pipe)
            return std::nullopt;
        std::string output;
        std::array<char, 1024> buffer {};
        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
            output += buffer.data();
        auto const status = pclose(pipe);
        while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
            output.pop_back();
        if (status != 0 || output.empty())
            return std::nullopt;
        return std::filesystem::path(output);
    };
    if (std::system("command -v zenity >/dev/null 2>&1") == 0)
        return runPicker("zenity --file-selection --directory --title='Choose the World of Warcraft 3.3.5a folder' 2>/dev/null");
    if (std::system("command -v kdialog >/dev/null 2>&1") == 0)
        return runPicker("kdialog --getexistingdirectory \"$HOME\" --title 'Choose the World of Warcraft 3.3.5a folder' 2>/dev/null");
    return std::nullopt;
#endif
}

std::string Utf8(std::filesystem::path const& path)
{
#if defined(_WIN32)
    auto const wide = path.wstring();
    auto count = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (count <= 1)
        return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), count, nullptr, nullptr);
    result.pop_back();
    return result;
#else
    return path.string();
#endif
}

std::filesystem::path PathFromUtf8(std::string const& text)
{
#if defined(_WIN32)
    if (text.empty())
        return {};
    auto const count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (count <= 1)
        return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), count);
    result.pop_back();
    return result;
#else
    return std::filesystem::path(text);
#endif
}

ImVec2 CanvasPoint(ImVec2 origin, ImVec2 size, std::pair<float, float> normalized)
{
    return { origin.x + normalized.first * size.x, origin.y + normalized.second * size.y };
}

float DistanceSquared(ImVec2 a, ImVec2 b)
{
    auto const x = a.x - b.x, y = a.y - b.y;
    return x * x + y * y;
}

bool FuzzyTokenMatches(std::string const& text, std::string_view token)
{
    auto position = text.begin();
    for (unsigned char wanted : token)
    {
        position = std::find_if(position, text.end(), [wanted](unsigned char candidate) {
            return std::tolower(candidate) == std::tolower(wanted);
        });
        if (position == text.end())
            return false;
        ++position;
    }
    return true;
}

bool FuzzyMatches(std::string const& text, std::string const& filter)
{
    std::size_t position = 0;
    while (position < filter.size())
    {
        while (position < filter.size() && std::isspace(static_cast<unsigned char>(filter[position])))
            ++position;
        auto const start = position;
        while (position < filter.size() && !std::isspace(static_cast<unsigned char>(filter[position])))
            ++position;
        if (start != position && !FuzzyTokenMatches(text, std::string_view(filter).substr(start, position - start)))
            return false;
    }
    return true;
}
}

EditorApp::EditorApp()
{
    LoadSettings();
    if (settings_.clientPath.empty())
        settingsOpen_ = true;
    else
        ApplySettings();
}

EditorApp::~EditorApp()
{
    ReleaseMapTexture();
}

void EditorApp::SetStatus(std::string message, bool error)
{
    status_ = std::move(message);
    statusError_ = error;
}

void EditorApp::Render()
{
    ImGuiViewport const* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Quest POI Editor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    RenderToolbar();
    ImGui::Separator();
    auto const available = ImGui::GetContentRegionAvail();
    auto const workspaceHeight = std::max(1.0f, available.y - 28.0f);
    if (ImGui::BeginTable("Workspace", 3,
        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp,
        { 0.0f, workspaceHeight }))
    {
        ImGui::TableSetupColumn("Quest browser", ImGuiTableColumnFlags_WidthFixed, 310.0f);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Loaded quest", ImGuiTableColumnFlags_WidthFixed, 340.0f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("Quest browser panel", { 0.0f, workspaceHeight }, ImGuiChildFlags_Borders);
        RenderQuestBrowser();
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("Map canvas", { 0.0f, workspaceHeight }, ImGuiChildFlags_Borders);
        RenderCanvas();
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(2);
        ImGui::BeginChild("Loaded quest panel", { 0.0f, workspaceHeight }, ImGuiChildFlags_Borders);
        RenderPoiPanel();
        ImGui::EndChild();
        ImGui::EndTable();
    }
    if (ImGui::BeginTable("Status bar", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Connection", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Updates", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (database_.IsConnected())
            ImGui::TextColored({ 0.35f, 0.9f, 0.5f, 1.0f }, "Database Connected");
        else
            ImGui::TextDisabled("Database Disconnected");

        ImGui::TableSetColumnIndex(1);
        auto const color = statusError_ ? ImVec4(1.0f, 0.38f, 0.35f, 1.0f) : ImVec4(0.55f, 0.8f, 0.9f, 1.0f);
        auto const updateWidth = ImGui::CalcTextSize(status_.c_str()).x;
        auto const updateSpace = ImGui::GetContentRegionAvail().x;
        if (updateWidth < updateSpace)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + updateSpace - updateWidth);
        ImGui::TextColored(color, "%s", status_.c_str());
        ImGui::EndTable();
    }
    ImGui::End();
    RenderSettings();
    RenderInfo();
    RenderSaveConfirmation();
    RenderUnsavedChanges();
}

void EditorApp::RenderToolbar()
{
    if (ImGui::Button("Settings"))
        settingsOpen_ = true;
    ImGui::SameLine();
    if (ImGui::Button("Info"))
        infoOpen_ = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(!database_.IsConnected() || !IsDirty());
    if (ImGui::Button("Save to database"))
        saveConfirmationOpen_ = true;
    ImGui::EndDisabled();

    auto const& io = ImGui::GetIO();
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
    {
        if (io.KeyShift)
            Redo();
        else
            Undo();
    }
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
        Redo();
    if (!io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) &&
        database_.IsConnected() && IsDirty())
        saveConfirmationOpen_ = true;
}

void EditorApp::RenderPoiPanel()
{
    ImGui::SeparatorText("Loaded quest");
    if (quest_.id == 0)
    {
        ImGui::TextWrapped("Select a quest from the browser to view and edit its POIs.");
        return;
    }
    ImGui::Text("Quest %u", quest_.id);
    if (IsDirty())
    {
        ImGui::SameLine();
        ImGui::TextColored({ 1.0f, 0.72f, 0.25f, 1.0f }, "Unsaved changes");
    }
    ImGui::TextWrapped("%s", quest_.title.empty() ? "(title unavailable)" : quest_.title.c_str());
    ImGui::SeparatorText("POIs");
    for (std::size_t index = 0; index < quest_.pois.size(); ++index)
    {
        auto const& poi = quest_.pois[index];
        auto label = std::format("POI {}  |  {} point{}", poi.id, poi.points.size(), poi.points.size() == 1 ? "" : "s");
        if (ImGui::Selectable(label.c_str(), selectedPoi_ == static_cast<int>(index)))
            SelectPoi(static_cast<int>(index));
    }
    if (ImGui::Button("Add POI"))
    {
        RecordUndo(quest_);
        Poi poi;
        poi.id = static_cast<std::uint32_t>(quest_.pois.size());
        if (auto const* area = CurrentArea())
        {
            poi.mapId = area->mapId;
            poi.worldMapAreaId = area->id;
        }
        quest_.pois.push_back(poi);
        SelectPoi(static_cast<int>(quest_.pois.size() - 1));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedPoi_ < 0);
    if (ImGui::Button("Delete POI"))
    {
        RecordUndo(quest_);
        quest_.pois.erase(quest_.pois.begin() + selectedPoi_);
        NormalizePoiIds(quest_);
        SelectPoi(quest_.pois.empty() ? -1 : std::min(selectedPoi_, static_cast<int>(quest_.pois.size() - 1)));
    }
    ImGui::EndDisabled();
    if (selectedPoi_ < 0 || selectedPoi_ >= static_cast<int>(quest_.pois.size()))
        return;

    auto& poi = quest_.pois[selectedPoi_];
    ImGui::SeparatorText("Selected POI");
    auto trackedScalar = [this](char const* label, ImGuiDataType type, void* value) {
        auto before = quest_;
        if (ImGui::InputScalar(label, type, value))
            RecordUndo(before);
    };
    trackedScalar("Objective index", ImGuiDataType_S32, &poi.objectiveIndex);
    trackedScalar("Map ID", ImGuiDataType_U32, &poi.mapId);
    auto previousArea = poi.worldMapAreaId;
    auto previousFloor = poi.floor;
    trackedScalar("World map area", ImGuiDataType_U32, &poi.worldMapAreaId);
    auto selectedArea = std::find_if(areas_.begin(), areas_.end(), [&poi](auto const& area) { return area.id == poi.worldMapAreaId; });
    auto const preview = selectedArea == areas_.end() ? "Choose map..." : selectedArea->textureName.c_str();
    if (ImGui::BeginCombo("Map texture", preview))
    {
        for (auto const& area : areas_)
        {
            auto label = std::format("{}  ({})", area.textureName, area.id);
            if (ImGui::Selectable(label.c_str(), area.id == poi.worldMapAreaId) && area.id != poi.worldMapAreaId)
            {
                RecordUndo(quest_);
                poi.worldMapAreaId = area.id;
                poi.mapId = area.mapId;
            }
        }
        ImGui::EndCombo();
    }
    trackedScalar("Floor", ImGuiDataType_U32, &poi.floor);
    trackedScalar("Priority", ImGuiDataType_U32, &poi.priority);
    trackedScalar("Flags", ImGuiDataType_U32, &poi.flags);
    trackedScalar("Verified build", ImGuiDataType_S32, &poi.verifiedBuild);
    if (previousArea != poi.worldMapAreaId || previousFloor != poi.floor)
        LoadMap(poi.worldMapAreaId, poi.floor);

    ImGui::SeparatorText("Points");
    ImGui::TextWrapped("Left-click the map to add. Drag a handle to move it. Right-click a handle to remove it.");
    for (std::size_t index = 0; index < poi.points.size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable(std::format("{}: {}, {}", index, poi.points[index].x, poi.points[index].y).c_str(),
                              selectedPoint_ == static_cast<int>(index)))
            selectedPoint_ = static_cast<int>(index);
        ImGui::PopID();
    }
}

void EditorApp::RenderQuestBrowser()
{
    ImGui::SeparatorText("Quest browser");
    if (!database_.IsConnected())
    {
        ImGui::TextWrapped("Connect to the world database to browse quests. Only quest_poi and quest_poi_points are edited.");
        return;
    }

    if (ImGui::InputTextWithHint("##quest-name-filter", "Fuzzy-search quest names...", questNameFilter_.data(), questNameFilter_.size()))
        RebuildQuestFilter();
    if (ImGui::InputTextWithHint("##quest-id-filter", "Fuzzy-search quest IDs...", questIdFilter_.data(), questIdFilter_.size(),
        ImGuiInputTextFlags_CharsDecimal))
        RebuildQuestFilter();
    if (ImGui::Button(questIdAscending_ ? "ID ascending" : "ID descending"))
    {
        questIdAscending_ = !questIdAscending_;
        RebuildQuestFilter();
    }
    if (ImGui::Button("Refresh quests"))
        RefreshQuestList();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu of %zu", filteredQuestIndices_.size(), questList_.size());

    ImGui::BeginChild("Quest results", { 0.0f, 0.0f }, ImGuiChildFlags_Borders);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filteredQuestIndices_.size()), ImGui::GetTextLineHeightWithSpacing());
    while (clipper.Step())
    {
        for (int visibleIndex = clipper.DisplayStart; visibleIndex < clipper.DisplayEnd; ++visibleIndex)
        {
            auto const& summary = questList_[filteredQuestIndices_[visibleIndex]];
            auto label = std::format("{}  {}", summary.id, summary.title.empty() ? "(untitled)" : summary.title);
            if (ImGui::Selectable(label.c_str(), quest_.id == summary.id))
                RequestLoadQuest(summary.id);
        }
    }
    ImGui::EndChild();
}

bool EditorApp::RefreshQuestList(bool updateStatus)
{
    std::string error;
    if (!database_.LoadQuestList(questList_, error))
    {
        questList_.clear();
        filteredQuestIndices_.clear();
        SetStatus("Could not load the quest list: " + error, true);
        return false;
    }
    RebuildQuestFilter();
    if (updateStatus)
        SetStatus(std::format("Loaded {} quests from the world database.", questList_.size()));
    return true;
}

void EditorApp::RebuildQuestFilter()
{
    filteredQuestIndices_.clear();
    auto const nameFilter = std::string(questNameFilter_.data());
    auto const idFilter = std::string(questIdFilter_.data());
    for (std::size_t index = 0; index < questList_.size(); ++index)
        if (FuzzyMatches(questList_[index].title, nameFilter) &&
            FuzzyMatches(std::to_string(questList_[index].id), idFilter))
            filteredQuestIndices_.push_back(index);
    if (!questIdAscending_)
        std::reverse(filteredQuestIndices_.begin(), filteredQuestIndices_.end());
}

WorldMapArea const* EditorApp::CurrentArea() const
{
    auto const id = selectedPoi_ >= 0 && selectedPoi_ < static_cast<int>(quest_.pois.size())
        ? quest_.pois[selectedPoi_].worldMapAreaId : loadedArea_;
    auto found = std::find_if(areas_.begin(), areas_.end(), [id](auto const& area) { return area.id == id; });
    return found == areas_.end() ? nullptr : &*found;
}

void EditorApp::RenderCanvas()
{
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        pointDragActive_ = false;
    auto const* area = CurrentArea();
    if (!mapTexture_ || !area)
    {
        ImGui::TextWrapped("No map is loaded. Select a POI with a valid WorldMapAreaId and configure a readable 3.3.5a client.");
        return;
    }
    if (ImGui::Button("Reset view"))
    {
        mapZoom_ = 1.0f;
        mapPanX_ = 0.0f;
        mapPanY_ = 0.0f;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Zoom %.0f%%  |  Wheel: zoom  |  Middle-drag: pan", mapZoom_ * 100.0f);

    auto available = ImGui::GetContentRegionAvail();
    if (available.x < 1.0f || available.y < 1.0f)
        return;
    auto const canvasOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("poi-map", available,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    constexpr float aspect = 1002.0f / 668.0f;
    ImVec2 fittedSize { available.x, available.x / aspect };
    if (fittedSize.y > available.y)
        fittedSize = { available.y * aspect, available.y };

    auto clampPan = [&](ImVec2 const& mapSize) {
        auto const limitX = std::max(0.0f, (mapSize.x - available.x) * 0.5f);
        auto const limitY = std::max(0.0f, (mapSize.y - available.y) * 0.5f);
        mapPanX_ = std::clamp(mapPanX_, -limitX, limitX);
        mapPanY_ = std::clamp(mapPanY_, -limitY, limitY);
    };

    auto size = ImVec2(fittedSize.x * mapZoom_, fittedSize.y * mapZoom_);
    auto origin = ImVec2(canvasOrigin.x + (available.x - size.x) * 0.5f + mapPanX_,
                         canvasOrigin.y + (available.y - size.y) * 0.5f + mapPanY_);
    auto const itemHovered = ImGui::IsItemHovered();
    auto const mouse = ImGui::GetIO().MousePos;
    if (itemHovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        auto const normalizedX = (mouse.x - origin.x) / size.x;
        auto const normalizedY = (mouse.y - origin.y) / size.y;
        mapZoom_ = std::clamp(mapZoom_ * std::pow(1.2f, ImGui::GetIO().MouseWheel), 1.0f, 6.0f);
        size = { fittedSize.x * mapZoom_, fittedSize.y * mapZoom_ };
        auto const centeredX = canvasOrigin.x + (available.x - size.x) * 0.5f;
        auto const centeredY = canvasOrigin.y + (available.y - size.y) * 0.5f;
        mapPanX_ = mouse.x - normalizedX * size.x - centeredX;
        mapPanY_ = mouse.y - normalizedY * size.y - centeredY;
        clampPan(size);
        origin = { centeredX + mapPanX_, centeredY + mapPanY_ };
    }
    if (itemHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        mapPanX_ += ImGui::GetIO().MouseDelta.x;
        mapPanY_ += ImGui::GetIO().MouseDelta.y;
        clampPan(size);
        origin = { canvasOrigin.x + (available.x - size.x) * 0.5f + mapPanX_,
                   canvasOrigin.y + (available.y - size.y) * 0.5f + mapPanY_ };
    }

    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(canvasOrigin, { canvasOrigin.x + available.x, canvasOrigin.y + available.y }, true);
    draw->AddImage(static_cast<ImTextureID>(mapTexture_), origin, { origin.x + size.x, origin.y + size.y });
    if (selectedPoi_ < 0 || selectedPoi_ >= static_cast<int>(quest_.pois.size()))
    {
        draw->PopClipRect();
        return;
    }
    auto& poi = quest_.pois[selectedPoi_];
    std::vector<ImVec2> screenPoints;
    screenPoints.reserve(poi.points.size());
    for (auto const& point : poi.points)
        screenPoints.push_back(CanvasPoint(origin, size, area->WorldToNormalized(point)));

    auto const fill = IM_COL32(72, 190, 235, 75);
    if (screenPoints.size() == 1)
        draw->AddCircleFilled(screenPoints[0], std::max(8.0f, size.x * 0.0125f), fill, 32);
    else if (screenPoints.size() >= 3)
        draw->AddConcavePolyFilled(screenPoints.data(), static_cast<int>(screenPoints.size()), fill);

    for (std::size_t index = 0; index < screenPoints.size(); ++index)
    {
        draw->AddCircleFilled(screenPoints[index], selectedPoint_ == static_cast<int>(index) ? 3.5f : 2.5f,
            selectedPoint_ == static_cast<int>(index) ? IM_COL32(255, 220, 90, 255) : IM_COL32(225, 250, 255, 255));
        draw->AddCircle(screenPoints[index], 4.5f, IM_COL32(20, 70, 90, 240), 16, 1.0f);
    }
    draw->PopClipRect();

    if (!itemHovered)
        return;
    int hovered = -1;
    for (std::size_t index = 0; index < screenPoints.size(); ++index)
        if (DistanceSquared(mouse, screenPoints[index]) <= 144.0f)
            hovered = static_cast<int>(index);
    if (hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        RecordUndo(quest_);
        poi.points.erase(poi.points.begin() + hovered);
        selectedPoint_ = -1;
        return;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (hovered >= 0)
        {
            selectedPoint_ = hovered;
            pointDragActive_ = false;
        }
        else if (mouse.x >= origin.x && mouse.x <= origin.x + size.x &&
                 mouse.y >= origin.y && mouse.y <= origin.y + size.y)
        {
            RecordUndo(quest_);
            auto const nx = std::clamp((mouse.x - origin.x) / size.x, 0.0f, 1.0f);
            auto const ny = std::clamp((mouse.y - origin.y) / size.y, 0.0f, 1.0f);
            poi.points.push_back(area->NormalizedToWorld(nx, ny));
            selectedPoint_ = static_cast<int>(poi.points.size() - 1);
            pointDragActive_ = true;
        }
    }
    if (selectedPoint_ >= 0 && selectedPoint_ < static_cast<int>(poi.points.size()) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        if (!pointDragActive_)
        {
            RecordUndo(quest_);
            pointDragActive_ = true;
        }
        auto const nx = std::clamp((mouse.x - origin.x) / size.x, 0.0f, 1.0f);
        auto const ny = std::clamp((mouse.y - origin.y) / size.y, 0.0f, 1.0f);
        poi.points[selectedPoint_] = area->NormalizedToWorld(nx, ny);
    }
    if (hovered >= 0)
    {
        auto const& point = poi.points[hovered];
        ImGui::BeginTooltip(); ImGui::Text("Point %d: %d, %d", hovered, point.x, point.y); ImGui::EndTooltip();
    }
}

void EditorApp::RenderSettings()
{
    if (!settingsOpen_)
        return;
    ImGui::OpenPopup("Settings");
    ImGui::SetNextWindowSize({ 650, 0 }, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Settings", &settingsOpen_, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SeparatorText("WoW 3.3.5a client");
        ImGui::SetNextItemWidth(500); ImGui::InputText("Client folder", clientPath_.data(), clientPath_.size());
        ImGui::SameLine();
        if (ImGui::Button("Browse..."))
            if (auto path = PickFolder())
                Copy(clientPath_, Utf8(*path));
        ImGui::SetNextItemWidth(100); ImGui::InputText("Locale", locale_.data(), locale_.size());
        ImGui::SeparatorText("World database");
        ImGui::SetNextItemWidth(220); ImGui::InputText("Host", dbHost_.data(), dbHost_.size());
        ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::InputInt("Port", &dbPort_);
        ImGui::SetNextItemWidth(220); ImGui::InputText("User", dbUser_.data(), dbUser_.size());
        ImGui::SetNextItemWidth(220); ImGui::InputText("Password", dbPassword_.data(), dbPassword_.size(), ImGuiInputTextFlags_Password);
        ImGui::SetNextItemWidth(220); ImGui::InputText("Database", dbName_.data(), dbName_.size());
        ImGui::TextDisabled("The password is kept in memory only and is never written to settings.ini.");
        if (database_.IsConnected())
            ImGui::TextColored({ 0.35f, 0.9f, 0.5f, 1.0f }, "Connected to the world database");
        else
            ImGui::TextDisabled("Not connected to the world database");

        auto const connectionButton = database_.IsConnected() ? "Disconnect" : "Connect";
        if (ImGui::Button(connectionButton, { 120, 0 }))
        {
            if (database_.IsConnected())
            {
                settingsOpen_ = false;
                ImGui::CloseCurrentPopup();
                RequestDisconnect();
            }
            else
            {
                settings_.clientPath = clientPath_.data(); settings_.locale = locale_.data();
                settings_.database.host = dbHost_.data(); settings_.database.port = static_cast<std::uint16_t>(std::clamp(dbPort_, 1, 65535));
                settings_.database.user = dbUser_.data(); settings_.database.password = dbPassword_.data(); settings_.database.database = dbName_.data();
                SaveSettings(); ApplySettings(true); settingsOpen_ = false; ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 120, 0 }))
        {
            settingsOpen_ = false; ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::RenderInfo()
{
    if (!infoOpen_)
        return;
    ImGui::OpenPopup("How to use the Quest POI Editor");
    ImGui::SetNextWindowSize({ 650.0f, 0.0f }, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("How to use the Quest POI Editor", &infoOpen_, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SeparatorText("Get started");
        ImGui::BulletText("Open Settings, choose the WoW 3.3.5a client folder, and enter the world database details.");
        ImGui::BulletText("Connect loads the client maps and connects to the database. Disconnect is also available in Settings.");

        ImGui::SeparatorText("Find and load a quest");
        ImGui::BulletText("Use the left-side name and ID fields to fuzzy-search the quest list.");
        ImGui::BulletText("Click a quest in the list. Its POIs and map load automatically.");
        ImGui::BulletText("Use ID ascending/descending to change the list order.");

        ImGui::SeparatorText("Edit POIs and points");
        ImGui::BulletText("Use the right-side panel to add, delete, select, and configure POIs.");
        ImGui::BulletText("Left-click the map to add a point. Drag a point to move it.");
        ImGui::BulletText("Right-click a point to remove it. Three or more points shade the enclosed area.");
        ImGui::BulletText("Use Ctrl+Z to undo and Ctrl+Y or Ctrl+Shift+Z to redo POI edits.");
        ImGui::BulletText("Unsaved changes are marked and protected when switching quests, disconnecting, or exiting.");

        ImGui::SeparatorText("Navigate and save");
        ImGui::BulletText("Move the mouse wheel over the map to zoom toward the pointer.");
        ImGui::BulletText("Hold the middle mouse button and drag to pan. Reset view returns to the fitted map.");
        ImGui::BulletText("Choose Save to database or press Ctrl+S when the POI changes are ready.");
        ImGui::BulletText("Review the save confirmation and keep a current world-database backup.");

        if (ImGui::Button("Close", { 120.0f, 0.0f }))
        {
            infoOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::RenderSaveConfirmation()
{
    if (!saveConfirmationOpen_)
        return;
    ImGui::OpenPopup("Confirm database save");
    ImGui::SetNextWindowSize({ 540.0f, 0.0f }, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Confirm database save", &saveConfirmationOpen_, ImGuiWindowFlags_AlwaysAutoResize))
    {
        std::size_t pointCount = 0;
        for (auto const& poi : quest_.pois)
            pointCount += poi.points.size();
        ImGui::TextWrapped("Save quest %u to database '%s'?", quest_.id, settings_.database.database.c_str());
        ImGui::TextWrapped("This replaces every existing quest_poi and quest_poi_points row for this quest with %zu POI record(s) and %zu point(s).",
            quest_.pois.size(), pointCount);
        ImGui::Spacing();
        ImGui::TextColored({ 1.0f, 0.72f, 0.25f, 1.0f }, "Back up the world database before editing production data.");
        ImGui::TextWrapped("The write uses one transaction and rolls back if any statement fails.");
        ImGui::Spacing();
        if (ImGui::Button("Save changes", { 140.0f, 0.0f }))
        {
            if (SaveQuest())
            {
                saveConfirmationOpen_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 120.0f, 0.0f }))
        {
            saveConfirmationOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::RenderUnsavedChanges()
{
    if (!unsavedPromptOpen_)
        return;
    ImGui::OpenPopup("Unsaved quest changes");
    ImGui::SetNextWindowSize({ 560.0f, 0.0f }, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Unsaved quest changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        char const* action = "continue";
        if (pendingAction_ == PendingAction::LoadQuest) action = "load another quest";
        else if (pendingAction_ == PendingAction::Disconnect) action = "disconnect";
        else if (pendingAction_ == PendingAction::Exit) action = "exit";
        ImGui::TextWrapped("Quest %u has unsaved POI changes. Save them before you %s?", quest_.id, action);
        ImGui::Spacing();
        ImGui::BeginDisabled(!database_.IsConnected());
        if (ImGui::Button("Save and continue", { 160.0f, 0.0f }))
        {
            if (SaveQuest())
            {
                unsavedPromptOpen_ = false;
                ImGui::CloseCurrentPopup();
                PerformPendingAction();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Discard changes", { 150.0f, 0.0f }))
        {
            unsavedPromptOpen_ = false;
            ImGui::CloseCurrentPopup();
            PerformPendingAction();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100.0f, 0.0f }))
        {
            pendingAction_ = PendingAction::None;
            pendingQuestId_ = 0;
            unsavedPromptOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::LoadSettings()
{
    auto const path = SettingsPath();
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line))
    {
        auto const equals = line.find('=');
        if (equals == std::string::npos) continue;
        auto const key = line.substr(0, equals), value = line.substr(equals + 1);
        if (key == "client") settings_.clientPath = value;
        else if (key == "locale") settings_.locale = value;
        else if (key == "host") settings_.database.host = value;
        else if (key == "port") { try { settings_.database.port = static_cast<std::uint16_t>(std::stoi(value)); } catch (...) {} }
        else if (key == "user") settings_.database.user = value;
        else if (key == "database") settings_.database.database = value;
    }
    Copy(clientPath_, settings_.clientPath); Copy(locale_, settings_.locale); Copy(dbHost_, settings_.database.host);
    Copy(dbUser_, settings_.database.user); Copy(dbName_, settings_.database.database); dbPort_ = settings_.database.port;
    settingsLoaded_ = true;
}

void EditorApp::SaveSettings()
{
    auto const path = SettingsPath();
    if (path.empty()) return;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::trunc);
    file << "client=" << settings_.clientPath << '\n' << "locale=" << settings_.locale << '\n'
         << "host=" << settings_.database.host << '\n' << "port=" << settings_.database.port << '\n'
         << "user=" << settings_.database.user << '\n' << "database=" << settings_.database.database << '\n';
}

void EditorApp::ApplySettings(bool connectDatabase)
{
    database_.Disconnect();
    std::string mapError;
    bool mapLoaded = archives_.Open(PathFromUtf8(settings_.clientPath), settings_.locale, mapError);
    if (mapLoaded)
    {
        auto dbc = archives_.Read("DBFilesClient\\WorldMapArea.dbc", mapError);
        areas_ = ParseWorldMapAreas(dbc);
        mapLoaded = !areas_.empty();
        if (!mapLoaded && mapError.empty())
            mapError = "WorldMapArea.dbc is missing or invalid";
    }

    bool databaseConnected = false;
    bool questListLoaded = false;
    std::string databaseError;
    if (connectDatabase)
    {
        databaseConnected = database_.Connect(settings_.database, databaseError);
        if (databaseConnected)
        {
            questListLoaded = database_.LoadQuestList(questList_, databaseError);
            if (questListLoaded)
                RebuildQuestFilter();
            else
            {
                questList_.clear();
                filteredQuestIndices_.clear();
            }
        }
    }

    if (mapLoaded && (!connectDatabase || (databaseConnected && questListLoaded)))
    {
        if (databaseConnected)
            SetStatus(std::format("Loaded {} map definitions, connected to database '{}', and loaded {} quests.",
                areas_.size(), settings_.database.database, questList_.size()));
        else
            SetStatus(std::format("Loaded {} map definitions from {} MPQ/override sources.", areas_.size(), archives_.ArchiveCount()));
        return;
    }

    if (mapLoaded && databaseConnected)
        SetStatus("Database connected, but the quest list failed to load: " + databaseError, true);
    else if (mapLoaded)
        SetStatus("Map definitions loaded, but the database connection failed: " + databaseError, true);
    else if (databaseConnected)
        SetStatus("Database connected, but the client data failed to load: " + mapError, true);
    else if (connectDatabase)
        SetStatus("Client data failed: " + mapError + " | Database connection failed: " + databaseError, true);
    else
        SetStatus(mapError, true);
}

bool EditorApp::IsDirty() const
{
    return quest_.id != 0 && (!cleanQuest_ || quest_ != *cleanQuest_);
}

void EditorApp::RequestLoadQuest(std::uint32_t questId)
{
    if (questId == quest_.id)
        return;
    if (IsDirty())
    {
        pendingAction_ = PendingAction::LoadQuest;
        pendingQuestId_ = questId;
        unsavedPromptOpen_ = true;
        return;
    }
    LoadQuest(questId);
}

void EditorApp::RequestDisconnect()
{
    if (IsDirty())
    {
        pendingAction_ = PendingAction::Disconnect;
        unsavedPromptOpen_ = true;
        return;
    }
    DisconnectNow();
}

void EditorApp::RequestExit()
{
    if (exitReady_ || pendingAction_ != PendingAction::None)
        return;
    settingsOpen_ = false;
    infoOpen_ = false;
    saveConfirmationOpen_ = false;
    if (IsDirty())
    {
        pendingAction_ = PendingAction::Exit;
        unsavedPromptOpen_ = true;
        return;
    }
    exitReady_ = true;
}

void EditorApp::DisconnectNow()
{
    database_.Disconnect();
    questList_.clear();
    filteredQuestIndices_.clear();
    quest_ = {};
    cleanQuest_.reset();
    undoHistory_.clear();
    redoHistory_.clear();
    selectedPoi_ = -1;
    selectedPoint_ = -1;
    pointDragActive_ = false;
    loadedArea_ = 0;
    loadedFloor_ = 0;
    mapZoom_ = 1.0f;
    mapPanX_ = 0.0f;
    mapPanY_ = 0.0f;
    ReleaseMapTexture();
    SetStatus("Disconnected from the world database.");
}

void EditorApp::PerformPendingAction()
{
    auto const action = pendingAction_;
    auto const questId = pendingQuestId_;
    pendingAction_ = PendingAction::None;
    pendingQuestId_ = 0;
    switch (action)
    {
        case PendingAction::LoadQuest:
            if (IsDirty() && cleanQuest_)
                RestoreQuest(*cleanQuest_);
            LoadQuest(questId);
            break;
        case PendingAction::Disconnect: DisconnectNow(); break;
        case PendingAction::Exit: exitReady_ = true; break;
        case PendingAction::None: break;
    }
}

void EditorApp::LoadQuest(std::uint32_t questId)
{
    if (!database_.IsConnected())
    {
        SetStatus("Connect to the world database first.", true); return;
    }
    std::string error;
    Quest loaded;
    if (!database_.LoadQuest(questId, loaded, error))
    {
        SetStatus(error, true); return;
    }
    quest_ = std::move(loaded);
    cleanQuest_ = quest_;
    undoHistory_.clear();
    redoHistory_.clear();
    pointDragActive_ = false;
    SelectPoi(quest_.pois.empty() ? -1 : 0);
    SetStatus(std::format("Loaded quest {} with {} POI record(s).", quest_.id, quest_.pois.size()));
}

bool EditorApp::SaveQuest()
{
    NormalizePoiIds(quest_);
    std::string error;
    if (database_.SaveQuest(quest_, error))
    {
        cleanQuest_ = quest_;
        SetStatus(std::format("Saved {} POI record(s) and their points in one transaction.", quest_.pois.size()));
        return true;
    }
    SetStatus(error, true);
    return false;
}

void EditorApp::RecordUndo(Quest const& snapshot)
{
    constexpr std::size_t maximumHistory = 100;
    if (undoHistory_.empty() || undoHistory_.back() != snapshot)
    {
        undoHistory_.push_back(snapshot);
        if (undoHistory_.size() > maximumHistory)
            undoHistory_.erase(undoHistory_.begin());
    }
    redoHistory_.clear();
}

void EditorApp::RestoreQuest(Quest snapshot)
{
    quest_ = std::move(snapshot);
    pointDragActive_ = false;
    if (quest_.pois.empty())
    {
        selectedPoi_ = -1;
        selectedPoint_ = -1;
        return;
    }
    selectedPoi_ = std::clamp(selectedPoi_, 0, static_cast<int>(quest_.pois.size() - 1));
    auto const pointCount = static_cast<int>(quest_.pois[selectedPoi_].points.size());
    selectedPoint_ = pointCount == 0 ? -1 : std::clamp(selectedPoint_, -1, pointCount - 1);
    LoadMap(quest_.pois[selectedPoi_].worldMapAreaId, quest_.pois[selectedPoi_].floor);
}

void EditorApp::Undo()
{
    if (undoHistory_.empty())
        return;
    redoHistory_.push_back(quest_);
    auto snapshot = std::move(undoHistory_.back());
    undoHistory_.pop_back();
    RestoreQuest(std::move(snapshot));
    SetStatus("Undid the last POI edit.");
}

void EditorApp::Redo()
{
    if (redoHistory_.empty())
        return;
    undoHistory_.push_back(quest_);
    auto snapshot = std::move(redoHistory_.back());
    redoHistory_.pop_back();
    RestoreQuest(std::move(snapshot));
    SetStatus("Restored the last undone POI edit.");
}

void EditorApp::SelectPoi(int index)
{
    selectedPoi_ = index; selectedPoint_ = -1;
    if (index >= 0 && index < static_cast<int>(quest_.pois.size()))
        LoadMap(quest_.pois[index].worldMapAreaId, quest_.pois[index].floor);
}

bool EditorApp::LoadMap(std::uint32_t worldMapAreaId, std::uint32_t floor)
{
    if (mapTexture_ && loadedArea_ == worldMapAreaId && loadedFloor_ == floor)
        return true;
    ReleaseMapTexture();
    loadedArea_ = 0;
    loadedFloor_ = 0;
    auto found = std::find_if(areas_.begin(), areas_.end(), [worldMapAreaId](auto const& area) { return area.id == worldMapAreaId; });
    if (found == areas_.end())
    {
        SetStatus(std::format("WorldMapAreaId {} is not present in the client's DBC.", worldMapAreaId), true); return false;
    }
    std::vector<RgbaImage> tiles(12);
    std::string lastError;
    for (int index = 1; index <= 12; ++index)
    {
        std::vector<std::string> names;
        auto const folder = "Interface\\WorldMap\\" + found->textureName + "\\";
        if (floor > 0)
        {
            names.push_back(folder + found->textureName + std::to_string(floor) + "_" + std::to_string(index) + ".blp");
            names.push_back(folder + found->textureName + std::to_string(floor) + std::to_string(index) + ".blp");
        }
        names.push_back(folder + found->textureName + std::to_string(index) + ".blp");
        for (auto const& name : names)
        {
            auto bytes = archives_.Read(name, lastError);
            if (bytes.empty()) continue;
            std::string decodeError;
            tiles[index - 1] = DecodeBlp(bytes, decodeError);
            if (!tiles[index - 1].Empty()) break;
            lastError = decodeError;
        }
        if (tiles[index - 1].Empty())
        {
            SetStatus(std::format("Could not load tile {} for {}: {}", index, found->textureName, lastError), true); return false;
        }
    }
    auto stitched = StitchMapTiles(tiles);
    if (!UploadMap(stitched))
    {
        SetStatus("OpenGL could not create the stitched map texture.", true); return false;
    }
    loadedArea_ = worldMapAreaId; loadedFloor_ = floor;
    mapZoom_ = 1.0f;
    mapPanX_ = 0.0f;
    mapPanY_ = 0.0f;
    SetStatus(std::format("Loaded {} (WorldMapAreaId {}, floor {}).", found->textureName, worldMapAreaId, floor));
    return true;
}

bool EditorApp::UploadMap(RgbaImage const& image)
{
    if (image.Empty())
        return false;
    unsigned int texture = 0;
    glGenTextures(1, &texture);
    if (!texture)
        return false;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(image.width),
        static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
    auto const ok = glGetError() == GL_NO_ERROR;
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!ok)
    {
        glDeleteTextures(1, &texture);
        return false;
    }
    ReleaseMapTexture();
    mapTexture_ = texture;
    return true;
}

void EditorApp::ReleaseMapTexture()
{
    if (mapTexture_)
        glDeleteTextures(1, &mapTexture_);
    mapTexture_ = 0;
}
}
