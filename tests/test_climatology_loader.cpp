#include "ClimatologyDatasetLoader.h"
#include "ClimatologyQueryEngine.h"
#include "ClimatologyChecksum.h"
#include "ClimatologyDataService.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <thread>

using namespace climatology;

int main()
{
    assert(Sha256("", 0) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(Sha256("abc", 3) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    ClimatologyDatasetLoader loader(CLIMATOLOGY_TEST_DATA_DIR);
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

    ClimatologyDataService service;
    assert(service.Start(CLIMATOLOGY_TEST_DATA_DIR));
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
    return 0;
}
