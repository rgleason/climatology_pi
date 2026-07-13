//ClimatologyControlDialog.h (4‑tab full settings)

#pragma once

#include <wx/dialog.h>
#include <wx/notebook.h>

class ClimatologyOverlayFactory;
struct StandardDisplayParams;
struct WindAtlasParams;
struct CycloneParams;

class ClimatologyControlDialog : public wxDialog
{
public:
    ClimatologyControlDialog(wxWindow* parent,
                             ClimatologyOverlayFactory* factory);
    ~ClimatologyControlDialog() = default;

private:
    void InitUI();
    void BindEvents();

    void OnApply(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);

    // Builders
    StandardDisplayParams BuildStandardDisplayParams() const;
    WindAtlasParams       BuildWindAtlasParams() const;
    CycloneParams         BuildCycloneParams() const;

private:
    ClimatologyOverlayFactory* m_pFactory;
    wxNotebook*                 m_book;

    // Tab 1: Standard Displays
    // (arrays of controls for all overlays)
    // e.g. wxChoice* m_choiceUnits[NUM_OVERLAYS]; etc.
    // You can wire these exactly as in your existing 4‑tab UI.

    // Tab 2: Wind Atlas
    // controls for size, spacing, number size, center mode, calm/freq, auto scale, color, opacity, all-times, resolution.

    // Tab 3: Cyclones
    // controls for ENSO filters, basin toggles, state toggles, thresholds, day span, opacity, all-times.

    // Tab 4: About
    // simple static text.

    wxDECLARE_EVENT_TABLE();
};

