#pragma once

#pragma message("Header: " __FILE__)

#include <string>
#include <vector>
#include "ManifestEntry.hpp" 


class ManifestLoader {
public:
    // Construct with the full path to manifest.json
    ManifestLoader(const std::string& manifestPath);

    // Load entries from manifest.json into the vector
    bool Load(std::vector<ManifestEntry>& entries);
	

private:
    std::string m_manifestPath;
};
