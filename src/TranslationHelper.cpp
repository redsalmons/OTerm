#include "TranslationHelper.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/textfile.h>
#include <fstream>
#include <sstream>
#include "SSHManager.h"

std::map<wxString, wxString> TranslationHelper::s_translations;

void TranslationHelper::Load(const wxString& language) {
    s_translations.clear();
    
    wxStandardPaths& stdPaths = wxStandardPaths::Get();
    wxString exePath = stdPaths.GetExecutablePath();
    wxFileName fn(exePath);
    wxString localesDir = fn.GetPath() + wxFileName::GetPathSeparator() + "locales";
    
    wxString poFile = localesDir + wxFileName::GetPathSeparator() + language + ".po";
    LoadPOFile(poFile);
}

void TranslationHelper::LoadPOFile(const wxString& poFilePath) {
    wxCSConv conv(wxT("UTF-8"));
    wxTextFile file(poFilePath);
    if (!file.Exists()) {
        return;
    }
    
    if (!file.Open(conv)) {
        return;
    }
    
    wxString currentMsgid;
    wxString currentMsgstr;
    bool inMsgid = false;
    bool inMsgstr = false;
    
    for (size_t i = 0; i < file.GetLineCount(); i++) {
        wxString line = file.GetLine(i).Trim();
        
        if (line.StartsWith("msgid ")) {
            // Save previous translation if exists
            if (!currentMsgid.IsEmpty() && !currentMsgstr.IsEmpty()) {
                s_translations[currentMsgid] = currentMsgstr;
            }

            // Start new msgid
            currentMsgid = line.Mid(7).Trim();
            // Remove quotes - more robust method
            if (currentMsgid.StartsWith("\"")) {
                currentMsgid = currentMsgid.Mid(1);
            }
            if (currentMsgid.EndsWith("\"")) {
                currentMsgid = currentMsgid.Left(currentMsgid.Length() - 1);
            }
            currentMsgstr.Clear();
            inMsgid = true;
            inMsgstr = false;
        } else if (line.StartsWith("msgstr ")) {
            currentMsgstr = line.Mid(8).Trim();
            // Remove quotes - more robust method
            if (currentMsgstr.StartsWith("\"")) {
                currentMsgstr = currentMsgstr.Mid(1);
            }
            if (currentMsgstr.EndsWith("\"")) {
                currentMsgstr = currentMsgstr.Left(currentMsgstr.Length() - 1);
            }
            inMsgid = false;
            inMsgstr = true;
        } else if (line.StartsWith("\"") && line.EndsWith("\"")) {
            wxString content = line.Mid(1, line.Length() - 2);
            if (inMsgid) {
                currentMsgid += content;
            } else if (inMsgstr) {
                currentMsgstr += content;
            }
        } else if (line.IsEmpty()) {
            // Save translation at end of entry
            if (!currentMsgid.IsEmpty() && !currentMsgstr.IsEmpty()) {
                s_translations[currentMsgid] = currentMsgstr;
            }
            currentMsgid.Clear();
            currentMsgstr.Clear();
            inMsgid = false;
            inMsgstr = false;
        }
    }
    
    // Save last translation
    if (!currentMsgid.IsEmpty() && !currentMsgstr.IsEmpty()) {
        s_translations[currentMsgid] = currentMsgstr;
    }
    
    file.Close();
}

wxString TranslationHelper::Translate(const wxString& text) {
    auto it = s_translations.find(text);
    if (it != s_translations.end()) {
        return it->second;
    }
    return text;
}

wxString TranslationHelper::Tr(const wxString& text) {
    return Translate(text);
}
