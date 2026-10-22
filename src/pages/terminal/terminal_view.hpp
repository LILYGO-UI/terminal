#ifndef LILYGO_UI_TERMINAL_PAGES_TERMINAL_VIEW_HPP
#define LILYGO_UI_TERMINAL_PAGES_TERMINAL_VIEW_HPP

#include "pages/terminal/terminal_view_model.hpp"

#include <lvgl.h>

#include <cstdint>
#include <vector>

class TerminalView {
public:
    explicit TerminalView(TerminalViewModel &view_model) noexcept;

    lv_obj_t *create(lv_obj_t *parent, const char *application_name, void (*request_exit)(void *),
                     void *exit_user_data);
    void destroy() noexcept;

private:
    enum class KeyboardMode { lower, upper, number, symbol };

    static void special_key_event(lv_event_t *event);
    static void special_button_event(lv_event_t *event);
    static void exit_button_event(lv_event_t *event);
    static void special_close_event(lv_event_t *event);
    static void keyboard_event(lv_event_t *event);
    static void hardware_key_event(lv_event_t *event);
    static lv_key_t remap_keyboard_key(lv_indev_t *indev, lv_key_t key);
    static void size_changed_event(lv_event_t *event);
    static void timer_tick(lv_timer_t *timer);

    void build_header(const char *application_name);
    void build_terminal();
    void build_special_keys();
    void build_keyboard();
    void open_special_popup();
    void close_special_popup();
    void set_keyboard_mode(KeyboardMode mode);
    void handle_keyboard_text(const char *text);
    void apply_layout() noexcept;
    void apply_terminal_font(int font_size);
    void bind_keyboard_indev() noexcept;
    void unbind_keyboard_indev() noexcept;
    void sync_hardware_keyboard();
    void sync_state(bool force = false);

    TerminalViewModel &view_model_;
    lv_obj_t *surface_      = nullptr;
    lv_obj_t *header_       = nullptr;
    lv_obj_t *title_        = nullptr;
    lv_obj_t *status_label_ = nullptr;
    lv_obj_t *work_area_    = nullptr;
    lv_obj_t *terminal_box_ = nullptr;
    lv_obj_t *special_button_ = nullptr;
    lv_obj_t *exit_button_    = nullptr;
    lv_obj_t *special_popup_  = nullptr;
    lv_obj_t *special_window_ = nullptr;
    lv_obj_t *special_close_  = nullptr;
    lv_obj_t *special_keys_   = nullptr;
    lv_obj_t *keyboard_       = nullptr;
    lv_timer_t *timer_      = nullptr;
    std::vector<lv_obj_t *> row_labels_;
    KeyboardMode keyboard_mode_              = KeyboardMode::lower;
    std::uint64_t rendered_revision_         = 0;
    int rendered_font_size_                  = 0;
    bool applying_layout_                    = false;
    bool keyboard_present_                   = false;
    bool hardware_keyboard_                  = false;
    std::uint32_t hardware_keyboard_scan_at_ = 0;
    void (*request_exit_)(void *)            = nullptr;
    void *exit_user_data_                    = nullptr;
};

#endif
