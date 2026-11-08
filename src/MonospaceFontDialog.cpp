#include "MonospaceFontDialog.h"
#include "SSHManager.h"
#include <wx/button.h>
#include <wx/panel.h>

wxBEGIN_EVENT_TABLE(MonospaceFontDialog, wxDialog)
    EVT_LISTBOX(wxID_ANY, MonospaceFontDialog::OnFontSelected)
    EVT_BUTTON(wxID_OK, MonospaceFontDialog::OnOK)
    EVT_BUTTON(wxID_CANCEL, MonospaceFontDialog::OnCancel)
wxEND_EVENT_TABLE()

MonospaceFontDialog::MonospaceFontDialog(wxWindow* parent, const wxString& currentFont)
    : wxDialog(parent, wxID_ANY, "Select Monospace Font", wxDefaultPosition, wxSize(500, 400)),
      m_selectedFont(currentFont) {

    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // Title
    wxStaticText* titleText = new wxStaticText(panel, wxID_ANY, "Select a monospace font:");
    mainSizer->Add(titleText, 0, wxALL, 10);

    // Font list box
    m_fontListBox = new wxListBox(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, {}, wxLB_SINGLE | wxLB_ALWAYS_SB);
    mainSizer->Add(m_fontListBox, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

    // Preview text
    wxStaticText* previewLabel = new wxStaticText(panel, wxID_ANY, "Preview:");
    mainSizer->Add(previewLabel, 0, wxALL, 10);

    m_previewText = new wxStaticText(panel, wxID_ANY,
        "The quick brown fox 0123456789\n"
        "中文混排测试:gypQW012 中文字体效果\n"
        "-> => != === ... ~~ 中文标点，。！？");
    mainSizer->Add(m_previewText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    // wxFontEnumerator doesn't work in this environment, use hardcoded list
    std::vector<wxString> commonMonospaceFonts = {
        "Courier New",
        "Menlo",
        "Monaco",
        "Consolas",
        "Andale Mono",
        "Lucida Console",
        "PT Mono",
        "Fira Code",
        "JetBrains Mono",
        "Source Code Pro",
        "Ubuntu Mono",
        "DejaVu Sans Mono",
        "Inconsolata",
        "Hack",
        "Roboto Mono",
        "Fira Mono",
        "Cousine",
        "IBM Plex Mono",
        "Space Mono",
        "Anonymous Pro",
        "Victor Mono",
        "Spline Sans Mono",
        "Input Mono",
        "iA Writer Mono",
        "Berkeley Mono",
        "Commit Mono",
        "Geist Mono",
        "Cascadia Mono",
        "Cascadia Code",
        "SF Mono",
        "Noto Sans Mono",
        "Noto Sans Mono CJK SC",
        // CJK-capable monospace fonts: keep Chinese glyphs in the same family
        "Sarasa Mono SC",
        "Sarasa Gothic SC",
        "Maple Mono",
        "Maple Mono SC",
        "Maple Mono NF CN",
        // Chinese fonts for Windows
        "Microsoft YaHei UI",
        "Microsoft YaHei",
        "SimHei",
        "SimSun",
        "FangSong",
        "KaiTi",
        "Noto Sans CJK SC"
    };

    // Test each font and add only those that exist
    for (const auto& fontName : commonMonospaceFonts) {
        wxFont testFont(12, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, fontName);
        if (testFont.IsOk()) {
            m_fontListBox->Append(fontName);
        }
    }

    std::cout << "Added " << m_fontListBox->GetCount() << " monospace fonts from hardcoded list" << std::endl;
    SSH_LOG("Added " << m_fontListBox->GetCount() << " monospace fonts from hardcoded list");

    // Select current font if it exists
    if (!currentFont.IsEmpty()) {
        int index = m_fontListBox->FindString(currentFont);
        if (index != wxNOT_FOUND) {
            m_fontListBox->SetSelection(index);
            wxFont previewFont(12, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, currentFont);
            m_previewText->SetFont(previewFont);
        }
    }

    // Buttons
    wxStdDialogButtonSizer* buttonSizer = new wxStdDialogButtonSizer();
    buttonSizer->AddButton(new wxButton(panel, wxID_OK, "OK"));
    buttonSizer->AddButton(new wxButton(panel, wxID_CANCEL, "Cancel"));
    buttonSizer->Realize();
    mainSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 10);

    panel->SetSizer(mainSizer);
    mainSizer->SetSizeHints(this);
}

MonospaceFontDialog::~MonospaceFontDialog() {
}

void MonospaceFontDialog::OnFontSelected(wxCommandEvent& event) {
    wxString selectedFont = m_fontListBox->GetStringSelection();
    if (!selectedFont.IsEmpty()) {
        wxFont previewFont(12, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, selectedFont);
        m_previewText->SetFont(previewFont);
        m_previewText->Refresh();
    }
}

void MonospaceFontDialog::OnOK(wxCommandEvent& event) {
    m_selectedFont = m_fontListBox->GetStringSelection();
    EndModal(wxID_OK);
}

void MonospaceFontDialog::OnCancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
}
