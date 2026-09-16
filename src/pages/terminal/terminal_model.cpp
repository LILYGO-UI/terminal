#include "pages/terminal/terminal_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include <pwd.h>
#include <unistd.h>

namespace {

constexpr const char *kSettingsFileName = "settings.conf";

std::filesystem::path home_directory()
{
    if (const char *home = std::getenv("HOME")) {
        if (*home) return home;
    }
    if (const auto *entry = getpwuid(getuid())) {
        if (entry->pw_dir && *entry->pw_dir) return entry->pw_dir;
    }
    return "/tmp";
}

std::string trim(std::string value)
{
    const auto not_space = [](unsigned char character) { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

}  // namespace

TerminalModel::TerminalModel(std::filesystem::path settings_path)
    : settings_path_(settings_path.empty() ? default_settings_path() : std::move(settings_path))
{
    load_font_level();
}

int TerminalModel::font_level() const noexcept
{
    return font_level_;
}

int TerminalModel::font_size() const noexcept
{
    return font_sizes[static_cast<std::size_t>(font_level_)];
}

bool TerminalModel::ctrl_armed() const noexcept
{
    return ctrl_armed_;
}

bool TerminalModel::preference_saved() const noexcept
{
    return preference_saved_;
}

const std::filesystem::path &TerminalModel::settings_path() const noexcept
{
    return settings_path_;
}

TerminalCommand TerminalModel::handle_key(TerminalKey key)
{
    TerminalCommand command;
    if (key == TerminalKey::ctrl) {
        ctrl_armed_ = !ctrl_armed_;
        return command;
    }

    const bool use_ctrl = ctrl_armed_;
    ctrl_armed_         = false;
    switch (key) {
        case TerminalKey::escape:
            command.bytes = "\x1b";
            break;
        case TerminalKey::tab:
            command.bytes = "\t";
            break;
        case TerminalKey::enter:
            command.bytes = "\r";
            break;
        case TerminalKey::letter_c:
            command.bytes = use_ctrl ? std::string(1, '\x03') : "c";
            break;
        case TerminalKey::letter_d:
            command.bytes = use_ctrl ? std::string(1, '\x04') : "d";
            break;
        case TerminalKey::letter_l:
            command.bytes        = use_ctrl ? std::string(1, '\x0c') : "l";
            command.clear_screen = use_ctrl;
            break;
        case TerminalKey::arrow_up:
            command.bytes = "\x1b[A";
            break;
        case TerminalKey::arrow_down:
            command.bytes = "\x1b[B";
            break;
        case TerminalKey::arrow_left:
            command.bytes = "\x1b[D";
            break;
        case TerminalKey::arrow_right:
            command.bytes = "\x1b[C";
            break;
        case TerminalKey::backspace:
            command.bytes = "\x7f";
            break;
        case TerminalKey::clear:
            command.clear_screen = true;
            break;
        case TerminalKey::restart:
            command.restart_shell = true;
            command.clear_screen  = true;
            break;
        case TerminalKey::font_down:
            command.font_changed = adjust_font_level(-1);
            break;
        case TerminalKey::font_up:
            command.font_changed = adjust_font_level(1);
            break;
        case TerminalKey::ctrl:
            break;
    }
    return command;
}

TerminalCommand TerminalModel::handle_text(std::string_view text)
{
    TerminalCommand command;
    if (text.empty()) return command;

    if (ctrl_armed_ && text.size() == 1 && std::isalpha(static_cast<unsigned char>(text.front()))) {
        command.bytes.assign(1, static_cast<char>(std::toupper(static_cast<unsigned char>(text.front())) & 0x1f));
    } else {
        command.bytes.assign(text);
    }
    ctrl_armed_ = false;
    return command;
}

std::filesystem::path TerminalModel::default_settings_path()
{
    if (const char *override_path = std::getenv("LILYGO_UI_TERMINAL_CONFIG")) {
        if (*override_path) return override_path;
    }
    if (const char *config_home = std::getenv("XDG_CONFIG_HOME")) {
        if (*config_home) return std::filesystem::path(config_home) / "lilygo-ui-terminal" / kSettingsFileName;
    }
    return home_directory() / ".config" / "lilygo-ui-terminal" / kSettingsFileName;
}

void TerminalModel::load_font_level()
{
    std::ifstream input(settings_path_);
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos || trim(line.substr(0, separator)) != "font_level") continue;
        const std::string value = trim(line.substr(separator + 1));
        try {
            std::size_t consumed = 0;
            const int level      = std::stoi(value, &consumed);
            if (consumed == value.size()) font_level_ = std::clamp(level, minimum_font_level, maximum_font_level);
        } catch (...) {
            font_level_ = default_font_level;
        }
        break;
    }
}

bool TerminalModel::adjust_font_level(int delta)
{
    const int next = std::clamp(font_level_ + delta, minimum_font_level, maximum_font_level);
    if (next == font_level_) return false;
    font_level_       = next;
    preference_saved_ = save_font_level();
    return true;
}

bool TerminalModel::save_font_level() const
{
    std::error_code error;
    const auto parent = settings_path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) return false;
    }

    auto temporary_path = settings_path_;
    temporary_path += ".tmp";
    {
        std::ofstream output(temporary_path, std::ios::trunc);
        if (!output) return false;
        output << "# lilygo-ui-terminal persistent settings\n";
        output << "font_level=" << font_level_ << '\n';
        if (!output) return false;
    }

    std::filesystem::rename(temporary_path, settings_path_, error);
    if (!error) return true;
    std::filesystem::remove(temporary_path, error);
    return false;
}
