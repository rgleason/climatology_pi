#pragma once
#include <string>

struct ManifestEntry {
    std::string filename;
	std::string url;
    std::string description;
    uint64_t size = 0;
    std::string checksum;
    bool required = true;   // ← THIS FIELD IS REQUIRED
};
