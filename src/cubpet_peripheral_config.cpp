#include "cubpet_peripheral_config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr const char* kDeviceTreeModelPath = "/proc/device-tree/model";

void CopyString(char* dst, size_t dst_size, const std::string& src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    const size_t n = std::min(dst_size - 1, src.size());
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

std::string ParentDir(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    return slash == 0 ? "/" : path.substr(0, slash);
}

std::string ExecutablePath()
{
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return "";
    }
    buf[n] = '\0';
    return buf;
}

std::string PrefixShareBoardDir()
{
    const std::string exe = ExecutablePath();
    if (exe.empty()) {
        return "";
    }
    const std::string bin_dir = ParentDir(exe);
    const std::string prefix = ParentDir(bin_dir);
    return prefix + "/share/ai-cubpet/boards";
}

bool DirectoryExists(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string PathJoin(const std::string& lhs, const std::string& rhs)
{
    if (lhs.empty() || lhs == "/") {
        return lhs + rhs;
    }
    return lhs + "/" + rhs;
}

std::string ReadTextFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    while (!content.empty() &&
        (content.back() == '\0' || content.back() == '\n' ||
        content.back() == '\r' || content.back() == ' ' ||
        content.back() == '\t')) {
        content.pop_back();
    }
    return content;
}

std::vector<std::string> JsonFilesInDir(const std::string& dir)
{
    std::vector<std::string> files;
    DIR* d = opendir(dir.c_str());
    if (!d) {
        return files;
    }
    while (dirent* ent = readdir(d)) {
        const std::string name = ent->d_name;
        if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
            files.push_back(PathJoin(dir, name));
        }
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::string> CandidateBoardDirs(const char* explicit_dir)
{
    std::vector<std::string> dirs;
    auto add = [&dirs](const std::string& dir) {
        if (dir.empty()) {
            return;
        }
        if (std::find(dirs.begin(), dirs.end(), dir) == dirs.end()) {
            dirs.push_back(dir);
        }
    };

    if (explicit_dir && explicit_dir[0] != '\0') {
        add(explicit_dir);
        return dirs;
    }

    if (const char* env = std::getenv("AI_CUBPET_PERIPHERAL_CONFIG_DIR")) {
        add(env);
    }
    add("/etc/ai-cubpet/boards");
    add(PrefixShareBoardDir());
    add("/usr/share/ai-cubpet/boards");
    return dirs;
}

bool ModelMatches(const json& root, const std::string& model)
{
    const auto it = root.find("model_matches");
    if (it == root.end() || !it->is_array()) {
        return false;
    }
    for (const auto& item : *it) {
        if (!item.is_string()) {
            continue;
        }
        const std::string needle = item.get<std::string>();
        if (!needle.empty() && model.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool FitsUint8(int value)
{
    return value >= 0 && value <= std::numeric_limits<uint8_t>::max();
}

uint8_t GetUint8(const json& j, const char* key, uint8_t fallback)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) {
        return fallback;
    }
    const int value = it->get<int>();
    return FitsUint8(value) ? static_cast<uint8_t>(value) : fallback;
}

int GetInt(const json& j, const char* key, int fallback)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

uint32_t GetUint32(const json& j, const char* key, uint32_t fallback)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_unsigned()) {
        return fallback;
    }
    return it->get<uint32_t>();
}

int GetBoolAsInt(const json& j, const char* key, int fallback)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? (it->get<bool>() ? 1 : 0) : fallback;
}

std::string GetString(const json& j, const char* key, const char* fallback = "")
{
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}

void ParseI2cDevice(const json& j, cubpet_i2c_device_config* out)
{
    if (!out || !j.is_object()) {
        return;
    }
    CopyString(out->name, sizeof(out->name), GetString(j, "name", out->name));
    CopyString(out->i2c_dev, sizeof(out->i2c_dev),
        GetString(j, "i2c_dev", out->i2c_dev));
    out->scl_gpio = static_cast<unsigned int>(
        GetInt(j, "scl_gpio", static_cast<int>(out->scl_gpio)));
    out->sda_gpio = static_cast<unsigned int>(
        GetInt(j, "sda_gpio", static_cast<int>(out->sda_gpio)));
    out->i2c_addr = GetUint8(j, "i2c_addr", out->i2c_addr);
    out->demo_block = GetUint8(j, "demo_block", out->demo_block);
    out->irq_gpio = static_cast<unsigned int>(
        GetInt(j, "irq_gpio", static_cast<int>(out->irq_gpio)));
    out->reset_gpio = static_cast<unsigned int>(
        GetInt(j, "reset_gpio", static_cast<int>(out->reset_gpio)));
    out->int1_gpio = static_cast<unsigned int>(
        GetInt(j, "int1_gpio", static_cast<int>(out->int1_gpio)));
}

void ParseMotor(const json& j, cubpet_motor_config* out)
{
    if (!out || !j.is_object()) {
        return;
    }
    CopyString(out->name, sizeof(out->name), GetString(j, "name", out->name));
    out->motor_index = GetUint8(j, "motor_index", out->motor_index);
    out->step_gpio = GetUint8(j, "step_gpio", out->step_gpio);
    out->dir_gpio = GetUint8(j, "dir_gpio", out->dir_gpio);
    out->enable_gpio = GetUint8(j, "enable_gpio", out->enable_gpio);
    out->stop_gpio = GetUint8(j, "stop_gpio", out->stop_gpio);
    out->current_position = GetInt(j, "current_position", out->current_position);
    out->constant_range = GetInt(j, "constant_range", out->constant_range);
    out->gpio_max_steps = GetInt(j, "gpio_max_steps", out->gpio_max_steps);
    out->enable_gpio_level = GetBoolAsInt(j, "enable_gpio_level",
        out->enable_gpio_level);
    out->dir_gpio_left_level = GetBoolAsInt(j, "dir_gpio_left_level",
        out->dir_gpio_left_level);
    out->stop_gpio_active_level = GetBoolAsInt(j, "stop_gpio_active_level",
        out->stop_gpio_active_level);
    out->range_steps = GetInt(j, "range_steps", out->range_steps);
}

void ParseGpioInput(const json& j, cubpet_gpio_input_config* out)
{
    if (!out || !j.is_object()) {
        return;
    }
    CopyString(out->name, sizeof(out->name), GetString(j, "name", out->name));
    CopyString(out->role, sizeof(out->role), GetString(j, "role", out->role));
    CopyString(out->chip_name, sizeof(out->chip_name),
        GetString(j, "chip_name", out->chip_name));
    out->line_offset = static_cast<unsigned int>(
        GetInt(j, "line_offset", static_cast<int>(out->line_offset)));
    out->active_high = GetBoolAsInt(j, "active_high", out->active_high);
}

int ParseConfigJson(const json& root, cubpet_peripheral_config* config)
{
    if (!root.is_object() || !config) {
        return -EINVAL;
    }

    CopyString(config->board_name, sizeof(config->board_name),
        GetString(root, "board_name", config->board_name));

    if (const auto it = root.find("motors");
        it != root.end() && it->is_array()) {
        config->motor_count = 0;
        for (const auto& item : *it) {
            if (config->motor_count >= CUBPET_PERIPHERAL_MAX_MOTORS) {
                break;
            }
            ParseMotor(item, &config->motors[config->motor_count]);
            config->motor_count++;
        }
    }

    if (const auto it = root.find("i2c_devices");
        it != root.end() && it->is_object()) {
        if (const auto jt = it->find("nfc"); jt != it->end()) {
            ParseI2cDevice(*jt, &config->nfc);
        }
        if (const auto jt = it->find("light_sensor"); jt != it->end()) {
            ParseI2cDevice(*jt, &config->light_sensor);
        }
        if (const auto jt = it->find("g_sensor"); jt != it->end()) {
            ParseI2cDevice(*jt, &config->g_sensor);
        }
    }

    if (const auto it = root.find("gpio_inputs");
        it != root.end() && it->is_array()) {
        config->gpio_input_count = 0;
        for (const auto& item : *it) {
            if (config->gpio_input_count >= CUBPET_PERIPHERAL_MAX_GPIO_INPUTS) {
                break;
            }
            ParseGpioInput(item, &config->gpio_inputs[config->gpio_input_count]);
            config->gpio_input_count++;
        }
    }

    if (const auto it = root.find("pm"); it != root.end() && it->is_object()) {
        CopyString(config->pm.charger_node, sizeof(config->pm.charger_node),
        GetString(*it, "charger_node", config->pm.charger_node));
        CopyString(config->pm.capacity_node, sizeof(config->pm.capacity_node),
        GetString(*it, "capacity_node", config->pm.capacity_node));
    }

    if (const auto it = root.find("fan"); it != root.end() && it->is_object()) {
        config->fan.gpio = static_cast<unsigned int>(
            GetInt(*it, "gpio", static_cast<int>(config->fan.gpio)));
        config->fan.period = GetUint32(*it, "period", config->fan.period);
        config->fan.duty_cycle = GetUint32(*it, "duty_cycle",
        config->fan.duty_cycle);
    }

    return 0;
}

int LoadJsonFile(const std::string& path, json* root)
{
    if (!root) {
        return -EINVAL;
    }
    std::ifstream in(path);
    if (!in.good()) {
        return -ENOENT;
    }
    try {
        *root = json::parse(in, nullptr, true, true);
    } catch (const std::exception&) {
        return -EINVAL;
    }
    return 0;
}

int LoadMatchingConfig(const std::string& model,
    const std::string& path,
    cubpet_peripheral_config* config)
{
    json root;
    const int rc = LoadJsonFile(path, &root);
    if (rc != 0) {
        return rc;
    }
    if (!ModelMatches(root, model)) {
        return -ENOENT;
    }
    cubpet_peripheral_config_init_defaults(config);
    return ParseConfigJson(root, config);
}

int LoadExplicitConfig(const std::string& path, cubpet_peripheral_config* config)
{
    json root;
    const int rc = LoadJsonFile(path, &root);
    if (rc != 0) {
        return rc;
    }
    cubpet_peripheral_config_init_defaults(config);
    return ParseConfigJson(root, config);
}

}  // namespace

extern "C" void cubpet_peripheral_config_init_defaults(
    cubpet_peripheral_config* config)
{
    if (!config) {
        return;
    }
    std::memset(config, 0, sizeof(*config));
    config->nfc.i2c_addr = 0;
    config->light_sensor.i2c_addr = 0;
    config->g_sensor.i2c_addr = 0;
}

extern "C" int cubpet_peripheral_config_load_for_model(
    const char* model,
    const char* board_config_dir,
    cubpet_peripheral_config* config)
{
    if (!model || !config) {
        return -EINVAL;
    }

    const char* explicit_config = std::getenv("AI_CUBPET_PERIPHERAL_CONFIG");
    if (explicit_config && explicit_config[0] != '\0') {
        return LoadExplicitConfig(explicit_config, config);
    }

    for (const auto& dir : CandidateBoardDirs(board_config_dir)) {
        if (!DirectoryExists(dir)) {
            continue;
        }
        for (const auto& file : JsonFilesInDir(dir)) {
            const int rc = LoadMatchingConfig(model, file, config);
            if (rc == 0) {
                return 0;
            }
        }
    }
    return -ENOENT;
}

extern "C" int cubpet_peripheral_config_load_auto(
    cubpet_peripheral_config* config)
{
    if (!config) {
        return -EINVAL;
    }

    const std::string model = ReadTextFile(kDeviceTreeModelPath);
    if (model.empty()) {
        return -ENOENT;
    }
    return cubpet_peripheral_config_load_for_model(model.c_str(), nullptr, config);
}

extern "C" const cubpet_motor_config* cubpet_peripheral_find_motor(
    const cubpet_peripheral_config* config,
    const char* name)
{
    if (!config || !name) {
        return nullptr;
    }
    for (size_t i = 0; i < config->motor_count; ++i) {
        if (std::strcmp(config->motors[i].name, name) == 0) {
            return &config->motors[i];
        }
    }
    return nullptr;
}
