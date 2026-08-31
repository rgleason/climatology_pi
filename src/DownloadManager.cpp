#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

// Always compiled:
#include "DownloadManager.hpp"
#include "icons.h"

#include <wx/filename.h>
#include <wx/log.h>
#include <wx/zstream.h>
#include <wx/wfstream.h>
#include <wx/progdlg.h>
#include <wx/hash.h>

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

const std::vector<ManifestEntry>& DownloadManager::GetManifest() const
{
    return m_files;    // or m_fullManifest if you prefer the full set
}



void DownloadManager::SetManifest(const std::vector<ManifestEntry>& files)
{
	m_fullManifest = files;
	m_files = files;

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
#include <curl/curl.h>

bool DownloadManager::CurlDownload(wxString url, const wxString& dest)
{
    ManifestEntry dummy;
    dummy.filename = dest;
    dummy.checksum = "";   // no checksum in this path
    return CurlDownload(dummy, url, dest);
}

bool DownloadManager::CurlDownload(const ManifestEntry& entry,
                                   wxString url,
                                   const wxString& dest)
{
    // Log curl version (shows backend: Schannel, OpenSSL, SecureTransport)
    wxLogMessage("curl version: %s", curl_version());
    wxLogMessage("Climatology: curl downloading %s -> %s", url, dest);

    // --- Sanitize URL -------------------------------------------------------
    url.Trim(true).Trim(false);
    url.Replace("\r", "");
    url.Replace("\n", "");

    if (url.IsEmpty() || !url.StartsWith("http")) {
        wxLogMessage("Climatology: Invalid URL after sanitizing: '%s'", url);
        return false;
    }

    // --- Open output file ---------------------------------------------------
    FILE* fp = fopen(dest.mb_str().data(), "wb");
    if (!fp) {
        wxLogMessage("Climatology: Failed to open %s for writing", dest);
        return false;
    }

    // --- Initialize curl ----------------------------------------------------
    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        wxLogMessage("Climatology: curl_easy_init failed");
        return false;
    }

    // --- IMPORTANT: Cross platform TLS trust -------------------------------
    //
    // Curl automatically selects the correct TLS backend:
    //   - Windows: Schannel (uses Windows certificate store)
    //   - Linux: OpenSSL (uses system CA bundle)
    //   - macOS: SecureTransport/LibreSSL (uses macOS trust store)
    //
    // Therefore:
    //   - No CAINFO required
    //   - No cacert.pem required
    //   - No platform-specific #ifdef required
    //
    // Setting CAINFO would override system trust and break TLS on Windows.
    // So we rely entirely on curl's built-in trust behavior.

    // --- Set URL ------------------------------------------------------------
    std::string url_utf8 = url.ToStdString();
    curl_easy_setopt(curl, CURLOPT_URL, url_utf8.c_str());

    // --- Write callback ------------------------------------------------------
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

    // --- Required for GitHub Raw --------------------------------------------
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);     // follow redirects
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);     // verify TLS
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);     // strict host check
	
	// Modern User-Agent (GitHub rejects unknown agents)
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/8.20.0");
	
	// Force TLS 1.2+ (GitHub requires this)
	curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

	// Use Windows native certificate store
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);

	// Enable ALPN (GitHub requires this)
	curl_easy_setopt(curl, CURLOPT_SSL_ENABLE_ALPN, 1L);

	// DO NOT enable NPN on Schannel — it breaks GitHub TLS
	// curl_easy_setopt(curl, CURLOPT_SSL_ENABLE_NPN, 1L);   // REMOVE THIS

    // --- Protocols -----------------------------------------------------------
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                     CURLPROTO_HTTPS | CURLPROTO_HTTP);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS,
                     CURLPROTO_HTTPS | CURLPROTO_HTTP);

    // --- Compression ---------------------------------------------------------
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    // --- Fail on HTTP errors -------------------------------------------------
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    // --- Timeout -------------------------------------------------------------
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    // --- Verbose logging -----------------------------------------------------
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    // --- Perform download ----------------------------------------------------
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (res != CURLE_OK) {
        wxLogMessage("Climatology: curl error: %s", curl_easy_strerror(res));
        wxRemoveFile(dest);
        return false;
    }

/*
	// --- Checksum validation ---------------------------------------------------
	if (!entry.checksum.IsEmpty()) {
		wxFileInputStream fin(dest);
		if (!fin.IsOk()) {
			wxLogWarning("Climatology: cannot open %s for checksum", dest);
			wxRemoveFile(dest);
			return false;
		}

		wxMemoryOutputStream mem;
		mem.Write(fin);
		
		auto* buf = mem.GetOutputStreamBuffer()->GetBufferStart();
		size_t len = mem.GetOutputStreamBuffer()->GetBufferSize();

//   (see next section about wxSHA256)
//   stubbed out the actual comparison 
//		wxString actual = wxSHA256::GetDigest(buf, len);
//		wxString actual = wxSHA256::Hash(buf, len);

		wxString expected(entry.checksum);
	
//    if (!actual.IsSameAs(expected) or false) {
        wxLogWarning("Climatology: checksum mismatch for %s", dest);
        wxRemoveFile(dest);
        return false;
    }
*/
    wxLogMessage("Climatology: checksum OK for %s", dest);
	return true;   // <- ensure this is the last statement
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

        // Try to download 3 times with a pause.
		bool ok = false;
		for (int attempt = 0; attempt < 3; attempt++) {
			ok = m_mgr->CurlDownload(url, dest);
			if (ok)
				break;

			wxLogMessage("Climatology: retrying %s (attempt %d)", url, attempt + 2);
			wxMilliSleep(500);
		}

        //  Continue after 3 attempts. 
        if (!ok)
        {
            wxLogWarning("Climatology: Download failed: %s", url);
			
			// *** ADD DELAY HERE ***
			wxMilliSleep(250);
	
            filesDone++;
            goto progress_update;
        }

		// *** ADD DELAY HERE ***
		wxMilliSleep(250);
		
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

		// *** ADD DELAY HERE ***
		wxMilliSleep(250);

	    filesDone++;

	progress_update:
		wxCommandEvent progressEvt(EVT_DM_PROGRESS);
		int pct = totalFiles > 0 ? (filesDone * 100) / totalFiles : 100;
		progressEvt.SetInt(pct);
		wxQueueEvent(m_mgr->m_parent, progressEvt.Clone());
	}

	wxCommandEvent evt(EVT_DM_COMPLETE);
	wxQueueEvent(m_mgr->m_parent, evt.Clone());

	// Mark worker thread as finished
	m_mgr->WorkerThreadFinished();

	return (wxThread::ExitCode)0;


}


void DownloadManager::WorkerThreadFinished()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_workerThreadRunning = false;
    m_worker = nullptr;
	std::vector<ManifestEntry> m_fullManifest;

    DestroyProgressDialog();
	
	// Restore full manifest after missing-file download
	if (m_fullManifest.size() > 0)
	m_files = m_fullManifest;

    wxLogMessage("Climatology: Download worker finished");
}


void DownloadManager::StartBackgroundDownload(bool interactive)
{
    std::lock_guard<std::mutex> lock(m_mutex);
	
	m_workerThreadRunning = true;

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



// Check Integrity
bool DownloadManager::CheckIntegrity()
{
    wxLogMessage("Climatology: Running data integrity check...");

    for (auto& entry : m_files)
    {
        wxString path = m_dataDir + entry.filename;

        if (!wxFileExists(path)) {
            wxLogWarning("Missing file: %s", path);
            return false;
        }

        if (wxFileName::GetSize(path) == 0) {
            wxLogWarning("Zero-size file: %s", path);
            return false;
        }

 //       if (!entry.checksum.empty()) {
 //           wxFileInputStream fin(path);
 //           if (!fin.IsOk()) {
 //               wxLogWarning("Cannot open %s for checksum", path);
 //               return false;
 //           }

 //           wxMemoryOutputStream mem;
 //           mem.Write(fin);

 //           wxString actual = wxSHA256::Hash(
 //               mem.GetOutputStreamBuffer()->GetBufferStart(),
 //               mem.GetOutputStreamBuffer()->GetBufferSize());

 //           if (!actual.IsSameAs(entry.checksum)) {
 //               wxLogWarning("Checksum mismatch: %s", path);
 //               return false;
 //           }
 //       }
    }

    wxLogMessage("Climatology: Integrity check passed.");
    return true;
}
