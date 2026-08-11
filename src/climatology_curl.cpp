#include "climatology_curl.h"
#include <wx/filename.h>
#include <wx/log.h>

#ifdef CLIMATOLOGY_BUNDLED_CURL
#include <windows.h>
#endif

bool Climatology_InitCurl()
{
#ifdef CLIMATOLOGY_BUNDLED_CURL
    wxString pluginDir = GetPluginDataDir("climatology_pi");
    wxFileName curlDll(pluginDir, "libcurl.dll");

    if (!curlDll.FileExists())
    {
        wxLogMessage("Climatology: bundled libcurl.dll not found at %s",
                     curlDll.GetFullPath());
        return false;
    }

    HMODULE hCurl = LoadLibraryW(curlDll.GetFullPath().wc_str());
    if (!hCurl)
    {
        wxLogMessage("Climatology: LoadLibrary failed for %s",
                     curlDll.GetFullPath());
        return false;
    }

    wxLogMessage("Climatology: bundled libcurl.dll loaded from %s",
                 curlDll.GetFullPath());
#endif

    return true;
}
