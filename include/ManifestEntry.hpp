#pragma once

#include <string>
#include <cstdint>

struct ManifestEntry {
    std::string filename;
    std::string description;
    std::string checksum;
    uint64_t size = 0;
    bool required = true;
    std::string url;        // ← LAST, matching manifest.json
};
