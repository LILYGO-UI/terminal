#ifndef LILYGO_UI_TERMINAL_PAGES_TERMINAL_VIEW_MODEL_HPP
#define LILYGO_UI_TERMINAL_PAGES_TERMINAL_VIEW_MODEL_HPP

#include "domain/terminal_session.hpp"
#include "pages/terminal/terminal_model.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct TerminalViewState {
    std::vector<std::string> rows;
    std::string status;
    std::size_t row_count    = 0;
    std::size_t column_count = 0;
    int font_size            = TerminalModel::font_sizes[TerminalModel::default_font_level];
    std::uint64_t revision   = 0;
    bool running             = false;
    bool ctrl_armed          = false;
    bool preference_saved    = true;
};

class TerminalViewModel {
public:
    explicit TerminalViewModel(std::filesystem::path settings_path = {});
    ~TerminalViewModel();

    TerminalViewModel(const TerminalViewModel &)            = delete;
    TerminalViewModel &operator=(const TerminalViewModel &) = delete;

    bool start();
    void stop() noexcept;
    void send_key(TerminalKey key);
    void send_text(std::string_view text);
    void set_geometry(std::size_t rows, std::size_t columns);
    [[nodiscard]] TerminalViewState state();

private:
    void apply(TerminalCommand command);

    TerminalModel model_;
    TerminalSession session_;
    std::uint64_t ui_revision_ = 1;
};

#endif
