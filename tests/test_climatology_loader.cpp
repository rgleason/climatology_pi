#include "ClimatologyDatasetLoader.h"
#include "ClimatologyQueryEngine.h"
#include "ClimatologyChecksum.h"
#include "ClimatologyDataService.h"
#include "ClimatologyRenderPreparation.h"
#include "zuFile.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>

using namespace climatology;

namespace {

void WriteBzipFixture(const char *path, const char *contents)
{
    FILE *output = std::fopen(path, "wb");
    assert(output);
    int error = BZ_OK;
    BZFILE *stream = BZ2_bzWriteOpen(&error, output, 9, 0, 30);
    assert(error == BZ_OK && stream);
    BZ2_bzWrite(&error, stream, const_cast<char *>(contents),
                static_cast<int>(std::strlen(contents)));
    assert(error == BZ_OK);
    unsigned int input_bytes = 0;
    unsigned int output_bytes = 0;
    BZ2_bzWriteClose(&error, stream, 0, &input_bytes, &output_bytes);
    assert(error == BZ_OK);
    assert(input_bytes == std::strlen(contents));
    assert(std::fclose(output) == 0);
}

void TestBzipBackwardSeek()
{
    char path[] = "/tmp/climatology-zu-XXXXXX";
    const int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    assert(close(descriptor) == 0);

    const char contents[] = "climatology bzip seek fixture";
    WriteBzipFixture(path, contents);

    ZUFILE *stream = zu_open(path, "rb", ZU_COMPRESS_BZIP);
    assert(stream);
    char buffer[64] = {};
    assert(zu_read(stream, buffer, 12) == 12);
    assert(zu_seek(stream, 0, SEEK_SET) == 0);
    assert(zu_read(stream, buffer, sizeof(contents) - 1) ==
           static_cast<int>(sizeof(contents) - 1));
    assert(std::memcmp(buffer, contents, sizeof(contents) - 1) == 0);
    assert(zu_close(stream) == 0);
    assert(unlink(path) == 0);
}

} // namespace

int main(int argc, char **argv)
{
    TestBzipBackwardSeek();

    const std::string data_directory =
        argc > 1 ? argv[1] : CLIMATOLOGY_TEST_DATA_DIR;

    assert(Sha256("", 0) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(Sha256("abc", 3) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    ClimatologyDatasetLoader loader(data_directory);
    const DatasetLoadResult loaded = loader.Load();
    assert(loaded.Succeeded());
    assert(loaded.snapshot->metadata.version == "ocpn-climatology-2026.1");
    assert(loaded.snapshot->metadata.manifest_validated);
    assert(loaded.snapshot->availability.cyclones);

    ClimatologyQueryEngine query(loaded.snapshot);
    MonthPosition january;
    january.month = 0;
    january.next_month = 0;
    january.current_weight = 1.0;

    const double trade_wind = query.Value(
        DatasetField::Wind, QueryComponent::Magnitude, 20.0, -30.0, january);
    assert(std::isfinite(trade_wind));
    assert(trade_wind > 5.0 && trade_wind < 25.0);

    WindDistributionResult atlas;
    assert(query.WindDistribution(20.0, -30.0, january, atlas));
    double frequency_sum = 0.0;
    for(std::size_t sector = 0; sector < kWindSectors; ++sector)
        frequency_sum += atlas.frequency[sector];
    // Legacy wind-atlas ABI exposes sector frequencies as fractions; the UI
    // multiplies these values by 100 only when drawing labels.
    assert(frequency_sum > 0.90 && frequency_sum < 1.10);

    const double pressure = query.Value(
        DatasetField::SeaLevelPressure, QueryComponent::Magnitude,
        50.0, -30.0, january);
    assert(pressure > 950.0 && pressure < 1050.0);

    const double dateline_left = query.Value(
        DatasetField::SeaSurfaceTemperature, QueryComponent::Magnitude,
        0.0, -180.0, january);
    const double dateline_right = query.Value(
        DatasetField::SeaSurfaceTemperature, QueryComponent::Magnitude,
        0.0, 180.0, january);
    assert(std::fabs(dateline_left - dateline_right) < 1e-6);

    CycloneFilterState cyclone_filters;
    cyclone_filters.minimum_wind_knots = 0;
    cyclone_filters.maximum_pressure_hpa = 2000;
    cyclone_filters.start_utc = 788918400;   // 1995-01-01 UTC
    cyclone_filters.end_utc = 1735689599;    // 2024-12-31 UTC
    const CycloneIndexResult cyclone_index =
        BuildCycloneIndex(loaded.snapshot, cyclone_filters);
    assert(cyclone_index.completed);
    assert(!cyclone_index.index.empty());

    ClimatologyDataService service;
    assert(service.Start(data_directory));
    assert(service.State() == DatasetLoadState::Loading ||
           service.State() == DatasetLoadState::Ready);
    DatasetLoadResult asynchronous;
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while(!service.TakeCompletion(asynchronous) &&
          std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(asynchronous.Succeeded());
    assert(service.State() == DatasetLoadState::Ready);

    // Repeated immediate cancellation must always join the worker and leave
    // the service reusable; this exercises the shutdown path used when the
    // plugin is disabled while OpenCPN remains live.
    for(int iteration = 0; iteration < 8; ++iteration) {
        ClimatologyDataService cancellable;
        assert(cancellable.Start(data_directory));
        cancellable.CancelAndWait();
        assert(cancellable.State() == DatasetLoadState::Cancelled ||
               cancellable.State() == DatasetLoadState::Ready);
        assert(cancellable.Start(data_directory));
        cancellable.CancelAndWait();
    }

    ClimatologyDataService invalid;
    assert(invalid.Start("/definitely/not/a/climatology/dataset"));
    DatasetLoadResult failed;
    const std::chrono::steady_clock::time_point failure_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(!invalid.TakeCompletion(failed) &&
          std::chrono::steady_clock::now() < failure_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(!failed.Succeeded());
    assert(!failed.errors.empty());
    assert(invalid.State() == DatasetLoadState::Failed);
    return 0;
}
