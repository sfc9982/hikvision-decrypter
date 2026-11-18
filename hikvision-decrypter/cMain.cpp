#include "cMain.h"
#include "hikvision.h"
#include "preview.h"

#include <wx/display.h>
#include <wx/dnd.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/statline.h>
#include <wx/spinctrl.h>
#include <wx/utils.h>
#include <wx/wupdlock.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <new>
#include <utility>

namespace
{
    wxDEFINE_EVENT(ConversionCompleted, wxThreadEvent);

    enum { HexView, TextView, StringsView };
    enum { ConvertCommand = wxID_HIGHEST + 1, LanguageSystem, LanguageChinese, LanguageEnglish };
    constexpr localization::Message AlignmentError{"解密文件的大小须为 16 字节的倍数。", "File size must be a multiple of 16 bytes to decrypt."};
    constexpr size_t MaxInputBytes = 64 * 1024 * 1024;

    int LanguageMenuId(localization::Preference preference)
    {
        if (preference == localization::Preference::Chinese) return LanguageChinese;
        if (preference == localization::Preference::English) return LanguageEnglish;
        return LanguageSystem;
    }

    wxStaticText* Label(wxWindow* parent, const wxString& text, long style = 0)
    {
        return new wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, style);
    }

    wxStaticText* Heading(wxWindow* parent, const wxString& text)
    {
        auto* label = Label(parent, text);
        label->SetFont(label->GetFont().Bold());
        return label;
    }

    wxButton* Button(wxWindow* parent, const wxString& title)
    {
        return new wxButton(parent, wxID_ANY, title);
    }

    wxString FormatSize(size_t bytes)
    {
        if (bytes >= 1024 * 1024)
            return wxString::Format("%.2f MiB", static_cast<double>(bytes) / (1024 * 1024));
        if (bytes >= 1024)
            return wxString::Format("%.1f KiB", static_cast<double>(bytes) / 1024);
        return wxString::Format("%llu B", static_cast<unsigned long long>(bytes));
    }

    struct ConversionResult
    {
        std::vector<unsigned char> bytes;
        enum class Error { None, NoMemory, Failed };
        Error error = Error::None;
        bool encrypted = false;
    };

    class ConfigurationDropTarget : public wxFileDropTarget
    {
    public:
        ConfigurationDropTarget(wxWindow* parent, const localization::Language& language,
                                std::function<bool(const wxString&)> load)
            : m_parent(parent), m_language(language), m_load(std::move(load)) {}

        bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& paths) override
        {
            if (paths.size() != 1)
            {
                wxMessageDialog notice(m_parent, localization::Text(m_language, "一次只能打开一个文件。", "Open one file at a time."),
                    localization::Text(m_language, "拖放文件", "Drop File"), wxOK | wxICON_INFORMATION);
                notice.SetOKLabel(localization::Text(m_language, "确定", "OK"));
                notice.ShowModal();
                return false;
            }
            return m_load(paths[0]);
        }

    private:
        wxWindow* m_parent;
        const localization::Language& m_language;
        std::function<bool(const wxString&)> m_load;
    };
}

class StringsList final : public wxListCtrl
{
public:
    StringsList(wxWindow* parent, localization::Language language)
        : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL | wxBORDER_SIMPLE)
    {
        const int offsetWidth = FromDIP(104);
        const int lengthWidth = FromDIP(92);
        InsertColumn(0, wxEmptyString, wxLIST_FORMAT_LEFT, offsetWidth);
        InsertColumn(1, wxEmptyString, wxLIST_FORMAT_RIGHT, lengthWidth);
        InsertColumn(2, wxEmptyString);
        SetLanguage(language);
        EnableAlternateRowColours();
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            SetColumnWidth(2, std::max(FromDIP(120), GetClientSize().x - GetColumnWidth(0)
                                       - GetColumnWidth(1) - FromDIP(20)));
            event.Skip();
        });
    }

    void SetLanguage(localization::Language language)
    {
        const wxString labels[]{localization::Text(language, "文件偏移", "File offset"),
                                localization::Text(language, "长度（字节）", "Bytes"),
                                localization::Text(language, "字符串", "String")};
        for (int column = 0; column < 3; ++column)
        {
            wxListItem item;
            item.SetText(labels[column]);
            SetColumn(column, item);
        }
        SetToolTip(localization::Text(language, "双击或按 Enter 跳到十六进制位置",
            "Double-click or press Enter to jump to this offset in hex"));
    }

    void ClearSource()
    {
        SetItemCount(0);
        m_bytes = nullptr;
        std::vector<preview::StringRange>().swap(m_index);
    }

    void SetSource(const std::vector<unsigned char>& bytes)
    {
        if (m_bytes == &bytes) return;
        m_index = preview::IndexStrings(bytes);
        m_bytes = &bytes;
        SetItemCount(static_cast<long>(m_index.size()));
    }

    size_t OffsetAt(long row) const
    {
        return row >= 0 && static_cast<size_t>(row) < m_index.size()
            ? m_index[static_cast<size_t>(row)].offset : preview::NotFound;
    }

    preview::StringMatch Find(const std::string& query, size_t start) const
    {
        return m_bytes ? preview::FindString(*m_bytes, m_index, query, start) : preview::StringMatch{};
    }

    void Reveal(long row)
    {
        SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                     wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        EnsureVisible(row);
    }

private:
    wxString OnGetItemText(long row, long column) const override
    {
        if (!m_bytes || OffsetAt(row) == preview::NotFound) return {};
        const auto& range = m_index[static_cast<size_t>(row)];
        if (column == 0) return wxString::Format("0x%08lX", static_cast<unsigned long>(range.offset));
        if (column == 1) return wxString::Format("%lu", static_cast<unsigned long>(range.length));

        // Avoid converting a huge printable run just to paint a single cell.
        const auto* data = m_bytes->data() + range.offset;
        size_t length = std::min<size_t>(range.length, 1024);
        if (length < range.length)
            while ((data[length] & 0xc0) == 0x80) --length;
        return wxString::FromUTF8(reinterpret_cast<const char*>(data), length)
            + (length < range.length ? wxString::FromUTF8("…") : wxString{});
    }

    const std::vector<unsigned char>* m_bytes = nullptr;
    std::vector<preview::StringRange> m_index;
};

cMain::cMain(localization::Preference preference)
    : wxFrame(nullptr, wxID_ANY, wxEmptyString), m_language(localization::Resolve(preference)),
      m_languagePreference(preference), m_pulseTimer(this)
{
    BuildInterface();

    Bind(wxEVT_MENU, &cMain::OpenFile, this, wxID_OPEN);
    Bind(wxEVT_MENU, &cMain::SaveFile, this, wxID_SAVE);
    Bind(wxEVT_MENU, &cMain::StartConversion, this, ConvertCommand);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        wxMessageDialog about(this, Ui("Hikvision 配置工具\n\n基于 WormChickenWizard 的 Hikvision Decrypter。\n许可证：GPL v3，详见 LICENSE。", "Hikvision Configuration Tool\n\nBased on Hikvision Decrypter by WormChickenWizard.\nLicensed under GPL v3. See LICENSE."),
                     Ui("关于", "About"), wxOK | wxICON_INFORMATION);
        about.SetOKLabel(Ui("确定", "OK"));
        about.ShowModal();
    }, wxID_ABOUT);
    Bind(ConversionCompleted, &cMain::OnCompleted, this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { if (m_busy) m_progress->Pulse(); });
    Bind(wxEVT_CLOSE_WINDOW, &cMain::OnClose, this);

    wxAcceleratorEntry shortcuts[3];
    shortcuts[0].Set(wxACCEL_CTRL, 'O', wxID_OPEN);
    shortcuts[1].Set(wxACCEL_CTRL, 'S', wxID_SAVE);
    shortcuts[2].Set(wxACCEL_CTRL, WXK_RETURN, ConvertCommand);
    SetAcceleratorTable(wxAcceleratorTable(3, shortcuts));
    SetDropTarget(new ConfigurationDropTarget(this, m_language, [this](const wxString& path) { return LoadFile(path); }));

    SetMinClientSize(FromDIP(wxSize(720, 420)));
    const wxSize wanted = FromDIP(wxSize(1000, 490));
    const wxRect available = wxDisplay(static_cast<unsigned>(0)).GetClientArea();
    SetClientSize(wxSize(std::min(wanted.x, available.width - FromDIP(64)),
                         std::min(wanted.y, available.height - FromDIP(96))));
    Centre();
    SetLanguage(preference);
    SetStatus({"打开或拖入配置文件", "Open or drop a configuration file"});
}

cMain::~cMain()
{
    m_pulseTimer.Stop();
    m_previewStrings->ClearSource();
    // The window remains alive until the worker has stopped posting events.
    if (m_worker.joinable()) m_worker.join();
}

void cMain::BuildInterface()
{
    auto* fileMenu = new wxMenu;
    fileMenu->Append(wxID_OPEN, wxEmptyString);
    fileMenu->Append(wxID_SAVE, wxEmptyString);
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, wxEmptyString);
    auto* helpMenu = new wxMenu;
    helpMenu->Append(wxID_ABOUT, wxEmptyString);
    auto* menus = new wxMenuBar;
    menus->Append(fileMenu, " ");
    auto* languageMenu = new wxMenu;
    languageMenu->AppendRadioItem(LanguageSystem, "System");
    languageMenu->AppendRadioItem(LanguageChinese, wxString::FromUTF8("简体中文"));
    languageMenu->AppendRadioItem(LanguageEnglish, "English");
    languageMenu->Check(LanguageMenuId(m_languagePreference), true);
    menus->Append(languageMenu, " ");
    Bind(wxEVT_MENU, &cMain::OnLanguageChanged, this, LanguageSystem, LanguageEnglish);
    menus->Append(helpMenu, " ");
    SetMenuBar(menus);

    m_languageUpdates.emplace_back([this, menus] {
        SetTitle(Ui("Hikvision 配置工具", "Hikvision Configuration Tool"));
        menus->SetMenuLabel(0, Ui("文件", "File"));
        menus->SetMenuLabel(1, Ui("语言", "Language"));
        menus->SetMenuLabel(2, Ui("帮助", "Help"));
        menus->SetLabel(wxID_OPEN, Ui("打开文件…\tCtrl+O", "Open File…\tCtrl+O"));
        menus->SetLabel(wxID_SAVE, Ui("另存为…\tCtrl+S", "Save As…\tCtrl+S"));
        menus->SetLabel(wxID_EXIT, Ui("退出", "Exit"));
        menus->SetLabel(LanguageSystem, Ui("跟随系统", "System default"));
        menus->SetLabel(wxID_ABOUT, Ui("关于", "About"));
    });

    m_workspace = new wxPanel(this);
    const int gap = FromDIP(6);
    const int pad = FromDIP(10);
    auto* tools = new wxBoxSizer(wxVERTICAL);
    tools->SetMinSize(FromDIP(wxSize(232, -1)));
    auto section = [&](localization::Message title, bool first = false) {
        if (!first)
            tools->Add(new wxStaticLine(m_workspace), 0, wxEXPAND | wxTOP | wxBOTTOM, pad);
        auto* heading = Heading(m_workspace, wxEmptyString);
        LocalizeLabel(heading, title);
        tools->Add(heading, 0, wxBOTTOM, gap);
    };

    section({"输入文件", "Input"}, true);
    auto* fileRow = new wxBoxSizer(wxHORIZONTAL);
    m_fileName = Label(m_workspace, wxEmptyString, wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE);
    m_fileName->SetMinSize(FromDIP(wxSize(60, -1)));
    fileRow->Add(m_fileName, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    m_openButton = Button(m_workspace, wxEmptyString);
    LocalizeLabel(m_openButton, {"浏览…", "Browse…"});
    LocalizeTooltip(m_openButton, {"打开文件（Ctrl+O）或拖入文件", "Open a file (Ctrl+O) or drag it here"});
    fileRow->Add(m_openButton, 0);
    tools->Add(fileRow, 0, wxEXPAND | wxBOTTOM, gap);
    section({"转换", "Convert"});
    auto* operationRow = new wxBoxSizer(wxHORIZONTAL);
    const wxArrayString modes{Ui("解密", "Decrypt"), Ui("加密", "Encrypt")};
    m_modeChoice = new wxChoice(m_workspace, wxID_ANY, wxDefaultPosition, wxDefaultSize, modes);
    m_modeChoice->SetSelection(0);

    operationRow->Add(m_modeChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    m_convertButton = Button(m_workspace, wxEmptyString);
    LocalizeLabel(m_convertButton, {"执行", "Run"});
    m_convertButton->SetName("convert-command");
    LocalizeTooltip(m_convertButton, {"开始转换（Ctrl+Enter）", "Start conversion (Ctrl+Enter)"});
    operationRow->Add(m_convertButton);
    tools->Add(operationRow, 0, wxEXPAND);
    m_validation = Label(m_workspace, wxEmptyString);
    tools->Add(m_validation, 0, wxTOP, gap);
    m_validation->Hide();

    section({"导出", "Export"});
    m_resultDetail = Label(m_workspace, wxEmptyString, wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
    tools->Add(m_resultDetail, 0, wxEXPAND | wxBOTTOM, gap);
    m_resultDetail->Hide();
    auto* exports = new wxBoxSizer(wxHORIZONTAL);
    m_saveButton = Button(m_workspace, wxEmptyString);
    LocalizeLabel(m_saveButton, {"另存为…", "Save As…"});
    LocalizeTooltip(m_saveButton, {"另存结果（Ctrl+S）", "Save the result (Ctrl+S)"});
    m_folderButton = Button(m_workspace, wxEmptyString);
    LocalizeLabel(m_folderButton, {"打开目录", "Open Folder"});
    LocalizeTooltip(m_folderButton, {"打开导出目录", "Open the export folder"});
    exports->Add(m_saveButton, 1, wxRIGHT, gap);
    exports->Add(m_folderButton, 1);
    tools->Add(exports, 0, wxEXPAND);

    auto* panes = new wxBoxSizer(wxHORIZONTAL);
    panes->Add(tools, 0, wxEXPAND | wxALL, pad);
    panes->Add(new wxStaticLine(m_workspace, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_VERTICAL), 0, wxEXPAND);
    auto* preview = new wxBoxSizer(wxVERTICAL);
    BuildPreview(preview);
    panes->Add(preview, 1, wxEXPAND | wxALL, pad);

    auto* footer = new wxBoxSizer(wxHORIZONTAL);
    m_status = Label(m_workspace, wxEmptyString, wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
    footer->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad);
    m_progress = new wxGauge(m_workspace, wxID_ANY, 100, wxDefaultPosition,
                             FromDIP(wxSize(96, 8)), wxGA_HORIZONTAL);
    footer->Add(m_progress, 0, wxALIGN_CENTER_VERTICAL);
    m_progress->Hide();
    auto* content = new wxBoxSizer(wxVERTICAL);
    content->Add(panes, 1, wxEXPAND);
    content->Add(new wxStaticLine(m_workspace), 0, wxEXPAND);
    content->Add(footer, 0, wxEXPAND | wxALL, gap);
    m_workspace->SetSizer(content);
    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(m_workspace, 1, wxEXPAND);
    SetSizer(root);
    RefreshPreview();
    m_openButton->Bind(wxEVT_BUTTON, &cMain::OpenFile, this);
    m_convertButton->Bind(wxEVT_BUTTON, &cMain::StartConversion, this);
    m_saveButton->Bind(wxEVT_BUTTON, &cMain::SaveFile, this);
    m_modeChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateControls(); });
    m_folderButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!m_exportPath.empty() && !wxLaunchDefaultApplication(wxFileName(m_exportPath).GetPath()))
            SetStatus({"打不开导出目录，请在文件管理器中打开。", "Could not open the export folder. Try opening it in your file manager."});
    });
}

void cMain::OnLanguageChanged(wxCommandEvent& event)
{
    using localization::Preference;
    const auto preference = event.GetId() == LanguageChinese ? Preference::Chinese
        : event.GetId() == LanguageEnglish ? Preference::English : Preference::System;
    if (preference == m_languagePreference) return;

    if (localization::WritePreference(preference))
    {
        SetLanguage(preference);
        return;
    }
    GetMenuBar()->Check(LanguageMenuId(m_languagePreference), true);
    wxMessageDialog notice(this, Ui("无法保存语言设置。", "Could not save the language setting."),
                           Ui("语言", "Language"), wxOK | wxICON_WARNING);
    notice.SetOKLabel(Ui("确定", "OK"));
    notice.ShowModal();
}

void cMain::LocalizeLabel(wxControl* control, localization::Message message)
{
    m_languageUpdates.emplace_back([this, control, message] {
        control->SetLabel(Ui(message.chinese, message.english));
        control->InvalidateBestSize();
    });
}

void cMain::LocalizeTooltip(wxWindow* window, localization::Message message)
{
    m_languageUpdates.emplace_back([this, window, message] {
        window->SetToolTip(Ui(message.chinese, message.english));
    });
}

void cMain::SetLanguage(localization::Preference preference)
{
    m_languagePreference = preference;
    m_language = localization::Resolve(preference);
    GetMenuBar()->Check(LanguageMenuId(preference), true);

    wxWindowUpdateLocker lock(m_workspace);
    for (const auto& update : m_languageUpdates) update();
    auto relabelChoice = [](wxChoice* choice, const wxArrayString& labels) {
        const int selected = choice->GetSelection();
        choice->Set(labels);
        choice->SetSelection(selected);
        choice->InvalidateBestSize();
    };
    relabelChoice(m_modeChoice, {Ui("解密", "Decrypt"), Ui("加密", "Encrypt")});
    m_modeChoice->SetName(Ui("处理方式", "Conversion mode"));
    relabelChoice(m_previewSource, {Ui("输入文件", "Input"), Ui("转换结果", "Result")});
    relabelChoice(m_previewFormat, {Ui("十六进制", "Hex"), Ui("UTF-8 文本", "UTF-8 text"), Ui("可读字符串", "Strings")});
    m_validation->SetLabel(Ui(AlignmentError.chinese, AlignmentError.english));
    m_validation->Wrap(FromDIP(232));
    m_previewStrings->SetLanguage(m_language);
    UpdateStatusText();
    UpdateResultText();
    UpdatePreviewTextLabels();
    UpdateMatchText();
    Layout();
    m_workspace->Layout();
}

void cMain::SetStatus(localization::Message message)
{
    m_statusMessage = message;
    UpdateStatusText();
    UpdateControls();
}

void cMain::UpdateStatusText()
{
    const auto message = Ui(m_statusMessage.chinese, m_statusMessage.english);
    m_status->SetLabel(message);
    if (message.empty()) m_status->UnsetToolTip();
    else m_status->SetToolTip(message);
}

void cMain::UpdateResultText()
{
    if (m_result.empty()) m_resultDetail->SetLabel(wxEmptyString);
    else if (m_exportPath.empty())
        m_resultDetail->SetLabel(FormatSize(m_result.size()) + Ui(" · 未导出", " · Not exported"));
    else
        m_resultDetail->SetLabel(FormatSize(m_result.size()) + Ui(" · 已导出 · ", " · Exported · ")
                                 + wxFileName(m_exportPath).GetFullName());
}

void cMain::UpdateControls()
{
    const bool loaded = !m_input.empty();
    const bool encrypt = m_modeChoice->GetSelection() == 1;
    const bool valid = loaded && (encrypt || m_input.size() % 16 == 0);
    const bool output = !m_result.empty();
    m_openButton->Enable(!m_busy);
    m_modeChoice->Enable(!m_busy);
    m_convertButton->Enable(!m_busy && valid);
    m_saveButton->Enable(!m_busy && output);
    m_folderButton->Enable(!m_busy && !m_exportPath.empty());
    m_progress->Show(m_busy);
    m_resultDetail->Show(output);
    GetMenuBar()->Enable(wxID_OPEN, !m_busy);
    GetMenuBar()->Enable(wxID_SAVE, !m_busy && output);
    m_validation->Show(loaded && !valid);
    m_workspace->Layout();
}

bool cMain::ConfirmDiscardResult(const wxString& title, const wxString& action, const wxString& message)
{
    if (!m_resultDirty) return true;
    wxMessageDialog confirm(this, message, title, wxOK | wxCANCEL | wxCANCEL_DEFAULT | wxICON_WARNING);
    confirm.SetOKCancelLabels(action, Ui("取消", "Cancel"));
    return confirm.ShowModal() == wxID_OK;
}

void cMain::OpenFile(wxCommandEvent&)
{
    if (m_busy) return;
    wxFileDialog dialog(this, Ui("打开配置文件", "Open Configuration File"), wxEmptyString, wxEmptyString,
                        Ui("所有文件 (*.*)|*.*", "All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) LoadFile(dialog.GetPath());
}

bool cMain::LoadFile(const wxString& path)
{
    if (m_busy)
    {
        SetStatus({"转换完成后可打开文件。", "Wait for conversion to finish before opening a file."});
        return false;
    }
    if (!ConfirmDiscardResult(Ui("打开新文件？", "Open a new file?"), Ui("打开", "Open"), Ui("未导出的结果将丢失。", "The unexported result will be lost."))) return false;
    wxFile file(path, wxFile::read);
    if (!file.IsOpened())
    {
        SetStatus({"打不开文件，请检查路径和读取权限。", "Could not open the file. Check the path and read permissions."});
        return false;
    }
    const wxFileOffset length = file.Length();
    if (length <= 0 || length > static_cast<wxFileOffset>(MaxInputBytes))
    {
        SetStatus(length < 0 ? localization::Message{"无法获取文件大小。", "Could not get the file size."}
            : length == 0 ? localization::Message{"文件为空。", "The file is empty."} : localization::Message{"文件超过 64 MiB。", "The file exceeds 64 MiB."});
        return false;
    }
    try
    {
        std::vector<unsigned char> bytes(static_cast<size_t>(length));
        if (file.Read(bytes.data(), bytes.size()) != static_cast<wxFileOffset>(bytes.size()))
        {
            SetStatus({"文件未读完，请重新打开。", "Could not read the entire file. Open it again."});
            return false;
        }
        if (!file.Close())
        {
            SetStatus({"关闭文件时出错，请重新打开。", "Could not close the file. Open it again."});
            return false;
        }
        m_input = std::move(bytes);
    }
    catch (const std::exception&)
    {
        SetStatus({"内存不足，请关闭其他程序或打开较小的文件。", "Not enough memory. Close other apps or open a smaller file."});
        return false;
    }
    m_sourcePath = wxFileName(path).GetFullPath();
    m_result.clear();
    m_exportPath.clear();
    m_resultDirty = false;
    m_fileName->SetLabel(wxFileName(path).GetFullName());
    m_fileName->SetToolTip(m_sourcePath + "\n" + FormatSize(m_input.size()));
    m_resultDetail->UnsetToolTip();
    UpdateResultText();
    m_progress->SetValue(0);
    m_previewSource->SetSelection(0);
    m_previewFormat->SetSelection(HexView);
    m_previewQuery->Clear();
    RefreshPreview(true);
    SetStatus();
    return true;
}

void cMain::StartConversion(wxCommandEvent&)
{
    if (m_busy) return;
    const bool encrypt = m_modeChoice->GetSelection() == 1;
    if (m_input.empty() || (!encrypt && m_input.size() % 16 != 0))
    {
        SetStatus(m_input.empty() ? localization::Message{"请先打开文件。", "Open a file first."} : AlignmentError);
        return;
    }
    if (!ConfirmDiscardResult(Ui("覆盖结果？", "Replace the result?"), Ui("覆盖", "Replace"), Ui("新结果将替换未导出的结果。", "The new result will replace the unexported result."))) return;
    m_busy = true;
    SetStatus(encrypt ? localization::Message{"正在加密…", "Encrypting…"} : localization::Message{"正在解密…", "Decrypting…"});
    m_pulseTimer.Start(90);
    try
    {
        auto result = std::make_shared<ConversionResult>();
        result->encrypted = encrypt;
        // Loading is blocked while the worker reads m_input; destruction joins it.
        m_worker = std::thread([this, result, encrypt]() {
            try
            {
                result->bytes = transformConfiguration(m_input,
                    encrypt ? ConversionMode::Encrypt : ConversionMode::Decrypt);
            }
            catch (const std::bad_alloc&)
            {
                result->error = ConversionResult::Error::NoMemory;
            }
            catch (...)
            {
                result->error = ConversionResult::Error::Failed;
            }
            auto* completed = new wxThreadEvent(ConversionCompleted);
            completed->SetPayload(result);
            wxQueueEvent(this, completed);
        });
    }
    catch (const std::exception&)
    {
        m_busy = false;
        m_pulseTimer.Stop();
        m_progress->SetValue(0);
        SetStatus({"无法开始转换，请重试。", "Could not start conversion. Try again."});
    }
}

void cMain::OnCompleted(wxThreadEvent& event)
{
    if (m_worker.joinable()) m_worker.join();
    m_pulseTimer.Stop();
    m_busy = false;
    const auto result = event.GetPayload<std::shared_ptr<ConversionResult>>();
    if (result->error != ConversionResult::Error::None)
    {
        m_progress->SetValue(0);
        SetStatus(result->error == ConversionResult::Error::NoMemory
            ? localization::Message{"内存不足，未生成新结果。", "Not enough memory. No new result was created."} : localization::Message{"转换失败，未生成新结果。", "Conversion failed. No new result was created."});
        return;
    }
    m_result = std::move(result->bytes);
    m_resultEncrypted = result->encrypted;
    m_resultDirty = true;
    m_exportPath.clear();
    m_resultDetail->UnsetToolTip();
    UpdateResultText();
    m_previewSource->SetSelection(1);
    m_previewFormat->SetSelection(m_resultEncrypted ? HexView : StringsView);
    m_previewQuery->Clear();
    RefreshPreview(true);
    SetStatus();
}

void cMain::SaveFile(wxCommandEvent&)
{
    if (m_busy || m_result.empty()) return;
    const wxFileName source(m_sourcePath);
    const wxString suggested = source.GetName() + (m_resultEncrypted ? ".encrypted.bin" : ".decrypted.bin");
    wxFileDialog dialog(this, Ui("另存结果", "Save Result"), source.GetPath(), suggested,
                        Ui("二进制文件 (*.bin)|*.bin|所有文件 (*.*)|*.*", "Binary files (*.bin)|*.bin|All files (*.*)|*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return;
    if (source.SameAs(wxFileName(dialog.GetPath())))
    {
        SetStatus({"不能覆盖原文件，请另选路径。", "Choose a different path to keep the original file."});
        return;
    }
    wxTempFile output(dialog.GetPath());
    if (!output.IsOpened() || !output.Write(m_result.data(), m_result.size()) || !output.Flush() || !output.Commit())
    {
        SetStatus({"导出失败，请检查写入权限和磁盘空间。", "Export failed. Check write permissions and free disk space."});
        return;
    }
    m_exportPath = dialog.GetPath();
    m_resultDirty = false;
    UpdateResultText();
    m_resultDetail->SetToolTip(m_exportPath);
    SetStatus();
}

void cMain::OnClose(wxCloseEvent& event)
{
    if (event.CanVeto())
    {
        if (m_busy)
        {
            event.Veto();
            SetStatus({"转换完成后可退出。", "Wait for conversion to finish before exiting."});
            return;
        }
        if (!ConfirmDiscardResult(Ui("退出？", "Exit?"), Ui("退出", "Exit"), Ui("未导出的结果将丢失。", "The unexported result will be lost.")))
        {
            event.Veto();
            return;
        }
    }
    event.Skip();
}


void cMain::BuildPreview(wxBoxSizer* content)
{
    auto* toolbar = new wxBoxSizer(wxHORIZONTAL);
    auto* heading = Heading(m_workspace, wxEmptyString);
    LocalizeLabel(heading, {"预览", "Preview"});
    toolbar->Add(heading, 1, wxALIGN_CENTER_VERTICAL);
    const wxArrayString sources{Ui("输入文件", "Input"), Ui("转换结果", "Result")};
    m_previewSource = new wxChoice(m_workspace, wxID_ANY, wxDefaultPosition, wxDefaultSize, sources);
    m_previewSource->SetName("preview-source");
    m_previewSource->SetSelection(0);
    toolbar->Add(m_previewSource, 0, wxRIGHT, FromDIP(8));
    const wxArrayString formats{Ui("十六进制", "Hex"), Ui("UTF-8 文本", "UTF-8 text"), Ui("可读字符串", "Strings")};
    m_previewFormat = new wxChoice(m_workspace, wxID_ANY, wxDefaultPosition, wxDefaultSize, formats);
    m_previewFormat->SetName("preview-format");
    m_previewFormat->SetSelection(HexView);
    toolbar->Add(m_previewFormat, 0);
    content->Add(toolbar, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    m_previewText = new wxTextCtrl(m_workspace, wxID_ANY, wxEmptyString, wxDefaultPosition,
        FromDIP(wxSize(-1, 120)), wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP |
        wxTE_RICH2 | wxTE_NOHIDESEL | wxBORDER_SIMPLE);
    m_previewText->SetName("preview-content");
    m_previewText->SetFont(wxFont(wxFontInfo(m_workspace->GetFont().GetPointSize()).Family(wxFONTFAMILY_TELETYPE)));
    content->Add(m_previewText, 1, wxEXPAND | wxBOTTOM, FromDIP(8));

    m_previewStrings = new StringsList(m_workspace, m_language);
    m_previewStrings->SetName("preview-strings");
    m_previewStrings->SetMinSize(FromDIP(wxSize(-1, 120)));
    content->Add(m_previewStrings, 1, wxEXPAND | wxBOTTOM, FromDIP(8));
    m_previewStrings->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& event) {
        const auto offset = m_previewStrings->OffsetAt(event.GetIndex());
        if (offset != preview::NotFound)
        {
            ShowHexAt(offset);
            m_previewText->SetFocus();
        }
    });

    auto* navigation = new wxBoxSizer(wxHORIZONTAL);
    m_previewInfo = Label(m_workspace, wxEmptyString, wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
    m_previewInfo->SetMinSize(FromDIP(wxSize(160, 22)));
    navigation->Add(m_previewInfo, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_previousPage = Button(m_workspace, wxEmptyString);
    LocalizeLabel(m_previousPage, {"上一页", "Previous"});
    m_previousPage->SetName("preview-previous");
    m_previousPage->SetMinSize(FromDIP(wxSize(70, 28)));
    m_nextPage = Button(m_workspace, wxEmptyString);
    LocalizeLabel(m_nextPage, {"下一页", "Next"});
    m_nextPage->SetName("preview-next");
    m_nextPage->SetMinSize(FromDIP(wxSize(70, 28)));
    m_previewPage = new wxSpinCtrl(m_workspace, wxID_ANY, "1", wxDefaultPosition,
        FromDIP(wxSize(80, 28)), wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER, 1, 1, 1);
    m_previewPage->SetName("preview-page");
    navigation->Add(m_previousPage, 0, wxRIGHT, FromDIP(6));
    navigation->Add(m_previewPage, 0, wxRIGHT, FromDIP(6));
    navigation->Add(m_nextPage, 0);
    content->Add(navigation, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* search = new wxBoxSizer(wxHORIZONTAL);
    m_previewQuery = new wxTextCtrl(m_workspace, wxID_ANY, wxEmptyString, wxDefaultPosition,
        wxDefaultSize, wxTE_PROCESS_ENTER | wxBORDER_SIMPLE);
    m_previewQuery->SetName("preview-query");
    search->Add(m_previewQuery, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_findButton = Button(m_workspace, wxEmptyString);
    LocalizeLabel(m_findButton, {"查找下一个", "Find Next"});
    m_findButton->SetName("preview-find");
    search->Add(m_findButton, 0);
    content->Add(search, 0, wxEXPAND);
    m_previewMatch = Label(m_workspace, wxEmptyString);
    content->Add(m_previewMatch, 0, wxEXPAND | wxTOP, FromDIP(5));

    m_previewSource->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RefreshPreview(true); });
    m_previewFormat->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        m_searchNext = 0;
        RefreshPreview();
    });
    m_previousPage->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_previewPage->SetValue(m_previewPage->GetValue() - 1);
        RefreshPreview();
    });
    m_nextPage->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_previewPage->SetValue(m_previewPage->GetValue() + 1);
        RefreshPreview();
    });
    m_previewPage->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { RefreshPreview(); });
    m_previewPage->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { RefreshPreview(); });
    m_previewPage->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        // Ordinary focus changes must not reset the preview's scroll or selection.
        if (m_previewFormat->GetSelection() != StringsView && m_previewPage->GetValue() != m_renderedPage)
            RefreshPreview();
        event.Skip();
    });
    m_previewQuery->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
        m_searchNext = 0;
        ClearMatch();
    });
    m_previewQuery->Bind(wxEVT_TEXT_ENTER, &cMain::FindInPreview, this);
    m_findButton->Bind(wxEVT_BUTTON, &cMain::FindInPreview, this);
}

const std::vector<unsigned char>& cMain::PreviewBytes() const
{
    return m_previewSource->GetSelection() == 1 ? m_result : m_input;
}

void cMain::RefreshPreview(bool resetPage)
{
    if (resetPage)
    {
        m_previewStrings->ClearSource();
        m_previewPage->SetValue(1);
        m_searchNext = 0;
    }
    ClearMatch();
    const auto& bytes = PreviewBytes();
    const int view = m_previewFormat->GetSelection();
    const bool strings = view == StringsView;
    const bool available = !bytes.empty();
    m_previewText->Show(!strings);
    m_previewStrings->Show(strings);
    m_previewPage->Show(!strings);
    m_previousPage->Show(!strings);
    m_nextPage->Show(!strings);

    if (strings)
    {
        m_stringsIndexed = true;
        try { m_previewStrings->SetSource(bytes); }
        catch (const std::exception&) { m_stringsIndexed = false; }
        const bool searchable = m_previewStrings->GetItemCount() > 0;
        m_previewQuery->Enable(searchable);
        m_findButton->Enable(searchable);
        UpdatePreviewTextLabels();
        m_workspace->Layout();
        return;
    }

    const auto page = preview::Render(bytes, static_cast<size_t>(std::max(1, m_previewPage->GetValue()) - 1),
        view == TextView ? preview::Format::Text : preview::Format::Hex);
    m_previewPage->SetRange(1, static_cast<int>(std::max(size_t(1), page.count)));
    m_renderedPage = static_cast<int>(page.index + 1);
    m_previewPage->SetValue(m_renderedPage);
    m_previewPage->Enable(available);
    m_previousPage->Enable(available && page.index > 0);
    m_nextPage->Enable(available && page.index + 1 < page.count);
    m_findButton->Enable(available);
    m_previewQuery->Enable(available);
    if (available)
    {
        wxWindowUpdateLocker lock(m_previewText);
        m_previewText->ChangeValue(wxString::FromUTF8(page.text.data(), page.text.size()));
        m_previewText->SetInsertionPoint(0);
        m_previewText->ShowPosition(0);
    }
    UpdatePreviewTextLabels();
    m_workspace->Layout();
}

void cMain::UpdatePreviewTextLabels()
{
    const auto& bytes = PreviewBytes();
    const auto emptyMessage = m_previewSource->GetSelection() == 1
        ? Ui("暂无结果", "No result yet") : Ui("未打开文件", "No file open");
    wxString info;
    if (m_previewFormat->GetSelection() == StringsView)
    {
        const long count = m_previewStrings->GetItemCount();
        info = bytes.empty() ? emptyMessage
            : !m_stringsIndexed ? Ui("内存不足，无法列出字符串", "Not enough memory to list strings")
            : count == 0 ? Ui("没有可读字符串", "No readable strings")
            : wxString::Format(Ui("%ld 条字符串", "Strings: %ld"), count);
        m_previewQuery->SetHint(Ui("查找字符串（区分大小写）", "Find strings (case-sensitive)"));
    }
    else
    {
        m_previewQuery->SetHint(Ui("查找 UTF-8 文本（区分大小写）", "Find UTF-8 text (case-sensitive)"));
        if (bytes.empty()) m_previewText->ChangeValue(emptyMessage);
        else
        {
            const size_t offset = static_cast<size_t>(m_renderedPage - 1) * preview::PageBytes;
            const size_t end = std::min(offset + preview::PageBytes, bytes.size()) - 1;
            info = wxString::Format(Ui("%d / %d 页 · 0x%08llX–0x%08llX", "Page %d / %d · 0x%08llX–0x%08llX"),
                m_renderedPage, m_previewPage->GetMax(), static_cast<unsigned long long>(offset),
                static_cast<unsigned long long>(end));
        }
    }
    m_previewInfo->SetLabel(info);
    if (info.empty()) m_previewInfo->UnsetToolTip();
    else m_previewInfo->SetToolTip(info);
}

void cMain::ClearMatch()
{
    m_hasSearchResult = false;
    m_previewMatch->SetLabel(wxEmptyString);
}

void cMain::UpdateMatchText()
{
    if (!m_hasSearchResult) return;
    if (m_searchNext == 0) m_previewMatch->SetLabel(Ui("未找到", "No matches"));
    else m_previewMatch->SetLabel(wxString::Format(Ui("匹配偏移：0x%08llX", "Match at 0x%08llX"),
        static_cast<unsigned long long>(m_searchNext - 1)) + (m_searchWrapped ? Ui(" · 已从头查找", " · Wrapped") : wxString{}));
}

void cMain::FindInPreview(wxCommandEvent&)
{
    const auto& bytes = PreviewBytes();
    const wxString queryText = m_previewQuery->GetValue();
    const auto utf8 = queryText.utf8_str();
    const std::string query(utf8.data() ? utf8.data() : "", utf8.length());
    if (bytes.empty() || query.empty()) return;
    const bool strings = m_previewFormat->GetSelection() == StringsView;
    auto find = [&](size_t start) {
        return strings ? m_previewStrings->Find(query, start)
                       : preview::StringMatch{preview::NotFound, preview::Find(bytes, query, start)};
    };
    auto match = find(m_searchNext);
    bool wrapped = false;
    if (match.offset == preview::NotFound && m_searchNext != 0)
    {
        match = find(0);
        wrapped = match.offset != preview::NotFound;
    }
    if (match.offset == preview::NotFound) m_searchNext = 0;
    else
    {
        m_searchNext = match.offset + 1;
        if (strings) m_previewStrings->Reveal(static_cast<long>(match.row));
        else ShowHexAt(match.offset);
    }
    m_hasSearchResult = true;
    m_searchWrapped = wrapped;
    UpdateMatchText();
}

void cMain::ShowHexAt(size_t offset)
{
    const int page = static_cast<int>(offset / preview::PageBytes + 1);
    if (m_previewFormat->GetSelection() != HexView || m_renderedPage != page || m_previewPage->GetValue() != page)
    {
        // The pager's range can be stale when coming from the strings view.
        if (page > m_previewPage->GetMax()) m_previewPage->SetRange(1, page);
        m_previewPage->SetValue(page);
        m_previewFormat->SetSelection(HexView);
        RefreshPreview();
    }
    // RichEdit can defer line layout after SetValue. The hex text has fixed-width
    // ASCII rows, so jump by text position rather than querying visual geometry.
    const auto position = static_cast<long>(preview::HexByteTextPosition(offset % preview::PageBytes));
    m_previewText->ShowPosition(position);
    m_previewText->SetSelection(position, position + 2);
}
