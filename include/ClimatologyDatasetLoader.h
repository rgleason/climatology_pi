#ifndef CLIMATOLOGY_DATASET_LOADER_H
#define CLIMATOLOGY_DATASET_LOADER_H

#include "ClimatologyDataset.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace climatology {

struct DatasetLoadResult {
    std::shared_ptr<const ClimatologyDatasetSnapshot> snapshot;
    std::vector<std::string> errors;
    bool cancelled = false;

    bool Succeeded() const { return snapshot && errors.empty() && !cancelled; }
};

class ClimatologyDatasetLoader {
public:
    explicit ClimatologyDatasetLoader(const std::string& data_directory);

    DatasetLoadResult Load(const std::atomic<bool>* cancel = 0) const;

private:
    std::string m_dataDirectory;
};

}  // namespace climatology

#endif
