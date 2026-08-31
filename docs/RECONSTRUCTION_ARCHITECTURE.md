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
