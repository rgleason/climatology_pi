#pragma message("CLIMATOLOGY_BUNDLED_CURL is defined")

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// Always compiled:
#include "DownloadManager.hpp"
#include "DownloadFileEntry.hpp"
#include "icons.h"

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/zstream.h>
#include <wx/wfstream.h>
#include <wx/progdlg.h>

#ifndef wxPD_APP_MODAL
#define wxPD_APP_MODAL 0
#endif

#ifndef wxPD_AUTO_HIDE
#define wxPD_AUTO_HIDE 0
#endif


// ------------------------------------------------------------
// Event definitions
// ------------------------------------------------------------
wxDEFINE_EVENT(EVT_DM_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_DM_COMPLETE, wxCommandEvent);

// ------------------------------------------------------------
// Worker thread class
// ------------------------------------------------------------
class DownloadWorker : public wxThread
{
public:
    DownloadWorker(DownloadManager* mgr, bool interactive)
        : wxThread(wxTHREAD_DETACHED),
          m_mgr(mgr),
          m_interactive(interactive)
    {}

protected:
    virtual ExitCode Entry() override;

private:
    DownloadManager* m_mgr;
    bool m_interactive;
};

// ------------------------------------------------------------
// DownloadManager
// ------------------------------------------------------------
DownloadManager::DownloadManager(wxWindow* parent, const wxString& dataDir)
    : m_parent(parent),
      m_dataDir(dataDir)
{
	wxLogMessage("Climatology: DownloadManager using dataDir = '%s'", m_dataDir);
    wxFileName::Mkdir(m_dataDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}

DownloadManager::~DownloadManager()
{
    Cancel();
}

void DownloadManager::SetManifest(const std::vector<DownloadFileEntry>& files)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_files = files;
}

bool DownloadManager::AllFilesPresent() const
{
    for (const auto &entry : m_files) {
        wxFileName fn(m_dataDir + "/" + entry.filename);
        if (!fn.FileExists())
            return false;
    }
    return true;
}


bool DownloadManager::AllRequiredAvailable() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& f : m_files)
    {
        wxString path = m_dataDir + f.filename;

        if (!wxFileExists(path)) {
            wxLogMessage("Climatology: Missing file: %s", path);
            return false;
        }

        if (wxFileName::GetSize(path) == 0) {
            wxLogMessage("Climatology: Zero-size file: %s", path);
            return false;
        }
    }

    wxLogMessage("Climatology: AllRequiredAvailable() = true");
    return true;
}



void DownloadManager::OnDownloadProgress(wxCommandEvent& event)
{
    if (!m_progress)
        return;

    int pct = event.GetInt();
    m_progress->Update(pct);
}

void DownloadManager::DestroyProgressDialog()
{
    if (m_progress) {
        m_progress->Destroy();
        m_progress = nullptr;
    }

    m_parent->Unbind(EVT_DM_PROGRESS,
                     &DownloadManager::OnDownloadProgress,
                     this);
}


void DownloadManager::Cancel()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cancelled = true;
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    FILE* fp = (FILE*)userp;
    return fwrite(contents, size, nmemb, fp);
}

// Curl download begins here
// Only curl‑dependent parts under the macro:
#ifdef CLIMATOLOGY_BUNDLED_CURL

#include <curl/curl.h>

bool DownloadManager::CurlDownload(wxString url, const wxString& dest)
{
    wxLogMessage("curl version: %s", curl_version());
    wxLogMessage("Climatology: curl downloading %s -> %s", url, dest);

    url.Trim(true).Trim(false);
    url.Replace("\r", "");
    url.Replace("\n", "");

    if (url.IsEmpty() || !url.StartsWith("http")) {
        wxLogMessage("Climatology: Invalid URL after sanitizing: '%s'", url);
        return false;
    }

    FILE* fp = fopen(dest.mb_str().data(), "wb");
    if (!fp) {
        wxLogMessage("Climatology: Failed to open %s for writing", dest);
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        wxLogMessage("Climatology: curl_easy_init failed");
        return false;
    }

    // LOG CAINFO PATH
	wxString ca = m_dataDir + "cacert.pem";

    std::string ca_utf8 = ca.ToStdString();
    wxLogMessage("Climatology: Using CAINFO = %s", ca_utf8.c_str());
	wxLogMessage("Climatology: CA file exists? %d", wxFileExists(ca));
    curl_easy_setopt(curl, CURLOPT_CAINFO, ca_utf8.c_str());
	
    // SAFE URL CONVERSION
    std::string url_utf8 = url.ToStdString();
    curl_easy_setopt(curl, CURLOPT_URL, url_utf8.c_str());

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

 	// REQUIRED CURL OPTIONS FOR GITHUB RAW
	// Follow redirects (GitHub Raw uses 301/302)
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	// Verify TLS certificates
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

	// Required for GitHub Raw (otherwise you get HTML wrappers)
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "ClimatologyPlugin/1.6");
	
	// Allow HTTPS + HTTP
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS | CURLPROTO_HTTP);
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS | CURLPROTO_HTTP);

	// Accept gzip/deflate (GitHub sends compressed responses)
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

	// Fail on HTTP errors (prevents saving HTML error pages)
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

	// Timeout (prevents hangs)
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    // ENABLE VERBOSE CURL LOGGING
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (res != CURLE_OK) {
        wxLogMessage("Climatology: curl error: %s", curl_easy_strerror(res));
        wxRemoveFile(dest);
        return false;
    }

    wxLogMessage("Climatology: download OK");
    return true;
}


// ------------------------------------------------------------
// Worker thread implementation
// ------------------------------------------------------------
wxThread::ExitCode DownloadWorker::Entry()
{
    wxLogMessage("Climatology: DownloadWorker thread started, %zu files in manifest",
                 m_mgr->m_files.size());

    const int totalFiles = (int)m_mgr->m_files.size();
    int filesDone = 0;

    wxString url;   // declare once BEFORE the loop
    for (auto& entry : m_mgr->m_files)
    {
        {
            std::lock_guard<std::mutex> lock(m_mgr->m_mutex);
            if (m_mgr->m_cancelled)
                break;
        }

        wxString dest = m_mgr->m_dataDir + entry.filename;

        // Skip existing files
        if (wxFileExists(dest) && wxFileName::GetSize(dest) > 0) {
            filesDone++;
            goto progress_update;
        }

        wxLogMessage("Climatology: ENTRY URL = '%s'", entry.url);
		
		url = entry.url;   // assign here, AFTER entry exists

        url.Trim(true).Trim(false);
        url.Replace("\r", "");
        url.Replace("\n", "");

        wxLogMessage("Climatology: Downloading %s -> %s", url, dest);

        bool ok = m_mgr->CurlDownload(url, dest);

        if (!ok)
        {
            wxLogWarning("Climatology: Download failed: %s", url);
            filesDone++;
            goto progress_update;
        }

        if (entry.filename.rfind("cyclone-", 0) == 0)
        {
            wxString gzfile = dest;
            wxString outFile = dest.BeforeLast('.');

            if (wxFileExists(outFile) && wxFileName::GetSize(outFile) > 0) {
                wxLogMessage("Climatology: cyclone file already decompressed: %s", outFile);
            } else {
                wxFileInputStream in(gzfile);
                if (!in.IsOk()) {
                    wxLogWarning("Climatology: cannot open cyclone gzip file %s", gzfile);
                } else {
                    wxZlibInputStream zin(in, wxZLIB_GZIP);
                    wxFileOutputStream out(outFile);

                    if (!out.IsOk()) {
                        wxLogWarning("Climatology: cannot create cyclone output file %s", outFile);
                    } else {
                        out.Write(zin);
                        wxLogMessage("Climatology: Decompressed cyclone file %s", outFile);
                    }
                }
            }
        }

        filesDone++;

	progress_update:
		wxCommandEvent progressEvt(EVT_DM_PROGRESS);
		int pct = totalFiles > 0 ? (filesDone * 100) / totalFiles : 100;
		progressEvt.SetInt(pct);
		wxQueueEvent(m_mgr->m_parent, progressEvt.Clone());
	}

	wxCommandEvent evt(EVT_DM_COMPLETE);
	wxQueueEvent(m_mgr->m_parent, evt.Clone());

	return (wxThread::ExitCode)0;

}


void DownloadManager::StartBackgroundDownload(bool interactive)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_worker) {
        wxLogMessage("Climatology: Download already in progress");
        return;
    }

    m_cancelled = false;

    if (interactive) {
        m_parent->Bind(EVT_DM_PROGRESS,
                       &DownloadManager::OnDownloadProgress,
                       this);

        m_progress = new wxProgressDialog(
            _("Climatology Data Download"),
            _("Downloading climatology data files..."),
            100,
            m_parent,
            wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_SMOOTH
        );
    }

    m_worker = new DownloadWorker(this, interactive);
    if (m_worker->Run() != wxTHREAD_NO_ERROR) {
        wxLogWarning("Climatology: Failed to start download worker thread");
        delete m_worker;
        m_worker = nullptr;
        DestroyProgressDialog();
    }
}


#endif // CLIMATOLOGY_BUNDLED_CURL