#ifndef CLIMATOLOGY_TOUCH_H
#define CLIMATOLOGY_TOUCH_H

#ifdef __OCPN__ANDROID__
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/control.h>
#include <wx/scrolwin.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <QtWidgets/QWidget>

// Qt styles are applied after wxFormBuilder has cached desktop-sized controls.
// Recalculate their text metrics and reserve a usable touch target afterwards.
inline void ClimatologyTouchControls(wxWindow* window, const wxFont& font) {
    window->SetFont(font);
    for (wxWindowList::iterator it = window->GetChildren().begin();
         it != window->GetChildren().end(); ++it)
        ClimatologyTouchControls(*it, font);
    window->GetHandle()->ensurePolished();
    const QSize hint = window->GetHandle()->sizeHint();
    window->InvalidateBestSize();
    if (wxCheckBox* checkbox = dynamic_cast<wxCheckBox*>(window)) {
        checkbox->SetMinSize(wxSize(wxMax(80, hint.width()), wxMax(48, hint.height())));
    } else if (dynamic_cast<wxChoice*>(window)) {
        window->SetMinSize(wxSize(wxMax(150, hint.width()), wxMax(48, hint.height())));
    } else if (dynamic_cast<wxSpinCtrl*>(window)) {
        window->SetMinSize(wxSize(100, 48));
    } else if (dynamic_cast<wxSlider*>(window)) {
        window->SetMinSize(wxSize(180, 48));
    } else if (dynamic_cast<wxButton*>(window)) {
        window->SetMinSize(wxSize(wxMax(80, hint.width()), wxMax(48, hint.height())));
    } else if (dynamic_cast<wxTextCtrl*>(window)) {
        window->SetMinSize(wxSize(120, 48));
    }
    if (wxScrolledWindow* scrolled = dynamic_cast<wxScrolledWindow*>(window)) {
        scrolled->SetScrollRate(12, 12);
        scrolled->FitInside();
    }
    window->Layout();
}

inline wxFont ClimatologyTouchFont() {
    wxFont font = GetOCPNGUIScaledFont_PlugIn(_("Dialog"));
    if (!font.IsOk()) font = *wxNORMAL_FONT;
    font.SetPointSize(wxMax(14, font.GetPointSize()));
    return font;
}
#endif
#endif
