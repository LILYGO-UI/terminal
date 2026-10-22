#include "app.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

constexpr int kPartialRows                  = 80;
constexpr std::uint32_t kTerminalBackground = 0xffffff;
constexpr std::uint32_t kTerminalText       = 0x000000;

std::vector<lv_color32_t> framebuffer;
int display_width = 0;

void flush_display(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels)
{
    const int width    = area->x2 - area->x1 + 1;
    const auto *source = reinterpret_cast<const lv_color32_t *>(pixels);
    for (int y = area->y1; y <= area->y2; ++y) {
        const auto source_offset = static_cast<std::size_t>(y - area->y1) * static_cast<std::size_t>(width);
        const auto destination_offset =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(display_width) + static_cast<std::size_t>(area->x1);
        std::copy_n(source + source_offset, width, framebuffer.begin() + destination_offset);
    }
    lv_display_flush_ready(display);
}

bool write_ppm(const char *path, int width, int height)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto &pixel : framebuffer) {
        file.put(static_cast<char>(pixel.red));
        file.put(static_cast<char>(pixel.green));
        file.put(static_cast<char>(pixel.blue));
    }
    return file.good();
}

void request_exit_for_test(void *user_data)
{
    *static_cast<bool *>(user_data) = true;
}

bool colors_differ(const lv_color32_t &left, const lv_color32_t &right)
{
    return left.red != right.red || left.green != right.green || left.blue != right.blue || left.alpha != right.alpha;
}

void advance_ui(int steps = 12)
{
    for (int attempt = 0; attempt < steps; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        lv_tick_inc(20);
        (void)lv_timer_handler();
    }
}

bool vertically_separate(lv_obj_t *upper, lv_obj_t *lower)
{
    lv_area_t upper_area{};
    lv_area_t lower_area{};
    lv_obj_get_coords(upper, &upper_area);
    lv_obj_get_coords(lower, &lower_area);
    return upper_area.y2 < lower_area.y1;
}

}  // namespace

int main(int argc, char **argv)
{
    assert(argc == 4);
    const int width  = std::atoi(argv[1]);
    const int height = std::atoi(argv[2]);
    assert(width >= 320 && height >= 240);

    const auto settings = std::filesystem::temp_directory_path() /
                          ("lilygo-ui-terminal-render-" + std::to_string(static_cast<long long>(getpid()))) /
                          "settings.conf";
    (void)setenv("LILYGO_UI_TERMINAL_CONFIG", settings.c_str(), 1);
    (void)setenv("LILYGO_UI_TERMINAL_HOME", settings.parent_path().c_str(), 1);
    (void)setenv("LILYGO_UI_TERMINAL_PS1", "device:~$ ", 1);

    display_width = width;
    framebuffer.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    std::vector<lv_color32_t> draw_buffer(static_cast<std::size_t>(width) * kPartialRows);

    lv_init();
    auto *input_group = lv_group_create();
    assert(input_group);
    lv_group_set_default(input_group);
    auto *display = lv_display_create(width, height);
    assert(display);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                           static_cast<std::uint32_t>(draw_buffer.size() * sizeof(lv_color32_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_default(display);

    cm0_app_context_t context{};
    context.root           = lv_screen_active();
    bool exit_requested    = false;
    context.request_exit   = request_exit_for_test;
    context.user_data      = &exit_requested;
    const auto *descriptor = cm0_app_get_descriptor();
    descriptor->open(&context);
    lv_obj_update_layout(context.root);
    advance_ui();

    auto *surface   = lv_obj_find_by_name(context.root, "terminal_surface");
    auto *header    = lv_obj_find_by_name(context.root, "terminal_header");
    auto *title     = lv_obj_find_by_name(context.root, "terminal_title");
    auto *status    = lv_obj_find_by_name(context.root, "terminal_status");
    auto *output    = lv_obj_find_by_name(context.root, "terminal_output");
    auto *special_button = lv_obj_find_by_name(context.root, "terminal_special_button");
    auto *exit_button    = lv_obj_find_by_name(context.root, "terminal_exit_button");
    auto *special_popup  = lv_obj_find_by_name(context.root, "terminal_special_popup");
    auto *special        = lv_obj_find_by_name(context.root, "terminal_special_keys");
    auto *special_close  = lv_obj_find_by_name(context.root, "terminal_special_close");
    auto *keyboard  = lv_obj_find_by_name(context.root, "terminal_keyboard");
    auto *first_row = lv_obj_find_by_name(context.root, "terminal_row_0");
    assert(surface && header && title && status && output && special_button && exit_button && special_popup && special && special_close &&
           keyboard && first_row);
    assert(std::string(lv_label_get_text(title)) == "Terminal");
    assert(lv_color_eq(lv_obj_get_style_bg_color(output, LV_PART_MAIN), lv_color_hex(kTerminalBackground)));
    assert(lv_color_eq(lv_obj_get_style_text_color(first_row, LV_PART_MAIN), lv_color_hex(kTerminalText)));
    assert(lv_obj_get_width(output) >= 120);
    assert(lv_obj_get_height(output) >= 90);
    assert(lv_obj_get_width(keyboard) == width);
    assert(!lv_obj_has_flag(special_button, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_parent(special_button) == header);
    assert(lv_obj_get_width(special_button) == 42);
    assert(lv_obj_get_height(special_button) == 42);
    assert(lv_obj_get_parent(exit_button) == header);
    assert(lv_obj_get_width(exit_button) == 42);
    assert(lv_obj_get_height(exit_button) == 42);
    lv_area_t special_button_area{};
    lv_area_t exit_button_area{};
    lv_obj_get_coords(special_button, &special_button_area);
    lv_obj_get_coords(exit_button, &exit_button_area);
    assert(special_button_area.x2 < exit_button_area.x1);
    assert(lv_obj_has_flag(special_popup, LV_OBJ_FLAG_HIDDEN));
    // The SDL host keyboard is not a hardware TCA8418, so both software
    // key surfaces remain available in simulator renders.
    assert(!lv_obj_has_flag(special, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));

    assert(vertically_separate(header, keyboard));

    lv_obj_send_event(special_button, LV_EVENT_CLICKED, nullptr);
    advance_ui(2);
    assert(!lv_obj_has_flag(special_popup, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(special, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_width(special) >= 240);

    lv_buttonmatrix_set_selected_button(special, 5);
    lv_obj_send_event(special, LV_EVENT_VALUE_CHANGED, nullptr);
    advance_ui(2);
    assert(std::string(lv_label_get_text(status)).find("22 px") != std::string::npos);
    assert(!lv_obj_has_flag(special_popup, LV_OBJ_FLAG_HIDDEN));

    lv_buttonmatrix_set_selected_button(special, 0);
    lv_obj_send_event(special, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(lv_buttonmatrix_has_button_ctrl(special, 0, LV_BUTTONMATRIX_CTRL_CHECKED));
    lv_buttonmatrix_set_selected_button(special, 0);
    lv_obj_send_event(special, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(!lv_buttonmatrix_has_button_ctrl(special, 0, LV_BUTTONMATRIX_CTRL_CHECKED));

    lv_obj_send_event(special_close, LV_EVENT_CLICKED, nullptr);
    assert(lv_obj_has_flag(special_popup, LV_OBJ_FLAG_HIDDEN));
    auto *group = lv_group_get_default();
    assert(group && lv_group_get_focused(group) == surface);

    lv_tick_inc(20);
    lv_refr_now(display);
    const bool has_contrast = std::any_of(framebuffer.cbegin() + 1, framebuffer.cend(),
                                          [](const auto &pixel) { return colors_differ(pixel, framebuffer[0]); });
    assert(has_contrast);
    assert(write_ppm(argv[3], width, height));

    lv_obj_send_event(exit_button, LV_EVENT_CLICKED, nullptr);
    assert(exit_requested);

    descriptor->close();
    lv_group_set_default(nullptr);
    lv_group_delete(input_group);
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();

    std::error_code error;
    std::filesystem::remove_all(settings.parent_path(), error);
    return 0;
}
