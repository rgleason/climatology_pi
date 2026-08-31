#include "ClimatologyDataService.h"

#include <exception>

namespace climatology {

ClimatologyDataService::ClimatologyDataService()
    : m_cancel(false), m_state(DatasetLoadState::Idle),
      m_completionAvailable(false)
{
}

ClimatologyDataService::~ClimatologyDataService()
{
    CancelAndWait();
}

bool ClimatologyDataService::Start(const std::string& data_directory)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_state == DatasetLoadState::Loading || m_worker.joinable())
        return false;
    m_cancel.store(false, std::memory_order_release);
    m_result = DatasetLoadResult();
    m_completionAvailable = false;
    m_state = DatasetLoadState::Loading;
    try {
        m_worker = std::thread(&ClimatologyDataService::Worker, this,
                               data_directory);
    } catch(...) {
        m_state = DatasetLoadState::Failed;
        m_result.errors.push_back("unable to start dataset loader thread");
        m_completionAvailable = true;
        return false;
    }
    return true;
}

DatasetLoadState ClimatologyDataService::State() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

bool ClimatologyDataService::TakeCompletion(DatasetLoadResult& result)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(!m_completionAvailable)
            return false;
        result = m_result;
        m_completionAvailable = false;
    }
    if(m_worker.joinable())
        m_worker.join();
    return true;
}

void ClimatologyDataService::CancelAndWait()
{
    m_cancel.store(true, std::memory_order_release);
    if(m_worker.joinable())
        m_worker.join();
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_state == DatasetLoadState::Loading)
        m_state = DatasetLoadState::Cancelled;
    m_completionAvailable = false;
}

void ClimatologyDataService::Worker(std::string data_directory)
{
    DatasetLoadResult result;
    try {
        result = ClimatologyDatasetLoader(data_directory).Load(&m_cancel);
    } catch(const std::exception& error) {
        result.errors.push_back(error.what());
    } catch(...) {
        result.errors.push_back("unknown dataset loader failure");
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_result = result;
    if(result.cancelled || m_cancel.load(std::memory_order_acquire))
        m_state = DatasetLoadState::Cancelled;
    else if(result.Succeeded())
        m_state = DatasetLoadState::Ready;
    else
        m_state = DatasetLoadState::Failed;
    m_completionAvailable = true;
}

}  // namespace climatology
