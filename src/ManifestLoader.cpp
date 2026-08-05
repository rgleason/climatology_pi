
// GLEW MUST be first
//#define GLEW_STATIC
#include <GL/glew.h>
#include "gldefs.h"

#include "ManifestLoader.hpp"
#include "ManifestEntry.hpp"
#include "climatology_pi.h"
#include "ClimatologyEnums.h"

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/progdlg.h>

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/jsonreader.h>
#include <wx/jsonval.h>
#include <wx/dir.h>

ManifestLoader::ManifestLoader(const std::string& manifestPath)
    : m_manifestPath(manifestPath)
{
}

bool ManifestLoader::Load(std::vector<ManifestEntry>& entries)
{
    entries.clear();

    if (!wxFileExists(m_manifestPath))
    {
		wxLogWarning("Climatology: manifest.json not found: %s", m_manifestPath.c_str());
        return false;
    }

    wxString jsonText;
    wxFile file(m_manifestPath);

    if (!file.IsOpened() || !file.ReadAll(&jsonText))
    {
        wxLogWarning("Climatology: Failed to read manifest.json");
        return false;
    }

    wxJSONValue root;
    wxJSONReader reader;

    int errors = reader.Parse(jsonText, &root);
    if (errors > 0)
    {
        wxLogWarning("Climatology: JSON parse error in manifest.json");
        return false;
    }

    if (!root.IsArray())
    {
        wxLogWarning("Climatology: manifest.json root is not an array");
        return false;
    }

for (int i = 0; i < root.Size(); i++)
{
    wxJSONValue item = root[i];

    if (!item.HasMember("filename"))
    {
        wxLogWarning("Climatology: manifest entry missing filename");
        continue;
    }

    ManifestEntry entry;

    // Read metadata
    entry.description = item.HasMember("description") ? item["description"].AsString() : "";
    entry.size        = item.HasMember("size") ? (uint64_t)item["size"].AsInt() : 0;
    entry.checksum    = item.HasMember("checksum") ? item["checksum"].AsString() : "";

    // Sanitize filename using wxString
    wxString fname = item["filename"].AsString();

    fname.Trim(true).Trim(false);
    fname.Replace("\r", "");
    fname.Replace("\n", "");

    if (fname.StartsWith("./"))
        fname = fname.Mid(2);

    while (fname.StartsWith("/"))
        fname = fname.Mid(1);

    entry.filename = fname.ToStdString();

    entries.push_back(entry);
}


wxLogMessage("Climatology: Loaded %llu manifest entries",
             static_cast<unsigned long long>(entries.size()));
    return true;
}
