# Reconstruction architecture

The historical `ClimatologyOverlayFactory` remains temporarily as a
compatibility facade while responsibilities move behind explicit boundaries:

```text
ClimatologyDatasetLoader -> immutable ClimatologyDatasetSnapshot
                                      |
                                      +-> ClimatologyQueryEngine
                                      |      +-> legacy xWeatherRouting ABI
                                      |      +-> typed Provider API V1
                                      |      +-> cursor readouts
                                      |
ClimatologyState -> RenderRequest -> ClimatologyRenderEngine -> GL/DC
       ^                  ^
       |                  |
    dialogs          OpenCPN viewport
```

Each scalar or vector field owns its native geometry and month availability.
Wind distributions and cyclone tracks are distinct typed collections rather
than artificial raster layers.

Loading and validation run away from the GUI thread.  Rendering and OpenGL
resource management remain on OpenCPN's render thread.  Query callbacks copy a
`shared_ptr<const ClimatologyDatasetSnapshot>` and use no mutable UI state.

The final Phase 10 facade owns the asynchronous loader, published snapshot,
query engine, render engine and typed state.  It does not own controls and it
does not expose storage to dialogs.

## Phase 8 state boundary

The dialog captures one `ClimatologyRenderState` at the start of a render.
The renderer consumes that typed value for field visibility, unit selection,
overlay options, arrows, wind-atlas options and cyclone filters; it does not
read individual controls while drawing.  Unit calibration is a pure captured
value, so an isobar worker never consults mutable settings.

Dataset availability and performance warnings travel in the other direction
through explicit dialog-facade methods on the main thread.  Worker code does
not enable controls or display dialogs.

## Phase 9 render preparation

Potentially expensive preparation has a strict thread boundary:

```text
dataset snapshot --+--> texture raster worker ----> RGBA buffer
                   |                                  |
                   |                                  +--> GL upload/render thread
                   +--> cyclone index worker ------> atomic cache publication
                   +--> isobar worker -------------> completed contour map
```

Only short publication and OpenGL resource operations remain on the OpenCPN
thread.  Jobs are cancellable, joined during plugin shutdown, and published
only after a release/acquire completion handoff.  A lightweight main-thread
timer consumes completions and asks OpenCPN to repaint.  Cyclone cache readers
used by both rendering and Weather Routing are read-only; replacement obtains
all cache permits and swaps a fully built index in one operation.

`BuildTextureRaster` and `BuildCycloneIndex` are wx-free pure components.  The
headless suite verifies normal, invalid and mid-operation cancellation paths,
the complete 2026.1 dataset, cyclone filtering/indexing, and repeated loader
cancel/restart lifecycle behavior.
