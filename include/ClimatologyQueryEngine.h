#ifndef CLIMATOLOGY_QUERY_ENGINE_H
#define CLIMATOLOGY_QUERY_ENGINE_H

#include "ClimatologyDataset.h"

#include <memory>

namespace climatology {

enum class QueryComponent { Eastward, Northward, Magnitude, Direction };

struct MonthPosition {
    int month = 0;
    int next_month = 1;
    double current_weight = 1.0;
};

struct WindDistributionResult {
    std::array<double, kWindSectors> frequency = {{0.0}};
    std::array<double, kWindSectors> speed_knots = {{0.0}};
    double calm_probability = 0.0;
    double gale_probability = 0.0;
};

class ClimatologyQueryEngine {
public:
    explicit ClimatologyQueryEngine(
        std::shared_ptr<const ClimatologyDatasetSnapshot> snapshot);

    bool Ready() const { return static_cast<bool>(m_snapshot); }
    std::shared_ptr<const ClimatologyDatasetSnapshot> Snapshot() const
    {
        return m_snapshot;
    }

    double ValueMonth(DatasetField field, QueryComponent component,
                      double latitude, double longitude, int month) const;
    double Value(DatasetField field, QueryComponent component,
                 double latitude, double longitude,
                 const MonthPosition& position) const;
    bool WindDistribution(double latitude, double longitude,
                          const MonthPosition& position,
                          WindDistributionResult& result) const;
    int CycloneTrackCrossings(double latitude0, double longitude0,
                              double latitude1, double longitude1,
                              int day_of_year, int day_range) const;

private:
    std::shared_ptr<const ClimatologyDatasetSnapshot> m_snapshot;
};

}  // namespace climatology

#endif
