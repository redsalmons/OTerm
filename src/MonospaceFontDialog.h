#ifndef MONOSPACEFONTDIALOG_H
#define MONOSPACEFONTDIALOG_H

#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <wx/fontenum.h>
#include <wx/font.h>

class MonospaceFontEnumerator : public wxFontEnumerator {
public:
    std::vector<wxString> m_monospacedFonts;
    std::vector<wxString> m_allFonts;

    virtual bool OnFontFace(const wxString& font) {
        m_allFonts.push_back(font);
        wxFont testFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, font);
        if (testFont.IsOk() && testFont.IsFixedWidth()) {
            m_monospacedFonts.push_back(font);
        }
        return true;
    }
};

class MonospaceFontDialog : public wxDialog {
public:
    MonospaceFontDialog(wxWindow* parent, const wxString& currentFont = wxEmptyString);
    virtual ~MonospaceFontDialog();

    wxString GetSelectedFont() const { return m_selectedFont; }

private:
    wxListBox* m_fontListBox;
    wxStaticText* m_previewText;
    wxString m_selectedFont;

    void OnFontSelected(wxCommandEvent& event);
    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);

    wxDECLARE_EVENT_TABLE();
};

#endif
