#include <GL/glew.h>
#include "gldefs.h"

// Otherwise MSVC will misorder wx includes (wx/wxprec.h)
// and the plugin API symbols will not resolve.
#include "ocpn_plugin_guarded.h"

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include "ManifestLoader.hpp"
#include "ManifestEntry.hpp"
#include "nlohmann/json.hpp"

#include <wx/progdlg.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/dir.h>

using json = nlohmann::json;

static const std::string BASE_URL =
    "https://raw.githubusercontent.com/rgleason/climatology_pi_data/master/";

ManifestLoader::ManifestLoader(const std::string& manifestPath)
    : m_manifestPath(manifestPath)
{
}

bool ManifestLoader::Load(std::vector<ManifestEntry>& entries)
{
    entries.clear();

    wxFile file(wxString::FromUTF8(m_manifestPath.c_str()));
    if (!file.IsOpened()) {
        wxLogWarning("Climatology: manifest.json not found: %s", m_manifestPath.c_str());
        return false;
    }

    wxString jsonText;
    if (!file.ReadAll(&jsonText)) {
        wxLogWarning("Climatology: Failed to read manifest.json");
        return false;
    }

    json root;
    try {
        root = json::parse(jsonText.ToStdString());
    }
    catch (const std::exception& e) {
        wxLogWarning("Climatology: JSON parse error: %s", e.what());
        return false;
    }

    if (!root.is_array()) {
        wxLogWarning("Climatology: manifest.json root must be an array");
        return false;
    }

    for (const auto& item : root) {
        if (!item.is_object())
            continue;

        if (!item.contains("filename") || !item["filename"].is_string())
            continue;

        ManifestEntry e;
        e.filename = item["filename"].get<std::string>();

        if (item.contains("description") && item["description"].is_string())
            e.description = item["description"].get<std::string>();

        if (item.contains("checksum") && item["checksum"].is_string())
            e.checksum = item["checksum"].get<std::string>();

        if (item.contains("size") && item["size"].is_number_integer())
            e.size = item["size"].get<uint64_t>();

        if (item.contains("required") && item["required"].is_boolean())
            e.required = item["required"].get<bool>();

        // URL: either provided or constructed
        if (item.contains("url") && item["url"].is_string())
            e.url = item["url"].get<std::string>();
        else
            e.url = BASE_URL + e.filename;

        entries.push_back(e);
    }

    wxLogMessage("Climatology: Loaded %zu manifest entries", entries.size());
    return true;
}