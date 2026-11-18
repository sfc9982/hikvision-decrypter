#pragma once

#include "localization.h"
#include <wx/wx.h>
#include <functional>
#include <thread>
#include <vector>

class wxChoice;
class wxGauge;
class wxSpinCtrl;
class wxBoxSizer;
class StringsList;

class cMain : public wxFrame
{
public:
    explicit cMain(localization::Preference preference = localization::Preference::System);
    ~cMain() override;
    void SetLanguage(localization::Preference preference);
    bool LoadFile(const wxString& path);

private:
    wxString Ui(const char* chinese, const char* english) const
    {
        return localization::Text(m_language, chinese, english);
    }

    void BuildInterface();
    void LocalizeLabel(wxControl* control, localization::Message message);
    void LocalizeTooltip(wxWindow* window, localization::Message message);
    void OnLanguageChanged(wxCommandEvent&);
    void OpenFile(wxCommandEvent&);
    void SaveFile(wxCommandEvent&);
    void StartConversion(wxCommandEvent&);
    void OnCompleted(wxThreadEvent&);
    void OnClose(wxCloseEvent&);
    void UpdateControls();
    void BuildPreview(wxBoxSizer* content);
    void RefreshPreview(bool resetPage = false);
    void FindInPreview(wxCommandEvent&);
    void ShowHexAt(size_t offset);
    const std::vector<unsigned char>& PreviewBytes() const;
    void SetStatus(localization::Message message = {});
    void UpdateStatusText();
    void UpdateResultText();
    void UpdatePreviewTextLabels();
    void UpdateMatchText();
    void ClearMatch();
    bool ConfirmDiscardResult(const wxString& title, const wxString& action, const wxString& message);

    localization::Language m_language;
    localization::Preference m_languagePreference;
    std::vector<std::function<void()>> m_languageUpdates;
    localization::Message m_statusMessage;
    bool m_stringsIndexed = true;
    bool m_hasSearchResult = false;
    bool m_searchWrapped = false;

    wxButton* m_openButton = nullptr;
    wxButton* m_convertButton = nullptr;
    wxButton* m_saveButton = nullptr;
    wxButton* m_folderButton = nullptr;
    wxChoice* m_modeChoice = nullptr;
    wxGauge* m_progress = nullptr;
    wxStaticText* m_fileName = nullptr;
    wxStaticText* m_validation = nullptr;
    wxStaticText* m_resultDetail = nullptr;
    wxStaticText* m_status = nullptr;
    wxChoice* m_previewSource = nullptr;
    wxChoice* m_previewFormat = nullptr;
    wxSpinCtrl* m_previewPage = nullptr;
    wxButton* m_previousPage = nullptr;
    wxButton* m_nextPage = nullptr;
    wxButton* m_findButton = nullptr;
    wxTextCtrl* m_previewText = nullptr;
    StringsList* m_previewStrings = nullptr;
    wxTextCtrl* m_previewQuery = nullptr;
    wxStaticText* m_previewInfo = nullptr;
    wxStaticText* m_previewMatch = nullptr;
    size_t m_searchNext = 0;
    int m_renderedPage = 0;
    wxPanel* m_workspace = nullptr;
    wxTimer m_pulseTimer;
    std::thread m_worker;
    std::vector<unsigned char> m_input;
    std::vector<unsigned char> m_result;
    wxString m_sourcePath;
    wxString m_exportPath;
    bool m_busy = false;
    bool m_resultEncrypted = false;
    bool m_resultDirty = false;
};
