#ifndef CLIMATOLOGY_DATA_SERVICE_H
#define CLIMATOLOGY_DATA_SERVICE_H

#include "ClimatologyDatasetLoader.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace climatology {

enum class DatasetLoadState {
    Idle,
    Loading,
    Ready,
    Failed,
    Cancelled
};

// Owns the only mutable part of dataset loading.  The worker constructs a
// complete private snapshot; consumers see it only after atomic publication.
class ClimatologyDataService {
public:
    ClimatologyDataService();
    ~ClimatologyDataService();

    ClimatologyDataService(const ClimatologyDataService&) = delete;
    ClimatologyDataService& operator=(const ClimatologyDataService&) = delete;

    bool Start(const std::string& data_directory);
    DatasetLoadState State() const;
    bool TakeCompletion(DatasetLoadResult& result);
    void CancelAndWait();

private:
    void Worker(std::string data_directory);

    mutable std::mutex m_mutex;
    std::thread m_worker;
    std::atomic<bool> m_cancel;
    DatasetLoadState m_state;
    DatasetLoadResult m_result;
    bool m_completionAvailable;
};

}  // namespace climatology

#endif
