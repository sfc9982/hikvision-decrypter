#include "cMain.h"
#include "hikvision.h"
#include "preview.h"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

class PreviewTestApp : public wxApp
{
public:
    bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN(PreviewTestApp);

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    template<typename T>
    T* Control(cMain& frame, const char* name)
    {
        auto* control = dynamic_cast<T*>(wxWindow::FindWindowByName(name, &frame));
        Require(control != nullptr, "required preview control missing");
        return control;
    }

    void Select(wxChoice* choice, int selection)
    {
        choice->SetSelection(selection);
        wxCommandEvent event(wxEVT_CHOICE, choice->GetId());
        event.SetEventObject(choice);
        choice->GetEventHandler()->ProcessEvent(event);
    }

    void Click(wxButton* button)
    {
        wxCommandEvent event(wxEVT_BUTTON, button->GetId());
        event.SetEventObject(button);
        button->GetEventHandler()->ProcessEvent(event);
    }
}

int main(int argc, char** argv)
{
    const bool chinese = argc == 2 && wxString::FromUTF8(argv[1]) == "--zh";
    if (!wxEntryStart(argc, argv)) return 2;
    if (!wxTheApp->CallOnInit()) { wxEntryCleanup(); return 2; }
    int result = 0;
    cMain* frame = nullptr;
    try
    {
        frame = new cMain(chinese ? localization::Preference::Chinese : localization::Preference::English);
        auto expected = [chinese](const char* zh, const char* en) { return wxString::FromUTF8(chinese ? zh : en); };
        Require(frame->GetTitle() == expected("Hikvision 配置工具", "Hikvision Configuration Tool"), "window title not localized");
        auto* menus = frame->GetMenuBar();
        const int languageMenuIndex = menus->FindMenu(expected("语言", "Language"));
        Require(languageMenuIndex != wxNOT_FOUND, "language menu missing");
        auto* languages = menus->GetMenu(static_cast<size_t>(languageMenuIndex));
        const int currentLanguage = languages->FindItem(expected("简体中文", "English"));
        Require(currentLanguage != wxNOT_FOUND && languages->IsChecked(currentLanguage), "language preference not checked");
        auto* source = Control<wxChoice>(*frame, "preview-source");
        auto* format = Control<wxChoice>(*frame, "preview-format");
        auto* page = Control<wxSpinCtrl>(*frame, "preview-page");
        auto* text = Control<wxTextCtrl>(*frame, "preview-content");
        auto* strings = Control<wxListCtrl>(*frame, "preview-strings");
        auto* query = Control<wxTextCtrl>(*frame, "preview-query");
        auto* find = Control<wxButton>(*frame, "preview-find");
        Require(find->GetLabel() == expected("查找下一个", "Find Next")
                    && format->GetString(2) == expected("可读字符串", "Strings")
                    && text->GetValue() == expected("未打开文件", "No file open"), "preview controls not localized");
        wxListItem heading;
        heading.SetMask(wxLIST_MASK_TEXT);
        Require(strings->GetColumn(0, heading) && heading.GetText() == expected("文件偏移", "File offset"),
                "strings column not localized");
        Require(!text->IsEditable() && !page->IsEnabled() && !find->IsEnabled(), "empty state incorrect");
        std::cout << "PASS empty preview is read-only and navigation is disabled\n";

        // At the smallest supported size the inspector and its actions must fit
        // beside the conversion controls; resizing should grow the data view.
        auto* convert = Control<wxButton>(*frame, "convert-command");
        auto resize = [&](const wxSize& size) {
            frame->SetClientSize(size);
            frame->Layout();
            text->GetParent()->Layout();
        };
        resize(frame->GetMinClientSize());
        const wxRect compact = text->GetRect();
        Require(convert->GetRect().GetRight() < compact.GetLeft(), "tools are not beside preview");
        Require(text->GetParent()->GetClientRect().Contains(find->GetRect()), "search clipped at minimum size");
        resize(frame->GetClientSize() + frame->FromDIP(wxSize(160, 120)));
        Require(text->GetSize().x > compact.width && text->GetSize().y > compact.height,
                "preview does not expand with the window");
        std::cout << "PASS compact two-pane layout and expanding preview\n";
        std::vector<unsigned char> input(8209, 0);
        const std::string needle = "preview_test";
        const std::string crossing = "crosses_page";
        std::copy(crossing.begin(), crossing.end(), input.begin() + 4093);
        std::copy(needle.begin(), needle.end(), input.begin() + 5000);
        std::copy(needle.begin(), needle.end(), input.begin() + 8192);
        wxFileName executable(wxString::FromUTF8(argv[0]));
        executable.MakeAbsolute();
        const wxString path = executable.GetPathWithSep() + (chinese ? "preview-smoke-zh.bin" : "preview-smoke-en.bin");
        wxFile fixture(path, wxFile::write);
        Require(fixture.IsOpened() && fixture.Write(input.data(), input.size()) == input.size() && fixture.Close(),
                "could not write synthetic fixture");
        Require(frame->LoadFile(path), "could not load synthetic fixture");
        resize(frame->GetMinClientSize());
        Require(text->GetParent()->GetClientRect().Contains(find->GetRect()), "validation pushed preview outside the minimum window");
        Require(page->GetMax() == 3 && page->GetValue() == 1 && text->GetValue().StartsWith("00000000"),
                "initial file preview incorrect");
        Click(Control<wxButton>(*frame, "preview-next"));
        Require(page->GetValue() == 2 && text->GetValue().StartsWith("00001000"), "next page incorrect");
        Select(format, 2);
        Require(strings->IsShown() && !text->IsShown() && !page->IsShown()
                    && !Control<wxButton>(*frame, "preview-next")->IsShown(), "strings still show page controls");
        Require(strings->GetItemCount() == 3, "strings list does not span the whole file");
        Require(strings->GetItemText(0, 0) == "0x00000FFD" && strings->GetItemText(0, 1) == "12"
                    && strings->GetItemText(0, 2) == "crosses_page", "cross-page row incorrect");
        query->SetValue("preview_test");
        Click(find);
        Require(format->GetSelection() == 2 && strings->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == 1, "list search did not select matching row");
        Click(find);
        Require(strings->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == 2, "list search did not find next row");
        Click(find);
        Require(strings->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == 1, "list search did not wrap");

        const auto otherLanguage = chinese ? localization::Preference::English : localization::Preference::Chinese;
        const auto originalLanguage = chinese ? localization::Preference::Chinese : localization::Preference::English;
        frame->SetLanguage(otherLanguage);
        Require(menus->FindMenu(expected("Language", "语言")) == languageMenuIndex &&
                    find->GetLabel() == expected("Find Next", "查找下一个"), "live menu or button translation failed");
        Require(strings->GetColumn(0, heading) && heading.GetText() == expected("File offset", "文件偏移"),
                "live strings column translation failed");
        Require(format->GetSelection() == 2 && source->GetSelection() == 0 && strings->GetItemCount() == 3 &&
                    strings->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == 1 &&
                    query->GetValue() == "preview_test", "switching languages reset the strings view");
        Click(find);
        Require(strings->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == 2,
                "switching languages reset the search position");
        frame->SetLanguage(originalLanguage);

        wxListEvent activate(wxEVT_LIST_ITEM_ACTIVATED, strings->GetId());
        activate.SetIndex(2);
        activate.SetEventObject(strings);
        strings->GetEventHandler()->ProcessEvent(activate);
        Require(page->IsShown() && text->IsShown() && !strings->IsShown()
                    && page->GetValue() == 3 && format->GetSelection() == 0, "row activation did not navigate to hex");
        Require(text->GetStringSelection() == "70", "activated byte not highlighted");
        query->SetValue("crosses_page");
        Click(find);
        Require(page->GetValue() == 1 && text->GetStringSelection() == "63", "raw cross-page search failed");
        Click(find);
        Require(page->GetValue() == 1 && text->GetStringSelection() == "63", "same-page search wrap lost highlight");
        wxFocusEvent leavePager(wxEVT_KILL_FOCUS, page->GetId());
        leavePager.SetEventObject(page);
        page->GetEventHandler()->ProcessEvent(leavePager);
        Require(text->GetStringSelection() == "63", "unchanged pager focus reset the preview");
        Select(format, 2);
        Select(source, 1);
        Require(strings->GetItemCount() == 0 && !find->IsEnabled(), "missing-result string list state incorrect");
        Select(source, 0);
        Require(strings->GetItemCount() == 3 && find->IsEnabled(), "input string list did not reload");
        Select(format, 0);
        Require(page->GetValue() == 1 && page->IsEnabled(), "source switch did not reset page");
        std::cout << "PASS whole-file virtual strings, search and hex navigation\n";

        wxString encryptedPath;
        const bool suppliedSample = argc == 3;
        if (suppliedSample)
            encryptedPath = wxString::FromUTF8(argv[1]);
        else
        {
            input.resize((input.size() + 15) / 16 * 16);
            const auto ciphertext = transformConfiguration(input, ConversionMode::Encrypt);
            encryptedPath = path; // Reuse the temporary synthetic fixture.
            wxFile encrypted(encryptedPath, wxFile::write);
            Require(encrypted.IsOpened() && encrypted.Write(ciphertext.data(), ciphertext.size()) == ciphertext.size()
                        && encrypted.Close(), "could not write encrypted synthetic fixture");
        }
        Require(frame->LoadFile(encryptedPath), "encrypted fixture did not load");
        Require(convert->IsEnabled(), "aligned input did not enable conversion");
        Click(convert);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (source->GetSelection() != 1 && std::chrono::steady_clock::now() < deadline)
        {
            wxTheApp->ProcessPendingEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Require(source->GetSelection() == 1 && format->GetSelection() == 2 && strings->IsShown(),
                "conversion did not open the result string list");
        Require(strings->GetItemCount() > 0, "result string list empty");
        if (!suppliedSample)
            Require(strings->GetItemCount() == 3 && strings->GetItemText(0, 2) == "crosses_page",
                    "result string list did not reindex the new data");
        std::cout << "PASS conversion opens the populated result string list\n";

        frame->SetLanguage(otherLanguage);
        Require(source->GetSelection() == 1 && strings->IsShown() && strings->GetItemCount() > 0,
                "switching languages cleared the unsaved result");
        frame->SetLanguage(originalLanguage);

        if (suppliedSample)
        {
            const wxString decodedPath = wxString::FromUTF8(argv[2]);
            wxFile encrypted(encryptedPath), decoded(decodedPath);
            Require(encrypted.IsOpened() && decoded.IsOpened(), "sample verification files did not open");
            std::vector<unsigned char> ciphertext(static_cast<size_t>(encrypted.Length()));
            std::vector<unsigned char> plaintext(static_cast<size_t>(decoded.Length()));
            Require(encrypted.Read(ciphertext.data(), ciphertext.size()) == static_cast<wxFileOffset>(ciphertext.size()), "sample input read incomplete");
            Require(decoded.Read(plaintext.data(), plaintext.size()) == static_cast<wxFileOffset>(plaintext.size()), "sample reference read incomplete");
            const auto converted = transformConfiguration(ciphertext, ConversionMode::Decrypt);
            Require(converted == plaintext, "sample decrypted output differs from supplied reference");
            std::cout << "PASS decrypted bytes match supplied reference; no sample contents written to logs\n";
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        result = 1;
    }
    delete frame;
    wxTheApp->OnExit();
    wxEntryCleanup();
    return result;
}
