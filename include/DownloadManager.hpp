#pragma once
#pragma message("Header: " __FILE__)

#include <string>
#include <vector>
#include "ManifestEntry.hpp" 

// wxWidgets must be first
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#   include <wx/wx.h>
#endif

// now wx headers are safe
#include <wx/string.h>
#include <wx/window.h>
#include <wx/thread.h>
#include <wx/event.h>
#include <wx/progdlg.h>

#include <vector>
#include <mutex>

#include "DownloadFileEntry.hpp"

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

    void SetManifest(const std::vector<DownloadFileEntry>& files);
    bool AllRequiredAvailable() const;
	bool AllFilesPresent() const;

	void OnDownloadProgress(wxCommandEvent& event);
	void DestroyProgressDialog();
    void StartBackgroundDownload(bool interactive);
    void Cancel();
	
	bool CurlDownload(wxString url, const wxString& dest);

private:
    friend class DownloadWorker;

    wxWindow* m_parent;
    wxString  m_dataDir;

    std::vector<DownloadFileEntry> m_files;
    DownloadWorker* m_worker = nullptr;

	wxGenericProgressDialog* m_progress = nullptr;


    mutable std::mutex m_mutex;
    bool m_cancelled = false;
};
