/**
 * @file config_loader.hpp
 * @author Ashot Vardanian
 *
 * @brief DBMS configurations
 */
#pragma once

#include <limits>            // `std::numeric_limit`
#include <string>            // `std::string`
#include <vector>            // `std::vector`
#include <nlohmann/json.hpp> // `nlohmann::json`

#include "ukv/cpp/status.hpp" // `status_t`

namespace unum::ukv {

/**
 * @brief Storage disk configuration
 *
 * @path: Data directory path on the disk
 * @max_size: Space limit used by DBMS
 */
struct disk_config_t {
    static constexpr size_t unlimited_space_k = std::numeric_limits<size_t>::max(); // Not limited by software

    std::string path;
    size_t max_size = unlimited_space_k;
};

/**
 * @brief DBMS configuration
 *
 * @directory: Main path where DB stores metadata, e.g schema, log, etc.
 * @data_directories: Storage paths where DB stores data.
 * @engine_config_path: Engine specific config file path.
 */
struct config_t {
    std::string directory;
    std::vector<disk_config_t> data_directories;
    std::string engine_config_path;
};

/**
 * @brief DBMS configurations loader
 */
class config_loader_t {
  public:
    using json_t = nlohmann::json;

  public:
    static constexpr uint8_t current_major_version_k = 1;
    static constexpr uint8_t current_minor_version_k = 0;

  public:
    static inline status_t load_from_json(json_t const& json, config_t& config);
    static inline status_t load_from_json_string(std::string const& str_json, config_t& config);

    static inline status_t save_to_json(config_t const& config,json_t& json);
    static inline status_t save_to_json_string(config_t const& config, std::string& str_json);

  private:
    static inline status_t validate_config(json_t const& json) noexcept;

    static inline bool parse_version(std::string const& str_version, uint8_t& major, uint8_t& minor) noexcept;
    static inline bool parse_volume(json_t const& json, std::string const& key, size_t& bytes) noexcept;
    static inline bool parse_bytes(std::string const& str, size_t& bytes) noexcept;
};

inline status_t config_loader_t::load_from_json(json_t const& json, config_t& config) {

    try {
        auto status = validate_config(json);
        if (!status)
            return status;

        config.directory = json.value("directory", "./tmp/ukv/");
        config.engine_config_path = json.value("engine_config_path", "");

        if (json.contains("data_directories")) {
            auto j_disks = json["data_directories"];
            if (j_disks.is_array()) {
                for (auto j_disk : j_disks) {
                    disk_config_t disk;
                    disk.path = j_disk.value("path", "");
                    disk.max_size = j_disk.value("max_size", disk_config_t::unlimited_space_k);
                    if (disk.path.empty())
                        return "Empty data directory path";
                    if (!parse_volume(j_disk, "max_size", disk.max_size))
                        return "Invalid volume format";
                    config.data_directories.push_back(std::move(disk));
                }
            }
            else
                return "Invalid data directories config";
        }
    }
    catch (...) {
        return "Exception occurred: Invalid json config file";
    }

    return {};
}

inline status_t config_loader_t::load_from_json_string(std::string const& str_json, config_t& config) {
    auto json = json_t::parse(str_json);
    return load_from_json(json, config);
}

inline status_t config_loader_t::save_to_json(config_t const& config, json_t& json) {

    json.clear();
    json["directory"] = config.directory;
    json["engine_config_path"] = config.engine_config_path;

    std::vector<json_t> j_data_directories;
    for (auto const& directory : config.data_directories) {
        json_t j_directory;
        j_directory["path"] = directory.path;
        j_directory["max_size"] = directory.max_size;
        j_data_directories.push_back(j_directory);
    }
    json["data_directories"] = j_data_directories;

    return {};
}
    
inline status_t config_loader_t::save_to_json_string(config_t const& config, std::string& str_json) {
    json_t json;
    auto status = save_to_json(config, json);
    if (!status)
        return status;
    str_json = json.to_string();
    return true;
}

inline status_t config_loader_t::validate_config(json_t const& json) noexcept {

    std::string version = json.value("version", std::string());
    uint8_t major_version = 0;
    uint8_t minor_version = 0;
    if (!parse_version(version.c_str(), major_version, minor_version))
        return "Invalid version format";
    if (major_version != current_major_version_k || minor_version != current_minor_version_k)
        return "Version not supported";
    return {};
}

inline bool config_loader_t::parse_version(std::string const& str_version, uint8_t& major, uint8_t& minor) noexcept {

    int mj = 0;
    int mn = 0;
    try {
        size_t pos = 0;
        std::string str = str_version.c_str();
        mj = std::stoul(str, &pos);
        if (pos == str.size())
            return false;
        str = str.substr(++pos);
        mn = std::stoul(str, &pos);
        if (pos < str.size())
            return false;
    }
    catch (...) {
        return false;
    }
    if (mj < 0 || mn < 0 || mj > int(std::numeric_limits<uint8_t>::max()) ||
        mn > int(std::numeric_limits<uint8_t>::max()))
        return false;

    major = static_cast<uint8_t>(mj);
    minor = static_cast<uint8_t>(mn);
    return true;
}

inline bool config_loader_t::parse_volume(json_t const& json, std::string const& key, size_t& bytes) noexcept {

    auto it = json.find(key.c_str());
    if (it == json.end())
        return true; // Skip if not exist

    switch (it->type()) {
    case json_t::value_t::number_unsigned: {
        bytes = it.value();
        return true;
    }
    case json_t::value_t::string: {
        std::string value = std::string(it.value()).c_str();
        return parse_bytes(value, bytes);
    }
    default: break;
    }

    return false;
}

inline bool config_loader_t::parse_bytes(std::string const& str, size_t& bytes) noexcept {

    std::stringstream ss(str.c_str());

    // Parse number
    double number = 0.0;
    if (str.rfind('.', 0) == 0 || (ss >> number).fail() || std::isnan(number))
        return false;

    // Parse unite
    std::string metric;
    if (ss >> metric) {
        if (metric == "KB")
            number *= 1024ull;
        else if (metric == "MB")
            number *= 1024 * 1024ull;
        else if (metric == "GB")
            number *= 1024 * 1024 * 1024ull;
        else if (metric == "TB")
            number *= 1024 * 1024 * 1024 * 1024ull;
        else if (metric != "B" || str.find('.') != std::string::npos)
            return false;
    }
    else if (str.find('.') != std::string::npos)
        return false;

    if (!ss.eof())
        return false;
    if (std::isnan(number) || number > std::numeric_limits<size_t>::max())
        return false;

    bytes = static_cast<size_t>(number);
    return true;
}

} // namespace unum::ukv
