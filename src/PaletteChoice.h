#pragma once

#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/popupwin.h>
#include "UiColors.h"

// Themed popup list for PaletteChoice. A native wxMenu would always render
// in the OS light theme on wxMSW, so plain coloured text rows are used —
// they follow the app palette like every other custom widget here.
class PalettePopup : public wxPopupTransientWindow {
public:
    PalettePopup(wxWindow* parent, const wxArrayString& items, int selected,
                 const wxColour& bg, const wxColour& fg, const wxColour& hoverBg,
                 std::function<void(int)> onPick)
        : wxPopupTransientWindow(parent, wxBORDER_SIMPLE), m_pick(std::move(onPick)) {
        SetBackgroundColour(bg);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        const int rowH = GetCharHeight() + 8;
        for (size_t i = 0; i < items.size(); ++i) {
            const int idx = static_cast<int>(i);
            const wxColour& rowBg = (idx == selected) ? hoverBg : bg;
            wxStaticText* row = new wxStaticText(this, wxID_ANY, " " + items[i] + " ",
                                                 wxDefaultPosition, wxSize(-1, rowH));
            row->SetForegroundColour(fg);
            row->SetBackgroundColour(rowBg);
            row->Bind(wxEVT_LEFT_DOWN, [this, idx](wxMouseEvent&) {
                if (m_pick) m_pick(idx);
                Dismiss();
            });
            row->Bind(wxEVT_ENTER_WINDOW, [row, hoverBg](wxMouseEvent& e) {
                row->SetBackgroundColour(hoverBg);
                row->Refresh();
                e.Skip();
            });
            row->Bind(wxEVT_LEAVE_WINDOW, [row, rowBg](wxMouseEvent& e) {
                row->SetBackgroundColour(rowBg);
                row->Refresh();
                e.Skip();
            });
            sizer->Add(row, 0, wxEXPAND);
        }
        SetSizerAndFit(sizer);
    }

private:
    std::function<void(int)> m_pick;
};

// Dropdown-list lookalike implemented as an owner-drawn control + popup wxMenu.
// Native wxChoice, wxOwnerDrawnComboBox and wxButton all ignore
// background/foreground colours on wxMSW, so none of them can follow the app
// palette. This control paints itself, honours the standard colour setters
// and emits wxEVT_COMMAND_CHOICE_SELECTED on selection, so it is a drop-in
// replacement in forms that already handle wxEVT_CHOICE.
class PaletteChoice : public wxControl {
public:
    PaletteChoice(wxWindow* parent, wxWindowID id)
        : wxControl(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetCursor(wxCursor(wxCURSOR_HAND));
        Bind(wxEVT_PAINT, &PaletteChoice::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
            SetFocus();
            OnPopup();
        });
        Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e) {
            if (e.GetKeyCode() == WXK_SPACE || e.GetKeyCode() == WXK_RETURN) {
                OnPopup();
            } else {
                e.Skip();
            }
        });
    }

    void Append(const wxString& item) {
        m_choices.push_back(item);
        InvalidateBestSize();
    }

    int GetSelection() const { return m_selection; }

    void SetSelection(int sel) {
        if (sel < 0 || sel >= static_cast<int>(m_choices.size())) return;
        m_selection = sel;
        Refresh();
    }

    void SetStringSelection(const wxString& s) {
        const size_t idx = m_choices.Index(s);
        if (idx != wxNOT_FOUND) SetSelection(static_cast<int>(idx));
    }

    wxString GetValue() const {
        return m_selection >= 0 ? m_choices[m_selection] : wxString();
    }

protected:
    wxSize DoGetBestSize() const override {
        int w = 60;
        const int h = GetCharHeight();
        for (const auto& s : m_choices) {
            w = std::max(w, GetTextExtent(s).x + 8);
        }
        // Text width + arrow area + horizontal padding.
        return wxSize(w + h + 16, h + 10);
    }

private:
    void OnPaint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        const wxRect r = GetClientRect();

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(GetBackgroundColour()));
        dc.DrawRectangle(r);

        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(CurrentUiPalette().border));
        dc.DrawRectangle(r);

        dc.SetFont(GetFont());
        dc.SetTextForeground(GetForegroundColour());

        const int arrowW = GetCharHeight();
        const wxString label = m_selection >= 0 ? m_choices[m_selection] : wxString();
        dc.SetClippingRegion(wxRect(4, 0, r.width - arrowW - 8, r.height));
        dc.DrawText(label, 4, (r.height - dc.GetCharHeight()) / 2);
        dc.DestroyClippingRegion();

        // Downward triangle gives the control a dropdown affordance.
        const int cx = r.width - arrowW / 2 - 4;
        const int cy = r.height / 2;
        const int s = std::max(3, r.height / 8);
        const wxPoint pts[3] = {{cx - s, cy - s / 2}, {cx + s, cy - s / 2}, {cx, cy + s}};
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(GetForegroundColour()));
        dc.DrawPolygon(3, pts);
    }

    void OnPopup() {
        if (m_choices.empty()) return;
        const UiPalette pal = CurrentUiPalette();
        PalettePopup* popup = new PalettePopup(
            this, m_choices, m_selection,
            pal.bgContent, pal.textPrimary, pal.tabHover,
            [this](int i) { SelectFromMenu(i); });
        // Keep the popup at least as wide as the control itself.
        if (popup->GetSize().x < GetSize().x) {
            popup->SetSize(wxSize(GetSize().x, popup->GetSize().y));
        }
        popup->Position(ClientToScreen(wxPoint(0, 0)), GetSize());
        popup->Popup();
    }

    void SelectFromMenu(int i) {
        const bool changed = (i != m_selection);
        SetSelection(i);
        if (!changed) return;
        // Command event — propagates to parents like wxEVT_CHOICE.
        wxCommandEvent evt(wxEVT_COMMAND_CHOICE_SELECTED, GetId());
        evt.SetInt(i);
        evt.SetString(m_choices[i]);
        evt.SetEventObject(this);
        ProcessEvent(evt);
    }

    wxArrayString m_choices;
    int m_selection = -1;
};
