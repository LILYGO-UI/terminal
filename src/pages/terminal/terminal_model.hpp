#ifndef LILYGO_UI_TERMINAL_PAGES_TERMINAL_MODEL_HPP
#define LILYGO_UI_TERMINAL_PAGES_TERMINAL_MODEL_HPP

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

enum class TerminalKey {
    ctrl,
    escape,
    tab,
    enter,
    letter_c,
    letter_d,
    letter_l,
    arrow_up,
    arrow_down,
    arrow_left,
    arrow_right,
    backspace,
    clear,
    restart,
    font_down,
    font_up,
};

struct TerminalCommand {
    std::string bytes;
    bool clear_screen  = false;
    bool restart_shell = false;
    bool font_changed  = false;
};

class TerminalModel {
public:
    static constexpr int minimum_font_level = 0;
    static constexpr int maximum_font_level = 2;
    static constexpr int default_font_level = 1;
    inline static constexpr std::array<int, 3> font_sizes{14, 18, 22};

    explicit TerminalModel(std::filesystem::path settings_path = {});

    [[nodiscard]] int font_level() const noexcept;
    [[nodiscard]] int font_size() const noexcept;
    [[nodiscard]] bool ctrl_armed() const noexcept;
    [[nodiscard]] bool preference_saved() const noexcept;
    [[nodiscard]] const std::filesystem::path &settings_path() const noexcept;

    TerminalCommand handle_key(TerminalKey key);
    TerminalCommand handle_text(std::string_view text);

    static std::filesystem::path default_settings_path();

private:
    void load_font_level();
    bool adjust_font_level(int delta);
    bool save_font_level() const;

    std::filesystem::path settings_path_;
    int font_level_        = default_font_level;
    bool ctrl_armed_       = false;
    bool preference_saved_ = true;
};

#endif
