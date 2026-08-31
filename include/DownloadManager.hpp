#pragma once
#pragma message("Header: " __FILE__)

#include <string>
#include <vector>
#include <mutex>

#include "ManifestEntry.hpp"

// wxWidgets must be first
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#   include <wx/wx.h>
#endif

#include <wx/string.h>
#include <wx/window.h>
#include <wx/thread.h>
#include <wx/event.h>
#include <wx/progdlg.h>

// ------------------------------------------------------------
// Events
// ------------------------------------------------------------
wxDECLARE_EVENT(EVT_DM_PROGRESS, wxCommandEvent);
wxDECLARE_EVENT(EVT_DM_COMPLETE, wxCommandEvent);

class DownloadWorker;

// ------------------------------------------------------------
// DownloadManager
// ------------------------------------------------------------
class DownloadManager
{
public:
    DownloadManager(wxWindow* parent, const wxString& dataDir);
    ~DownloadManager();

    void SetManifest(const std::vector<ManifestEntry>& files);
    const std::vector<ManifestEntry>& GetManifest() const;

    void StartBackgroundDownload(bool interactive);
    bool AllFilesPresent() const;
    bool CheckIntegrity();
    bool IsBusy() const { return m_workerThreadRunning; }

    void Cancel();
    void WorkerThreadFinished();
    bool AllRequiredAvailable() const;
	
	bool CurlDownload(wxString url, const wxString& dest);
    bool CurlDownload(const ManifestEntry& entry,
                      wxString url,
                      const wxString& dest);

    void OnDownloadProgress(wxCommandEvent& event);
    void DestroyProgressDialog();

private:
    friend class DownloadWorker;

    wxWindow* m_parent;
    wxString  m_dataDir;

    std::vector<ManifestEntry> m_files;
    std::vector<ManifestEntry> m_fullManifest;

    mutable std::mutex m_mutex;
    bool m_cancelled = false;
    bool m_workerThreadRunning = false;

    DownloadWorker* m_worker = nullptr;
    wxGenericProgressDialog* m_progress = nullptr;
};
