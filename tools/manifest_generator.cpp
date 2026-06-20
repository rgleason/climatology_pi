#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

std::string sha256_file(const fs::path& path)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    DWORD hashObjectSize = 0, hashLen = 0, cbData = 0;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return "";

    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjectSize, sizeof(DWORD), &cbData, 0);
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(DWORD), &cbData, 0);

    std::vector<UCHAR> hashObject(hashObjectSize);
    std::vector<UCHAR> hash(hashLen);

    if (BCryptCreateHash(hAlg, &hHash, hashObject.data(), hashObjectSize, nullptr, 0, 0) != 0)
        return "";

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return "";

    std::vector<char> buffer(32768);
    while (file.good()) {
        file.read(buffer.data(), buffer.size());
        BCryptHashData(hHash, (PUCHAR)buffer.data(), file.gcount(), 0);
    }

    BCryptFinishHash(hHash, hash.data(), hashLen, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    std::ostringstream ss;
    for (auto b : hash)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)b;

    return ss.str();
}

std::string infer_category(const std::string& name)
{
    if (name.rfind("wind", 0) == 0) return "wind";
    if (name.rfind("current", 0) == 0) return "current";
    if (name.rfind("sealevelpressure", 0) == 0) return "pressure";
    if (name.rfind("seasurfacetemperature", 0) == 0) return "sst";
    if (name.rfind("airtemperature", 0) == 0) return "airtemp";
    if (name.rfind("cloud", 0) == 0) return "cloud";
    if (name.rfind("precipitation", 0) == 0) return "precip";
    if (name.rfind("relativehumidity", 0) == 0) return "humidity";
    if (name.rfind("lightning", 0) == 0) return "lightning";
    if (name.rfind("seadepth", 0) == 0) return "seadepth";
    if (name.rfind("cyclone", 0) == 0) return "cyclone";
    if (name.rfind("elnino", 0) == 0) return "elnino";
    return "unknown";
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: manifest_generator <data_dir> <output.json>\n";
        return 1;
    }

    fs::path dataDir = argv[1];
    fs::path output = argv[2];

    std::ofstream out(output);
    out << "[\n";

    bool first = true;

    for (auto& entry : fs::directory_iterator(dataDir)) {
        if (!entry.is_regular_file())
            continue;

        fs::path p = entry.path();
        std::string name = p.filename().string();

        if (!first) out << ",\n";
        first = false;

        out << "  {\n";
        out << "    \"filename\": \"" << name << "\",\n";
        out << "    \"category\": \"" << infer_category(name) << "\",\n";
        out << "    \"size\": " << fs::file_size(p) << ",\n";
        out << "    \"checksum\": \"" << sha256_file(p) << "\",\n";
        out << "    \"required\": true\n";
        out << "  }";
    }

    out << "\n]\n";
    out.close();

    std::cout << "Manifest written to " << output << "\n";
    return 0;
}
