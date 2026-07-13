#ifndef OVERLAY_TYPES_H
#define OVERLAY_TYPES_H

// enum defines the canonical list of overlay types:
// Purpose It provides the indexing scheme for:
// GL texture arrays, per-overlay settings arrays, dataset arrays
// dialog controls,  color maps, unit sets
// How it is used  Every major array in the factory uses NUM_OVERLAYS:
// This enum is the “overlay index backbone” of the entire plugin.
// OverlayType.h defines UI facing overlay IDs

enum OverlayType {
    OVERLAY_WIND=0,
    OVERLAY_WIND_ATLAS,
    OVERLAY_CYCLONES,
    OVERLAY_CURRENT,
    OVERLAY_PRESSURE,
    OVERLAY_SEA_TEMP,
    OVERLAY_AIR_TEMP,
    OVERLAY_CLOUD,
    OVERLAY_PRECIP,
    OVERLAY_RH,
    OVERLAY_LIGHTNING,
    OVERLAY_SEA_DEPTH,
    NUM_OVERLAYS
};

#endif // OVERLAY_TYPES_H
