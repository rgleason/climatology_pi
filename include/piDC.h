#pragma once

#include <wx/dc.h>
#include <wx/colour.h>

// ---------------------------------------------------------------------------
// piDC: Minimal compatibility wrapper used by legacy OpenCPN plugins.
// API120 does not provide ocpnDC or PlugInDC, so plugins define their own
// lightweight wrapper around wxDC to preserve older drawing interfaces.
// ---------------------------------------------------------------------------

class piDC
{
public:
    piDC(wxDC* dc) : m_dc(dc) {}

    wxDC* GetDC() const { return m_dc; }

    // Basic drawing primitives used by IsoBarMap, ClimatologyOverlayFactory, etc.
     // Basic drawing primitives used by climatology_pi and isobars


    void DrawLine(int x1, int y1, int x2, int y2) {
        if (m_dc) m_dc->DrawLine(x1, y1, x2, y2);
    }

    void DrawCircle(int x, int y, int radius) {
        if (m_dc) m_dc->DrawCircle(x, y, radius);
    }

    void DrawRectangle(int x, int y, int w, int h) {
        if (m_dc) m_dc->DrawRectangle(x, y, w, h);
    }

    void DrawText(const wxString& text, int x, int y) {
        if (m_dc) m_dc->DrawText(text, x, y);
    }
	
    void SetPen(const wxPen& pen)
    {
        if (m_dc) m_dc->SetPen(pen);
    }

    void SetBrush(const wxBrush& brush) {
        if (m_dc) m_dc->SetBrush(brush);
    }

private:
    wxDC* m_dc;   // raw pointer is correct: plugin does not own the DC
};
