#pragma once

#include "Models.h"
#include "db/Database.h"
#include "wow/MpqArchive.h"
#include "wow/WorldMap.h"

#include <array>
#include <functional>
#include <optional>
#include <utility>

namespace qpe
{
class EditorApp
{
public:
    explicit EditorApp(std::function<std::pair<int, int>(int, int)> resizeWindow = {});
    ~EditorApp();
    void Render();
    void RequestExit();
    [[nodiscard]] bool ExitReady() const { return exitReady_; }

private:
    enum class PendingAction
    {
        None,
        LoadQuest,
        Disconnect,
        Exit
    };

    void RenderToolbar();
    void RenderQuestBrowser();
    void RenderPoiPanel();
    void RenderCanvas();
    void RenderSettings();
    void RenderInfo();
    void RenderSaveConfirmation();
    void RenderUnsavedChanges();
    void LoadSettings();
    void SaveSettings();
    void ApplySettings(bool connectDatabase = false);
    void RequestLoadQuest(std::uint32_t questId);
    void LoadQuest(std::uint32_t questId);
    void RequestDisconnect();
    void DisconnectNow();
    void PerformPendingAction();
    bool RefreshQuestList(bool updateStatus = true);
    void RebuildQuestFilter();
    [[nodiscard]] bool SaveQuest();
    [[nodiscard]] bool IsDirty() const;
    void SelectPoi(int index);
    void RecordUndo(Quest const& snapshot);
    void Undo();
    void Redo();
    void RestoreQuest(Quest snapshot);
    bool LoadMap(std::uint32_t worldMapAreaId, std::uint32_t floor);
    bool UploadMap(RgbaImage const& image);
    void ReleaseMapTexture();
    WorldMapArea const* CurrentArea() const;
    void SetStatus(std::string message, bool error = false);

    unsigned int mapTexture_ = 0;
    AppSettings settings_;
    Database database_;
    MpqArchiveSet archives_;
    std::vector<WorldMapArea> areas_;
    std::vector<WorldMapOverlay> overlays_;
    Quest quest_;
    std::optional<Quest> cleanQuest_;
    std::vector<Quest> undoHistory_;
    std::vector<Quest> redoHistory_;
    std::vector<QuestSummary> questList_;
    std::vector<std::size_t> filteredQuestIndices_;
    int selectedPoi_ = -1;
    int selectedPoint_ = -1;
    std::uint32_t loadedArea_ = 0;
    std::uint32_t loadedFloor_ = 0;
    std::string status_ = "Set the client and database paths to begin.";
    bool statusError_ = false;
    bool settingsOpen_ = false;
    bool infoOpen_ = false;
    bool saveConfirmationOpen_ = false;
    bool unsavedPromptOpen_ = false;
    bool exitReady_ = false;
    bool settingsLoaded_ = false;
    bool pointDragActive_ = false;
    PendingAction pendingAction_ = PendingAction::None;
    std::uint32_t pendingQuestId_ = 0;
    float mapZoom_ = 1.0f;
    float mapPanX_ = 0.0f;
    float mapPanY_ = 0.0f;
    std::array<char, 1024> clientPath_ {};
    std::array<char, 256> questNameFilter_ {};
    std::array<char, 32> questIdFilter_ {};
    std::array<char, 16> locale_ {};
    std::array<char, 256> dbHost_ {};
    std::array<char, 128> dbUser_ {};
    std::array<char, 128> dbPassword_ {};
    std::array<char, 128> dbName_ {};
    int dbPort_ = 3306;
    int fontSize_ = 14;
    int windowWidth_ = 1600;
    int windowHeight_ = 900;
    std::function<std::pair<int, int>(int, int)> resizeWindow_;
    bool questIdAscending_ = true;
};
}
