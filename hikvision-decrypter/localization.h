#pragma once

#include <wx/config.h>
#include <wx/intl.h>

namespace localization
{
    enum class Preference { System, Chinese, English };
    enum class Language { Chinese, English };

    struct Message
    {
        const char* chinese = "";
        const char* english = "";
    };

    inline wxString Text(Language language, const char* chinese, const char* english)
    {
        return wxString::FromUTF8(language == Language::Chinese ? chinese : english);
    }

    inline Preference ReadPreference()
    {
        wxConfig config("HikvisionDecrypter");
        const auto stored = config.Read("Language", "system");
        if (stored == "zh-CN") return Preference::Chinese;
        if (stored == "en") return Preference::English;
        return Preference::System;
    }

    inline bool WritePreference(Preference preference)
    {
        wxConfig config("HikvisionDecrypter");
        const char* stored = preference == Preference::Chinese ? "zh-CN"
            : preference == Preference::English ? "en" : "system";
        return config.Write("Language", stored) && config.Flush();
    }

    inline Language Resolve(Preference preference)
    {
        if (preference == Preference::Chinese) return Language::Chinese;
        if (preference == Preference::English) return Language::English;
        const auto system = wxLocale::GetLanguageCanonicalName(wxLocale::GetSystemLanguage());
        return system == "zh" || system.StartsWith("zh_") || system.StartsWith("zh-")
            ? Language::Chinese : Language::English;
    }
}
