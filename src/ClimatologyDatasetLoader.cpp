#include "ClimatologyDatasetLoader.h"

#include "ClimatologyChecksum.h"

#include "zuFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>

namespace climatology {
namespace {

double NaN()
{
    return std::numeric_limits<double>::quiet_NaN();
}

std::string JoinPath(const std::string& directory, const std::string& name)
{
    if(directory.empty()) return name;
    const char last = directory[directory.size() - 1];
    return directory + (last == '/' || last == '\\' ? "" : "/") + name;
}

bool FileExists(const std::string& path)
{
    std::ifstream stream(path.c_str(), std::ios::binary);
    return stream.good();
}

std::string ResolveDataDirectory(const std::string& directory)
{
    if(FileExists(JoinPath(directory, "dataset-manifest.json")))
        return directory;

    // OpenCPN exposes the plugin resource root, while callers focused on the
    // dataset often already pass its data child.  Accept both forms for a
    // versioned package, but retain the historical direct-directory fallback
    // when no manifest identifies the nested layout.
    const std::string packaged_data = JoinPath(directory, "data");
    if(FileExists(JoinPath(packaged_data, "dataset-manifest.json")))
        return packaged_data;
    return directory;
}

std::uint64_t FileSize(const std::string& path)
{
    std::ifstream stream(path.c_str(), std::ios::binary | std::ios::ate);
    return stream ? static_cast<std::uint64_t>(stream.tellg()) : 0;
}

std::string ReadText(const std::string& path)
{
    std::ifstream stream(path.c_str(), std::ios::binary);
    std::ostringstream output;
    output << stream.rdbuf();
    return output.str();
}

std::string JsonString(const std::string& text, const std::string& key)
{
    const std::regex expression("\\\"" + key +
        "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    return std::regex_search(text, match, expression) ? match[1].str() : "";
}

bool ValidateManifest(const std::string& directory,
                      ClimatologyDatasetSnapshot& snapshot,
                      std::vector<std::string>& errors)
{
    const std::string path = JoinPath(directory, "dataset-manifest.json");
    if(!FileExists(path)) {
        snapshot.metadata.version = "legacy/unversioned";
        return true;
    }
    const std::string text = ReadText(path);
    snapshot.metadata.version = JsonString(text, "dataset_version");
    if(snapshot.metadata.version.empty()) {
        errors.push_back("dataset-manifest.json has no dataset_version");
        return false;
    }
    snapshot.metadata.period_start = JsonString(text, "start");
    snapshot.metadata.period_end = JsonString(text, "end");
    snapshot.metadata.period_kind = JsonString(text, "kind");

    // Bind the manifest to every declared output before decoding. Strict
    // field decoders below additionally verify decompressed lengths/schemas.
    const std::regex output(
        "\\{\\s*\\\"bytes\\\"\\s*:\\s*([0-9]+)\\s*,\\s*"
        "\\\"file\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"\\s*,\\s*"
        "\\\"sha256\\\"\\s*:\\s*\\\"([0-9a-fA-F]{64})\\\"",
        std::regex::ECMAScript);
    std::sregex_iterator iterator(text.begin(), text.end(), output);
    const std::sregex_iterator end;
    std::size_t count = 0;
    for(; iterator != end; ++iterator) {
        ++count;
        const std::uint64_t expected =
            static_cast<std::uint64_t>(std::strtoull((*iterator)[1].str().c_str(),
                                                     0, 10));
        const std::string file = (*iterator)[2].str();
        const std::string checksum = (*iterator)[3].str();
        const std::string output_path = JoinPath(directory, file);
        if(!FileExists(output_path))
            errors.push_back("manifest output is missing: " + file);
        else if(FileSize(output_path) != expected)
            errors.push_back("manifest output has the wrong size: " + file);
        else if(Sha256File(output_path) != checksum)
            errors.push_back("manifest output checksum mismatch: " + file);
    }
    if(count == 0)
        errors.push_back("dataset-manifest.json contains no outputs");
    snapshot.metadata.manifest_validated = errors.empty();
    return errors.empty();
}

class CompressedFile {
public:
    explicit CompressedFile(const std::string& base) : m_file(0)
    {
        m_file = zu_open(base.c_str(), "rb", ZU_COMPRESS_AUTO);
        if(!m_file)
            m_file = zu_open((base + ".gz").c_str(), "rb", ZU_COMPRESS_AUTO);
    }
    ~CompressedFile() { if(m_file) zu_close(m_file); }
    bool Valid() const { return m_file != 0; }
    long Position() const { return m_file ? zu_tell(m_file) : 0; }
    bool Read(void* value, std::size_t bytes)
    {
        return m_file && zu_read(m_file, value, static_cast<long>(bytes)) ==
            static_cast<int>(bytes);
    }
    bool AtEnd()
    {
        unsigned char extra;
        return !m_file || zu_read(m_file, &extra, 1) == 0;
    }
private:
    CompressedFile(const CompressedFile&);
    CompressedFile& operator=(const CompressedFile&);
    ZUFILE* m_file;
};

bool ReadU16(CompressedFile& file, std::uint16_t& value)
{
    unsigned char bytes[2];
    if(!file.Read(bytes, sizeof bytes)) return false;
    value = static_cast<std::uint16_t>(bytes[0]) |
            static_cast<std::uint16_t>(bytes[1]) << 8;
    return true;
}

bool ReadI16(CompressedFile& file, std::int16_t& value)
{
    std::uint16_t raw;
    if(!ReadU16(file, raw)) return false;
    value = static_cast<std::int16_t>(raw);
    return true;
}

GridGeometry Geometry(std::size_t rows, std::size_t columns,
                      double latitude_origin, double longitude_origin,
                      double latitude_step, double longitude_step,
                      bool latitude_decreases = true,
                      double minimum_latitude = -90.0,
                      double maximum_latitude = 90.0)
{
    GridGeometry geometry;
    geometry.rows = rows;
    geometry.columns = columns;
    geometry.latitude_origin = latitude_origin;
    geometry.longitude_origin = longitude_origin;
    geometry.latitude_step = latitude_step;
    geometry.longitude_step = longitude_step;
    geometry.latitude_decreases = latitude_decreases;
    geometry.minimum_latitude = minimum_latitude;
    geometry.maximum_latitude = maximum_latitude;
    return geometry;
}

template<typename Raw, typename Decode>
bool LoadScalar(const std::string& directory, const std::string& filename,
                DatasetField id, const GridGeometry& geometry,
                Raw missing, bool has_missing, Decode decode,
                ClimatologyDatasetSnapshot& snapshot,
                std::vector<std::string>& errors)
{
    CompressedFile file(JoinPath(directory, filename));
    if(!file.Valid()) {
        errors.push_back("missing scalar file: " + filename);
        return false;
    }
    const std::size_t cells = geometry.CellCount();
    std::vector<Raw> raw(kClimatologyMonths * cells);
    if(!file.Read(&raw[0], raw.size() * sizeof(Raw)) || !file.AtEnd()) {
        errors.push_back("truncated or oversized scalar file: " + filename);
        return false;
    }
    MonthlyScalarField* field = snapshot.MutableScalar(id);
    field->geometry = geometry;
    for(std::size_t month = 0; month < kClimatologyMonths; ++month) {
        field->values[month].resize(cells);
        for(std::size_t cell = 0; cell < cells; ++cell) {
            const Raw value = raw[month * cells + cell];
            field->values[month][cell] = has_missing && value == missing
                ? static_cast<float>(NaN())
                : static_cast<float>(decode(value));
        }
        field->available[month] = true;
    }
    field->values[kAllTimesMonth].resize(cells);
    for(std::size_t cell = 0; cell < cells; ++cell) {
        double sum = 0.0;
        int count = 0;
        for(std::size_t month = 0; month < kClimatologyMonths; ++month) {
            const float value = field->values[month][cell];
            if(std::isfinite(value)) sum += value, ++count;
        }
        field->values[kAllTimesMonth][cell] = count
            ? static_cast<float>(sum / count)
            : static_cast<float>(NaN());
    }
    field->available[kAllTimesMonth] = true;
    snapshot.availability.fields[static_cast<std::size_t>(id)] = true;
    return true;
}

template<typename Raw, typename Decode>
bool LoadStaticScalar(const std::string& directory,
                      const std::string& filename, DatasetField id,
                      const GridGeometry& geometry, Raw missing,
                      Decode decode, ClimatologyDatasetSnapshot& snapshot,
                      std::vector<std::string>& errors)
{
    CompressedFile file(JoinPath(directory, filename));
    if(!file.Valid()) {
        errors.push_back("missing scalar file: " + filename);
        return false;
    }
    const std::size_t cells = geometry.CellCount();
    std::vector<Raw> raw(cells);
    if(!file.Read(&raw[0], raw.size() * sizeof(Raw)) || !file.AtEnd()) {
        errors.push_back("truncated or oversized scalar file: " + filename);
        return false;
    }
    MonthlyScalarField* field = snapshot.MutableScalar(id);
    field->geometry = geometry;
    std::vector<float> decoded(cells);
    for(std::size_t cell = 0; cell < cells; ++cell)
        decoded[cell] = raw[cell] == missing
            ? static_cast<float>(NaN())
            : static_cast<float>(decode(raw[cell]));
    for(std::size_t month = 0; month < kStoredMonths; ++month) {
        field->values[month] = decoded;
        field->available[month] = true;
    }
    snapshot.availability.fields[static_cast<std::size_t>(id)] = true;
    return true;
}

bool LoadPressure(const std::string& directory,
                  ClimatologyDatasetSnapshot& snapshot,
                  std::vector<std::string>& errors)
{
    CompressedFile file(JoinPath(directory, "sealevelpressure"));
    if(!file.Valid()) {
        errors.push_back("missing scalar file: sealevelpressure");
        return false;
    }
    const GridGeometry geometry = Geometry(90, 180, 89.0, 1.5, 2.0, 2.0);
    const std::size_t cells = geometry.CellCount();
    MonthlyScalarField* field =
        snapshot.MutableScalar(DatasetField::SeaLevelPressure);
    field->geometry = geometry;
    for(std::size_t month = 0; month < kClimatologyMonths; ++month) {
        field->values[month].resize(cells);
        for(std::size_t cell = 0; cell < cells; ++cell) {
            std::uint16_t raw;
            if(!ReadU16(file, raw)) {
                errors.push_back("truncated scalar file: sealevelpressure");
                return false;
            }
            const std::int16_t value = static_cast<std::int16_t>(raw);
            field->values[month][cell] = value == 32767
                ? static_cast<float>(NaN())
                : static_cast<float>(value * 0.01 + 1000.0);
        }
        field->available[month] = true;
    }
    if(!file.AtEnd()) {
        errors.push_back("oversized scalar file: sealevelpressure");
        return false;
    }
    field->values[kAllTimesMonth].resize(cells);
    for(std::size_t cell = 0; cell < cells; ++cell) {
        double sum = 0.0;
        int count = 0;
        for(std::size_t month = 0; month < kClimatologyMonths; ++month) {
            const float value = field->values[month][cell];
            if(std::isfinite(value)) sum += value, ++count;
        }
        field->values[kAllTimesMonth][cell] = count
            ? static_cast<float>(sum / count)
            : static_cast<float>(NaN());
    }
    field->available[kAllTimesMonth] = true;
    snapshot.availability.fields[
        static_cast<std::size_t>(DatasetField::SeaLevelPressure)] = true;
    return true;
}

bool LoadWindMonth(const std::string& directory, int month,
                   MonthlyWindDistributionField& field,
                   std::vector<std::string>& errors)
{
    std::ostringstream name;
    name << "wind" << (month < 9 ? "0" : "") << month + 1;
    CompressedFile file(JoinPath(directory, name.str()));
    if(!file.Valid()) {
        errors.push_back("missing wind file: " + name.str());
        return false;
    }
    std::uint16_t header[7];
    for(std::size_t i = 0; i < 7; ++i)
        if(!ReadU16(file, header[i])) {
            errors.push_back("truncated wind header: " + name.str());
            return false;
        }
    if(header[0] != 0xfefe || !header[1] || !header[2] || header[3] != 8 ||
       !header[4] || !header[5] || !header[6]) {
        errors.push_back("invalid wind header: " + name.str());
        return false;
    }
    const GridGeometry geometry = Geometry(
        header[1], header[2], -90.0 + 90.0 / header[1],
        180.0 / header[2], 180.0 / header[1], 360.0 / header[2], false);
    if(month == 0) {
        field.geometry = geometry;
        field.frequency_divisor = header[4];
        field.speed_divisor = static_cast<double>(header[5]) / header[6];
    } else if(field.geometry.rows != geometry.rows ||
              field.geometry.columns != geometry.columns ||
              field.frequency_divisor != header[4] ||
              field.speed_divisor != static_cast<double>(header[5]) / header[6]) {
        errors.push_back("wind geometry/scale changes between months");
        return false;
    }
    const std::size_t cells = geometry.CellCount();
    field.values[month].resize(cells);
    for(std::size_t cell = 0; cell < cells; ++cell) {
        unsigned char combined;
        if(!file.Read(&combined, 1)) {
            errors.push_back("truncated wind mask: " + name.str());
            return false;
        }
        WindDistributionCell& output = field.values[month][cell];
        output.valid = combined <= 200;
        if(output.valid) {
            if(combined >= 100) output.gale_percent = combined - 100;
            else output.calm_percent = combined;
        }
    }
    for(std::size_t sector = 0; sector < kWindSectors; ++sector)
        for(std::size_t cell = 0; cell < cells; ++cell) {
            WindDistributionCell& output = field.values[month][cell];
            if(!output.valid) continue;
            if(!file.Read(&output.frequency[sector], 1)) {
                errors.push_back("truncated wind frequencies: " + name.str());
                return false;
            }
        }
    for(std::size_t sector = 0; sector < kWindSectors; ++sector)
        for(std::size_t cell = 0; cell < cells; ++cell) {
            WindDistributionCell& output = field.values[month][cell];
            if(!output.valid || output.frequency[sector] == 0) continue;
            if(!file.Read(&output.encoded_speed[sector], 1)) {
                errors.push_back("truncated wind speeds: " + name.str());
                return false;
            }
        }
    if(!file.AtEnd()) {
        errors.push_back("oversized wind file: " + name.str());
        return false;
    }

    std::ostringstream extras_name;
    extras_name << "wind-extras" << (month < 9 ? "0" : "") << month + 1;
    CompressedFile extras(JoinPath(directory, extras_name.str()));
    if(extras.Valid()) {
        char magic[4];
        std::uint16_t rows, columns;
        if(!extras.Read(magic, sizeof magic) ||
           std::string(magic, sizeof magic) != "WEX1" ||
           !ReadU16(extras, rows) || !ReadU16(extras, columns) ||
           rows != geometry.rows || columns != geometry.columns) {
            errors.push_back("invalid wind extras: " + extras_name.str());
            return false;
        }
        std::vector<unsigned char> calm(cells), gale(cells);
        if(!extras.Read(&calm[0], cells) || !extras.Read(&gale[0], cells) ||
           !extras.AtEnd()) {
            errors.push_back("truncated wind extras: " + extras_name.str());
            return false;
        }
        for(std::size_t cell = 0; cell < cells; ++cell) {
            WindDistributionCell& output = field.values[month][cell];
            if(output.valid) {
                if(calm[cell] > 100 || gale[cell] > 100) {
                    errors.push_back("invalid wind probability: " + extras_name.str());
                    return false;
                }
                output.calm_percent = calm[cell];
                output.gale_percent = gale[cell];
            } else if(calm[cell] != 255 || gale[cell] != 255) {
                errors.push_back("wind extras mask mismatch: " + extras_name.str());
                return false;
            }
        }
    }
    field.available[month] = true;
    return true;
}

void AverageWind(MonthlyWindDistributionField& field)
{
    const std::size_t cells = field.geometry.CellCount();
    field.values[kAllTimesMonth].resize(cells);
    for(std::size_t cell = 0; cell < cells; ++cell) {
        WindDistributionCell& output = field.values[kAllTimesMonth][cell];
        bool valid = true;
        unsigned int calm = 0, gale = 0;
        std::array<unsigned int, kWindSectors> frequency = {{0}};
        std::array<unsigned int, kWindSectors> speed = {{0}};
        for(std::size_t month = 0; month < kClimatologyMonths; ++month) {
            const WindDistributionCell& input = field.values[month][cell];
            if(!input.valid) { valid = false; break; }
            calm += input.calm_percent;
            gale += input.gale_percent;
            for(std::size_t sector = 0; sector < kWindSectors; ++sector) {
                frequency[sector] += input.frequency[sector];
                speed[sector] += input.encoded_speed[sector];
            }
        }
        output.valid = valid;
        if(!valid) continue;
        output.calm_percent = calm / kClimatologyMonths;
        output.gale_percent = gale / kClimatologyMonths;
        for(std::size_t sector = 0; sector < kWindSectors; ++sector) {
            output.frequency[sector] = frequency[sector] / kClimatologyMonths;
            output.encoded_speed[sector] = speed[sector] / kClimatologyMonths;
        }
    }
    field.available[kAllTimesMonth] = true;
}

bool LoadCurrentMonth(const std::string& directory, int month,
                      MonthlyVectorField& field,
                      std::vector<std::string>& errors)
{
    std::ostringstream name;
    name << "current" << (month < 9 ? "0" : "") << month + 1;
    CompressedFile file(JoinPath(directory, name.str()));
    if(!file.Valid()) {
        errors.push_back("missing current file: " + name.str());
        return false;
    }
    std::uint16_t rows, columns, multiplier;
    if(!ReadU16(file, rows) || !ReadU16(file, columns) ||
       !ReadU16(file, multiplier) || !rows || !columns || !multiplier) {
        errors.push_back("invalid current header: " + name.str());
        return false;
    }
    const GridGeometry geometry = Geometry(
        rows, columns, 80.0, 0.0, rows > 1 ? 160.0 / (rows - 1) : 160.0,
        360.0 / columns, true, -80.0, 80.0);
    if(month == 0) field.geometry = geometry;
    else if(field.geometry.rows != rows || field.geometry.columns != columns) {
        errors.push_back("current geometry changes between months");
        return false;
    }
    const std::size_t cells = geometry.CellCount();
    field.eastward[month].resize(cells);
    field.northward[month].resize(cells);
    std::vector<float>* components[2] = {
        &field.eastward[month], &field.northward[month]};
    for(int component = 0; component < 2; ++component)
        for(std::size_t cell = 0; cell < cells; ++cell) {
            signed char raw;
            if(!file.Read(&raw, 1)) {
                errors.push_back("truncated current file: " + name.str());
                return false;
            }
            (*components[component])[cell] = raw == -128
                ? static_cast<float>(NaN())
                : static_cast<float>(raw) / multiplier;
        }
    if(!file.AtEnd()) {
        errors.push_back("oversized current file: " + name.str());
        return false;
    }
    field.available[month] = true;
    return true;
}

void AverageCurrent(MonthlyVectorField& field)
{
    const std::size_t cells = field.geometry.CellCount();
    field.eastward[kAllTimesMonth].resize(cells);
    field.northward[kAllTimesMonth].resize(cells);
    for(std::size_t cell = 0; cell < cells; ++cell) {
        double eastward = 0.0, northward = 0.0;
        int count = 0;
        for(std::size_t month = 0; month < kClimatologyMonths; ++month) {
            const float east = field.eastward[month][cell];
            const float north = field.northward[month][cell];
            if(std::isfinite(east) && std::isfinite(north)) {
                eastward += east;
                northward += north;
                ++count;
            }
        }
        field.eastward[kAllTimesMonth][cell] = count
            ? static_cast<float>(eastward / count) : static_cast<float>(NaN());
        field.northward[kAllTimesMonth][cell] = count
            ? static_cast<float>(northward / count) : static_cast<float>(NaN());
    }
    field.available[kAllTimesMonth] = true;
}

CycloneState DecodeCycloneState(signed char state, bool& valid)
{
    valid = true;
    switch(state) {
    case '*': return CycloneState::Tropical;
    case 'S': return CycloneState::Subtropical;
    case 'E': return CycloneState::Extratropical;
    case 'W': return CycloneState::Wave;
    case 'L': return CycloneState::Remnant;
    case 'D': case 'X': return CycloneState::Unknown;
    default: valid = false; return CycloneState::Unknown;
    }
}

bool LoadCyclones(const std::string& directory, const std::string& filename,
                  bool south, std::vector<CycloneTrack>& output,
                  std::vector<std::string>& errors)
{
    CompressedFile file(JoinPath(directory, filename));
    if(!file.Valid()) {
        errors.push_back("missing cyclone file: " + filename);
        return false;
    }
    for(;;) {
        std::uint16_t year;
        // A clean EOF after a complete track is the normal terminator.
        unsigned char first;
        if(!file.Read(&first, 1)) break;
        unsigned char second;
        if(!file.Read(&second, 1)) {
            errors.push_back("truncated cyclone year: " + filename);
            return false;
        }
        year = static_cast<std::uint16_t>(first) |
               static_cast<std::uint16_t>(second) << 8;
        CycloneTrack track;
        int last_month = 0;
        bool have_last = false;
        CycloneState last_state = CycloneState::Unknown;
        CivilTime last_time;
        double last_latitude = 0.0, last_longitude = 0.0;
        unsigned char last_wind = 0;
        std::uint16_t last_pressure = 0;
        for(;;) {
            signed char encoded_state;
            if(!file.Read(&encoded_state, 1)) {
                errors.push_back("truncated cyclone track: " + filename);
                return false;
            }
            if(encoded_state == -128) break;
            bool valid_state;
            const CycloneState state = DecodeCycloneState(encoded_state, valid_state);
            signed char encoded_day, encoded_month;
            std::int16_t latitude, longitude;
            unsigned char wind;
            std::uint16_t pressure;
            if(!valid_state || !file.Read(&encoded_day, 1) ||
               !file.Read(&encoded_month, 1) ||
               !ReadI16(file, latitude) || !ReadI16(file, longitude) ||
               !file.Read(&wind, 1) || !ReadU16(file, pressure)) {
                errors.push_back("invalid cyclone record: " + filename);
                return false;
            }
            const int month = encoded_month - 1;
            if(month < 0 || month > 11 || encoded_day <= 0) {
                errors.push_back("invalid cyclone date: " + filename);
                return false;
            }
            if(month < last_month) ++year;
            last_month = month;
            CivilTime time;
            time.year = year;
            time.month = month;
            time.day = encoded_day / 4;
            time.hour = (encoded_day % 4) * 6;
            const double lat = (south ? -1.0 : 1.0) * latitude / 10.0;
            const double lon = longitude / 10.0;
            if(std::fabs(lat) >= 90.0 || lon < -360.0 || lon > 15.0 ||
               time.day < 1 || time.day > 31) {
                errors.push_back("out-of-range cyclone record: " + filename);
                return false;
            }
            if(have_last) {
                CycloneSegment segment;
                segment.state = last_state;
                segment.time = last_time;
                segment.latitude0 = last_latitude;
                segment.longitude0 = last_longitude;
                segment.latitude1 = lat;
                segment.longitude1 = lon;
                segment.wind_knots = last_wind;
                segment.pressure_hpa = last_pressure;
                track.segments.push_back(segment);
            }
            have_last = true;
            last_state = state;
            last_time = time;
            last_latitude = lat;
            last_longitude = lon;
            last_wind = wind;
            last_pressure = pressure;
        }
        output.push_back(track);
    }
    return !output.empty();
}

bool LoadEnso(const std::string& directory,
              ClimatologyDatasetSnapshot& snapshot,
              std::vector<std::string>& errors)
{
    std::ifstream file(JoinPath(directory, "elnino_years.txt").c_str());
    if(!file) {
        errors.push_back("missing ENSO file: elnino_years.txt");
        return false;
    }
    std::string line;
    std::getline(file, line);
    while(std::getline(file, line)) {
        std::istringstream row(line);
        int year;
        std::array<double, kClimatologyMonths> values;
        if(!(row >> year)) continue;
        bool valid = true;
        for(std::size_t month = 0; month < kClimatologyMonths; ++month)
            if(!(row >> values[month])) { valid = false; break; }
        if(!valid) {
            errors.push_back("invalid ENSO row for year " + std::to_string(year));
            return false;
        }
        snapshot.enso_by_year[year] = values;
    }
    snapshot.availability.enso = !snapshot.enso_by_year.empty();
    return snapshot.availability.enso;
}

bool Cancelled(const std::atomic<bool>* cancel)
{
    return cancel && cancel->load(std::memory_order_relaxed);
}

}  // namespace

ClimatologyDatasetLoader::ClimatologyDatasetLoader(
    const std::string& data_directory)
    : m_dataDirectory(ResolveDataDirectory(data_directory))
{
}

DatasetLoadResult ClimatologyDatasetLoader::Load(
    const std::atomic<bool>* cancel) const
{
    DatasetLoadResult result;
    std::shared_ptr<ClimatologyDatasetSnapshot> snapshot(
        new ClimatologyDatasetSnapshot);
    if(!ValidateManifest(m_dataDirectory, *snapshot, result.errors))
        return result;

    for(int month = 0; month < 12 && !Cancelled(cancel); ++month)
        LoadWindMonth(m_dataDirectory, month, snapshot->wind, result.errors);
    if(Cancelled(cancel)) { result.cancelled = true; return result; }
    if(result.errors.empty()) {
        AverageWind(snapshot->wind);
        snapshot->availability.fields[
            static_cast<std::size_t>(DatasetField::Wind)] = true;
    }

    for(int month = 0; month < 12 && !Cancelled(cancel); ++month)
        LoadCurrentMonth(m_dataDirectory, month, snapshot->current, result.errors);
    if(Cancelled(cancel)) { result.cancelled = true; return result; }
    if(result.errors.empty()) {
        AverageCurrent(snapshot->current);
        snapshot->availability.fields[
            static_cast<std::size_t>(DatasetField::Current)] = true;
    }

    LoadPressure(m_dataDirectory, *snapshot, result.errors);
    LoadScalar<std::int8_t>(m_dataDirectory, "seasurfacetemperature",
        DatasetField::SeaSurfaceTemperature,
        Geometry(180, 360, 89.5, 0.5, 1.0, 1.0), -128, true,
        [](std::int8_t value) { return value * 0.2 + 15.0; },
        *snapshot, result.errors);
    LoadScalar<std::int8_t>(m_dataDirectory, "airtemperature",
        DatasetField::AirTemperature,
        Geometry(90, 180, 89.0, 0.5, 2.0, 2.0), -128, true,
        [](std::int8_t value) { return value / 3.0; },
        *snapshot, result.errors);
    LoadScalar<std::uint8_t>(m_dataDirectory, "cloud",
        DatasetField::CloudCover,
        Geometry(90, 180, 89.0, 0.5, 2.0, 2.0), 255, true,
        [](std::uint8_t value) { return value * 0.5; },
        *snapshot, result.errors);
    LoadScalar<std::uint8_t>(m_dataDirectory, "precipitation",
        DatasetField::Precipitation,
        Geometry(72, 144, 90.0, 2.0, 2.5, 2.5), 255, true,
        [](std::uint8_t value) { return value * 0.2; },
        *snapshot, result.errors);
    LoadScalar<std::uint8_t>(m_dataDirectory, "relativehumidity",
        DatasetField::RelativeHumidity,
        Geometry(180, 360, 90.0, 0.5, 1.0, 1.0), 255, true,
        [](std::uint8_t value) { return value / 2.0; },
        *snapshot, result.errors);
    LoadScalar<std::uint8_t>(m_dataDirectory, "lightning",
        DatasetField::Lightning,
        Geometry(180, 360, 90.0, 0.5, 1.0, 1.0), 255, false,
        [](std::uint8_t value) { return static_cast<double>(value); },
        *snapshot, result.errors);
    LoadStaticScalar<std::int8_t>(m_dataDirectory, "seadepth",
        DatasetField::SeaDepth,
        Geometry(180, 360, 90.0, 0.5, 1.0, 1.0), -128,
        [](std::int8_t value) {
            const int index = static_cast<int>(value);
            return index < 0 ? 0 : (index > 39 ? 39 : index);
        }, *snapshot, result.errors);

    const char* cyclone_names[6] = {
        "cyclone-epa", "cyclone-wpa", "cyclone-spa",
        "cyclone-atl", "cyclone-nio", "cyclone-she"};
    for(std::size_t basin = 0; basin < 6; ++basin)
        LoadCyclones(m_dataDirectory, cyclone_names[basin],
                     basin == 2 || basin == 5,
                     snapshot->cyclones.basins[basin], result.errors);
    snapshot->availability.cyclones = snapshot->cyclones.Empty() == false;
    LoadEnso(m_dataDirectory, *snapshot, result.errors);

    if(Cancelled(cancel)) { result.cancelled = true; return result; }
    std::string reason;
    if(result.errors.empty() && !snapshot->IsConsistent(&reason))
        result.errors.push_back(reason);
    if(result.errors.empty())
        result.snapshot = snapshot;
    return result;
}

}  // namespace climatology
