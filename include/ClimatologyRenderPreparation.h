#ifndef CLIMATOLOGY_RENDER_PREPARATION_H
#define CLIMATOLOGY_RENDER_PREPARATION_H

#include "ClimatologyDataset.h"
#include "ClimatologyState.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <map>
#include <vector>

namespace climatology {

struct RgbaPixel {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 0;
};

typedef RgbaPixel (*RasterColorFunction)(int field, double value);

struct TextureRasterRequest {
    DatasetField field = DatasetField::Wind;
    int color_field = 0;
    int month = 0;
    int width = 0;
    int height = 0;
    double samples_per_degree = 1.0;
    double latitude_offset = 0.0;
    double longitude_offset = 0.0;
};

struct TextureRasterResult {
    std::vector<unsigned char> pixels;
    bool completed = false;
    bool cancelled = false;
};

// CPU-only raster preparation.  OpenGL upload deliberately remains on
// OpenCPN's render thread; all interpolation and colour calculation runs here.
TextureRasterResult BuildTextureRaster(
    std::shared_ptr<const ClimatologyDatasetSnapshot> snapshot,
    const TextureRasterRequest &request, RasterColorFunction color,
    const std::atomic<bool> *cancel = 0);

typedef std::map<int, std::vector<const CycloneSegment*> > CycloneSpatialIndex;

struct CycloneIndexResult {
    CycloneSpatialIndex index;
    bool completed = false;
    bool cancelled = false;
};

CycloneIndexResult BuildCycloneIndex(
    std::shared_ptr<const ClimatologyDatasetSnapshot> snapshot,
    const CycloneFilterState &filters, const std::atomic<bool> *cancel = 0);

}  // namespace climatology

#endif
