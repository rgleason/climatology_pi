#include "ClimatologyDataset.h"
#include "ClimatologyQueryEngine.h"

#include <cassert>
#include <cmath>
#include <memory>

using namespace climatology;

static GridGeometry Geometry()
{
    GridGeometry geometry;
    geometry.rows = 2;
    geometry.columns = 4;
    geometry.latitude_origin = 45.0;
    geometry.longitude_origin = 0.0;
    geometry.latitude_step = 90.0;
    geometry.longitude_step = 90.0;
    geometry.latitude_decreases = true;
    return geometry;
}

int main()
{
    std::shared_ptr<ClimatologyDatasetSnapshot> mutable_snapshot(
        new ClimatologyDatasetSnapshot);
    MonthlyScalarField* sst =
        mutable_snapshot->MutableScalar(DatasetField::SeaSurfaceTemperature);
    assert(sst);
    sst->geometry = Geometry();
    sst->available[0] = true;
    sst->available[1] = true;
    sst->values[0].assign(8, 10.0f);
    sst->values[1].assign(8, 20.0f);
    mutable_snapshot->availability.fields[
        static_cast<std::size_t>(DatasetField::SeaSurfaceTemperature)] = true;

    assert(mutable_snapshot->IsConsistent());
    std::shared_ptr<const ClimatologyDatasetSnapshot> snapshot = mutable_snapshot;
    ClimatologyQueryEngine query(snapshot);

    MonthPosition middle;
    middle.month = 0;
    middle.next_month = 1;
    middle.current_weight = 0.5;
    assert(std::fabs(query.Value(DatasetField::SeaSurfaceTemperature,
                                 QueryComponent::Magnitude, 0.0, 359.0,
                                 middle) - 15.0) < 1e-9);

    // Missing values remain missing unless a valid neighbour can supply the
    // interpolation, matching the modernisation contract.
    mutable_snapshot.reset();
    assert(query.Ready());
    assert(std::isnan(query.ValueMonth(DatasetField::Current,
                                       QueryComponent::Magnitude,
                                       0.0, 0.0, 0)));
    return 0;
}
