#include <GL/glew.h>
#include "gldefs.h"

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include "ManifestLoader.hpp"
#include "ManifestEntry.hpp"

#include <wx/progdlg.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/dir.h>


// ADD nlohmann/json
#include "nlohmann/json.hpp"
using json = nlohmann::json;


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

    std::string jsonString = jsonText.ToStdString();

    json root;

    try {
        root = json::parse(jsonString);
    }
    catch (const std::exception& e)
    {
        wxLogWarning("Climatology: JSON parse error in manifest.json: %s", e.what());
        return false;
    }

    // Validate root type
    if (!root.is_array())
    {
        wxLogWarning("Climatology: manifest.json root must be an array, but is type '%s'",
                     root.type_name());
        return false;
    }

    // Iterate entries
    for (size_t i = 0; i < root.size(); ++i)
    {
        const json& item = root[i];

        // Validate object type
        if (!item.is_object())
        {
            wxLogWarning("Climatology: manifest entry %zu is not an object", i);
            continue;
        }

        // Validate required fields
        if (!item.contains("filename"))
        {
            wxLogWarning("Climatology: manifest entry %zu missing required field 'filename'", i);
            continue;
        }
        if (!item["filename"].is_string())
        {
            wxLogWarning("Climatology: manifest entry %zu: 'filename' must be a string", i);
            continue;
        }

        // Optional fields with type validation
        std::string description = "";
        if (item.contains("description"))
        {
            if (!item["description"].is_string())
            {
                wxLogWarning("Climatology: manifest entry %zu: 'description' must be a string", i);
            }
            else
                description = item["description"].get<std::string>();
        }

        uint64_t size = 0;
        if (item.contains("size"))
        {
            if (!item["size"].is_number_integer())
            {
                wxLogWarning("Climatology: manifest entry %zu: 'size' must be an integer", i);
            }
            else
                size = item["size"].get<uint64_t>();
        }

        std::string checksum = "";
        if (item.contains("checksum"))
        {
            if (!item["checksum"].is_string())
            {
                wxLogWarning("Climatology: manifest entry %zu: 'checksum' must be a string", i);
            }
            else
                checksum = item["checksum"].get<std::string>();
        }

        // Sanitize filename using wxString
        wxString fname(item["filename"].get<std::string>());

        fname.Trim(true).Trim(false);
        fname.Replace("\r", "");
        fname.Replace("\n", "");

        if (fname.StartsWith("./"))
            fname = fname.Mid(2);

        while (fname.StartsWith("/"))
            fname = fname.Mid(1);

        ManifestEntry entry;
        entry.filename   = fname.ToStdString();
        entry.description = description;
        entry.size        = size;
        entry.checksum    = checksum;

        entries.push_back(entry);
    }

    wxLogMessage("Climatology: Loaded %llu manifest entries",
                 static_cast<unsigned long long>(entries.size()));

    return true;
}

