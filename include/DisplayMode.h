#ifndef DISPLAY_MODE_H
#define DISPLAY_MODE_H

// ============================================================================
// DisplayMode
// Unified‑grid scalar compositing modes for NOAA overlays.
// Used by:
//   • StandardDisplayParams (persistent default)
//   • ClimatologyRenderParams (per‑frame)
//   • ClimatologyOverlayFactory (compositing engine)
// ============================================================================

enum class DisplayMode {
    SingleScaled,     // Use one overlay, scaled to its own min/max
    WeightedBlend,    // Weighted multi‑overlay blend (global scaling)
    Additive,         // Sum of all overlays
    MaxComposite,     // Max value across overlays
    MinComposite,     // Min value across overlays
    MeanComposite,    // Mean across overlays
    MedianComposite   // Median across overlays
};

#endif // DISPLAY_MODE_H
