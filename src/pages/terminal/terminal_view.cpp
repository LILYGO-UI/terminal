#include "pages/terminal/terminal_view.hpp"

#include "components/components.hpp"

#include <cm0/input.h>
#include <cm0/typography.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace {

const char *const kSpecialPopupMap[] = {
    "Ctrl", "Esc", "Tab", "Enter", "\n",
    "Font -", "Font +", "Clear", "Restart", "\n",
    "C",    "D",   "L",     "Backspace", "\n",
    LV_SYMBOL_LEFT, LV_SYMBOL_UP, LV_SYMBOL_DOWN, LV_SYMBOL_RIGHT, "",
};

const char *const kKeyboardLowerMap[] = {
    "q", "w", "e",  "r",   "t", "y", "u", "i", "o", "p", "\n", "a",  "s",  "d",   "f",   "g",     "h",     "j",
    "k", "l", "\n", "ABC", "z", "x", "c", "v", "b", "n", "m",  "BS", "\n", "123", "#+=", "Space", "Enter", "",
};

const char *const kKeyboardUpperMap[] = {
    "Q", "W", "E",  "R",   "T", "Y", "U", "I", "O", "P", "\n", "A",  "S",  "D",   "F",   "G",     "H",     "J",
    "K", "L", "\n", "abc", "Z", "X", "C", "V", "B", "N", "M",  "BS", "\n", "123", "#+=", "Space", "Enter", "",
};

const char *const kKeyboardNumberMap[] = {
    "1", "2", "3", "4",  "5",  "6",   "7", "8", "9", "0", "\n", "-",  "/",  ":",   ";",     "(",     ")",
    "$", "&", "@", "\"", "\n", "#+=", ".", ",", "?", "!", "'",  "BS", "\n", "abc", "Space", "Enter", "",
};

const char *const kKeyboardSymbolMap[] = {
    "[", "]", "{",  "}",  "#",   "%", "^", "*", "+", "=", "\n", "_",  "\\",  "|",     "~",     "<",
    ">", "`", "BS", "\n", "123", "-", "/", ":", ";", "!", "?",  "\n", "abc", "Space", "Enter", "",
};

/* LVGL consumes its navigation key values before sending them to the focused
 * object. Remap terminal controls to private values so they reach the shell
 * instead of moving focus or triggering AppKit's global Escape/Enter actions. */
constexpr lv_key_t kTerminalEscape    = static_cast<lv_key_t>(0x01);
constexpr lv_key_t kTerminalTab       = static_cast<lv_key_t>(0x04);
constexpr lv_key_t kTerminalBacktab   = static_cast<lv_key_t>(0x05);
constexpr lv_key_t kTerminalEnter     = static_cast<lv_key_t>(0x06);
constexpr lv_key_t kTerminalBackspace = static_cast<lv_key_t>(0x07);
constexpr lv_key_t kTerminalUp        = static_cast<lv_key_t>(0x0c);
constexpr lv_key_t kTerminalDown      = static_cast<lv_key_t>(0x0d);
constexpr lv_key_t kTerminalLeft      = static_cast<lv_key_t>(0x0e);
constexpr lv_key_t kTerminalRight     = static_cast<lv_key_t>(0x0f);
constexpr lv_key_t kTerminalHome      = static_cast<lv_key_t>(0x10);
constexpr lv_key_t kTerminalEnd       = static_cast<lv_key_t>(0x15);

std::optional<TerminalKey> special_key_for_label(std::string_view label)
{
    if (label == "Ctrl") return TerminalKey::ctrl;
    if (label == "Esc") return TerminalKey::escape;
    if (label == "Tab") return TerminalKey::tab;
    if (label == "Enter") return TerminalKey::enter;
    if (label == "Font -") return TerminalKey::font_down;
    if (label == "Font +") return TerminalKey::font_up;
    if (label == "Clear") return TerminalKey::clear;
    if (label == "Restart") return TerminalKey::restart;
    if (label == "C") return TerminalKey::letter_c;
    if (label == "D") return TerminalKey::letter_d;
    if (label == "L") return TerminalKey::letter_l;
    if (label == "Backspace") return TerminalKey::backspace;
    if (label == LV_SYMBOL_LEFT) return TerminalKey::arrow_left;
    if (label == LV_SYMBOL_UP) return TerminalKey::arrow_up;
    if (label == LV_SYMBOL_DOWN) return TerminalKey::arrow_down;
    if (label == LV_SYMBOL_RIGHT) return TerminalKey::arrow_right;
    return std::nullopt;
}

void prepare_container(lv_obj_t *object)
{
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace

TerminalView::TerminalView(TerminalViewModel &view_model) noexcept : view_model_(view_model)
{
}

lv_obj_t *TerminalView::create(lv_obj_t *parent, const char *application_name, void (*request_exit)(void *),
                               void *exit_user_data)
{
    request_exit_   = request_exit;
    exit_user_data_ = exit_user_data;
    surface_ = components::create_page(parent);
    lv_obj_set_name_static(surface_, "terminal_surface");
    lv_obj_add_event_cb(surface_, size_changed_event, LV_EVENT_SIZE_CHANGED, this);
    lv_obj_add_event_cb(surface_, hardware_key_event, LV_EVENT_KEY, this);
    cm0_input_set_focus_scope(surface_);
    keyboard_present_          = cm0_input_keyboard_present();
    hardware_keyboard_         = keyboard_present_ && cm0_input_hardware_keyboard_present();
    hardware_keyboard_scan_at_ = lv_tick_get();
    if (keyboard_present_) {
        /* The terminal has a custom button-matrix keyboard rather than an
         * lv_keyboard object. Give the page the runtime keyboard focus target.
         * On CM0, the physical TCA8418 also replaces both on-screen key
         * surfaces; the SDL simulator keeps them available for touch tests. */
        if (lv_group_t *group = lv_group_get_default()) lv_group_add_obj(group, surface_);
    }
    bind_keyboard_indev();

    build_header(application_name);
    work_area_ = lv_obj_create(surface_);
    lv_obj_set_name_static(work_area_, "terminal_work_area");
    lv_obj_set_width(work_area_, LV_PCT(100));
    lv_obj_set_flex_grow(work_area_, 1);
    prepare_container(work_area_);

    build_terminal();
    build_keyboard();
    build_special_keys();
    if (hardware_keyboard_) lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    apply_layout();
    (void)view_model_.start();
    sync_state(true);

    timer_ = lv_timer_create(timer_tick, 45, this);
    lv_timer_ready(timer_);
    return surface_;
}

void TerminalView::destroy() noexcept
{
    unbind_keyboard_indev();
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
    view_model_.stop();
    if (special_popup_) lv_obj_delete(special_popup_);
    if (surface_) lv_obj_delete(surface_);
    surface_                   = nullptr;
    header_                    = nullptr;
    title_                     = nullptr;
    status_label_              = nullptr;
    work_area_                 = nullptr;
    terminal_box_              = nullptr;
    special_button_            = nullptr;
    exit_button_               = nullptr;
    special_popup_             = nullptr;
    special_window_            = nullptr;
    special_close_             = nullptr;
    special_keys_              = nullptr;
    keyboard_                  = nullptr;
    keyboard_present_           = false;
    hardware_keyboard_         = false;
    hardware_keyboard_scan_at_ = 0;
    request_exit_              = nullptr;
    exit_user_data_            = nullptr;
    row_labels_.clear();
}

void TerminalView::special_key_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    auto *view          = static_cast<TerminalView *>(lv_event_get_user_data(event));
    const auto selected = lv_buttonmatrix_get_selected_button(view->special_keys_);
    if (selected == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char *label = lv_buttonmatrix_get_button_text(view->special_keys_, selected);
    const auto key    = special_key_for_label(label ? std::string_view(label) : std::string_view{});
    if (!key) return;
    view->view_model_.send_key(*key);
    view->sync_state(true);
}

void TerminalView::special_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto *view = static_cast<TerminalView *>(lv_event_get_user_data(event));
    view->open_special_popup();
}

void TerminalView::exit_button_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto *view = static_cast<TerminalView *>(lv_event_get_user_data(event));
    if (view->request_exit_) view->request_exit_(view->exit_user_data_);
}

void TerminalView::special_close_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto *view = static_cast<TerminalView *>(lv_event_get_user_data(event));
    view->close_special_popup();
}

void TerminalView::keyboard_event(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    auto *view          = static_cast<TerminalView *>(lv_event_get_user_data(event));
    const auto selected = lv_buttonmatrix_get_selected_button(view->keyboard_);
    if (selected == LV_BUTTONMATRIX_BUTTON_NONE) return;
    view->handle_keyboard_text(lv_buttonmatrix_get_button_text(view->keyboard_, selected));
}

void TerminalView::hardware_key_event(lv_event_t *event)
{
    auto *view              = static_cast<TerminalView *>(lv_event_get_user_data(event));
    const std::uint32_t key = lv_event_get_key(event);
    switch (key) {
        case kTerminalEnter:
        case LV_KEY_ENTER:
            view->view_model_.send_key(TerminalKey::enter);
            return;
        case kTerminalEscape:
        case LV_KEY_ESC:
            view->view_model_.send_key(TerminalKey::escape);
            return;
        case kTerminalTab:
        case LV_KEY_NEXT:
            view->view_model_.send_key(TerminalKey::tab);
            return;
        case kTerminalBacktab:
        case LV_KEY_PREV:
            view->view_model_.send_key(TerminalKey::backtab);
            return;
        case kTerminalBackspace:
        case LV_KEY_BACKSPACE:
        case LV_KEY_DEL:
            view->view_model_.send_key(TerminalKey::backspace);
            return;
        case kTerminalUp:
        case LV_KEY_UP:
            view->view_model_.send_key(TerminalKey::arrow_up);
            return;
        case kTerminalDown:
        case LV_KEY_DOWN:
            view->view_model_.send_key(TerminalKey::arrow_down);
            return;
        case kTerminalLeft:
        case LV_KEY_LEFT:
            view->view_model_.send_key(TerminalKey::arrow_left);
            return;
        case kTerminalRight:
        case LV_KEY_RIGHT:
            view->view_model_.send_key(TerminalKey::arrow_right);
            return;
        case kTerminalHome:
        case LV_KEY_HOME:
            view->view_model_.send_key(TerminalKey::home);
            return;
        case kTerminalEnd:
        case LV_KEY_END:
            view->view_model_.send_key(TerminalKey::end);
            return;
        default:
            break;
    }
    if (key >= 32 && key <= 126) {
        const char text[]{static_cast<char>(key), '\0'};
        view->view_model_.send_text(text);
    }
}

lv_key_t TerminalView::remap_keyboard_key(lv_indev_t *indev, lv_key_t key)
{
    if (!indev || !lv_indev_get_user_data(indev)) return key;
    auto *view = static_cast<TerminalView *>(lv_indev_get_user_data(indev));
    if (view->special_popup_ && !lv_obj_has_flag(view->special_popup_, LV_OBJ_FLAG_HIDDEN)) return key;
    switch (key) {
        case LV_KEY_ESC:       return kTerminalEscape;
        case LV_KEY_NEXT:      return kTerminalTab;
        case LV_KEY_PREV:      return kTerminalBacktab;
        case LV_KEY_ENTER:     return kTerminalEnter;
        case LV_KEY_BACKSPACE:
        case LV_KEY_DEL:       return kTerminalBackspace;
        case LV_KEY_UP:        return kTerminalUp;
        case LV_KEY_DOWN:      return kTerminalDown;
        case LV_KEY_LEFT:      return kTerminalLeft;
        case LV_KEY_RIGHT:     return kTerminalRight;
        case LV_KEY_HOME:      return kTerminalHome;
        case LV_KEY_END:       return kTerminalEnd;
        default:               return key;
    }
}

void TerminalView::bind_keyboard_indev() noexcept
{
    lv_group_t *group = lv_group_get_default();
    if (!group) return;
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) != LV_INDEV_TYPE_KEYPAD || lv_indev_get_group(indev) != group) continue;
        lv_indev_set_user_data(indev, this);
        lv_indev_set_key_remap_cb(indev, remap_keyboard_key);
    }
}

void TerminalView::unbind_keyboard_indev() noexcept
{
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_user_data(indev) != this) continue;
        lv_indev_set_key_remap_cb(indev, nullptr);
        lv_indev_set_user_data(indev, nullptr);
    }
}

void TerminalView::size_changed_event(lv_event_t *event)
{
    static_cast<TerminalView *>(lv_event_get_user_data(event))->apply_layout();
}

void TerminalView::timer_tick(lv_timer_t *timer)
{
    auto *view = static_cast<TerminalView *>(lv_timer_get_user_data(timer));
    view->sync_hardware_keyboard();
    view->sync_state();
}

void TerminalView::sync_hardware_keyboard()
{
    if (!surface_ || !special_keys_ || !keyboard_) return;
    bind_keyboard_indev();
    if (lv_tick_elaps(hardware_keyboard_scan_at_) < 500) return;
    hardware_keyboard_scan_at_ = lv_tick_get();

    const bool present = cm0_input_keyboard_present();
    const bool hardware = present && cm0_input_hardware_keyboard_present();
    if (present == keyboard_present_ && hardware == hardware_keyboard_) return;
    keyboard_present_ = present;
    hardware_keyboard_ = hardware;

    if (auto *group = lv_group_get_default()) {
        if (keyboard_present_) {
            if (lv_obj_get_group(surface_) != group) lv_group_add_obj(group, surface_);
            lv_group_focus_obj(surface_);
        } else if (lv_obj_get_group(surface_) == group) {
            lv_group_remove_obj(surface_);
        }
    }

    lv_obj_set_flag(keyboard_, LV_OBJ_FLAG_HIDDEN, hardware_keyboard_);
    apply_layout();
}

void TerminalView::open_special_popup()
{
    if (!special_popup_) return;
    lv_obj_clear_flag(special_popup_, LV_OBJ_FLAG_HIDDEN);
    if (auto *group = lv_group_get_default()) {
        if (special_button_ && lv_obj_get_group(special_button_) == group) lv_group_remove_obj(special_button_);
        if (special_close_ && lv_obj_get_group(special_close_) != group) lv_group_add_obj(group, special_close_);
        if (special_keys_ && lv_obj_get_group(special_keys_) != group) lv_group_add_obj(group, special_keys_);
        if (special_close_) lv_group_focus_obj(special_close_);
    }
}

void TerminalView::close_special_popup()
{
    if (!special_popup_) return;
    lv_obj_add_flag(special_popup_, LV_OBJ_FLAG_HIDDEN);
    if (auto *group = lv_group_get_default(); group) {
        if (lv_obj_get_group(special_button_) != group) lv_group_add_obj(group, special_button_);
        if (special_close_ && lv_obj_get_group(special_close_) == group) lv_group_remove_obj(special_close_);
        if (special_keys_ && lv_obj_get_group(special_keys_) == group) lv_group_remove_obj(special_keys_);
        if (surface_ && lv_obj_get_group(surface_) != group) lv_group_add_obj(group, surface_);
        if (surface_) lv_group_focus_obj(surface_);
    }
}

void TerminalView::build_header(const char *application_name)
{
    header_ = lv_obj_create(surface_);
    lv_obj_set_name_static(header_, "terminal_header");
    lv_obj_set_width(header_, LV_PCT(100));
    lv_obj_set_style_radius(header_, 0, 0);
    lv_obj_set_style_border_width(header_, 0, 0);
    lv_obj_set_style_border_side(header_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header_, 1, 0);
    lv_obj_set_style_border_color(header_, lv_color_hex(components::color_border), 0);
    lv_obj_set_style_bg_color(header_, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_opa(header_, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(header_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header_, LV_OBJ_FLAG_SCROLLABLE);

    title_ = lv_label_create(header_);
    lv_obj_set_name_static(title_, "terminal_title");
    lv_label_set_text(title_, application_name);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_CLIP);
    (void)components::set_text_font(title_, 28);
    lv_obj_set_style_text_color(title_, lv_color_hex(components::color_container_title), 0);
    lv_obj_set_width(title_, 0);
    lv_obj_set_flex_grow(title_, 1);

    status_label_ = lv_label_create(header_);
    lv_obj_set_name_static(status_label_, "terminal_status");
    lv_label_set_text(status_label_, "Starting shell / 18 px");
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_CLIP);
    (void)components::set_text_font(status_label_, 14);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(components::color_muted_text), 0);
}

void TerminalView::build_terminal()
{
    terminal_box_ = lv_obj_create(work_area_);
    lv_obj_set_name_static(terminal_box_, "terminal_output");
    lv_obj_set_style_radius(terminal_box_, 6, 0);
    lv_obj_set_style_border_width(terminal_box_, 1, 0);
    lv_obj_set_style_border_color(terminal_box_, lv_color_hex(components::color_border), 0);
    lv_obj_set_style_bg_color(terminal_box_, lv_color_hex(components::color_terminal_background), 0);
    lv_obj_set_style_bg_opa(terminal_box_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(terminal_box_, 10, 0);
    lv_obj_clear_flag(terminal_box_, LV_OBJ_FLAG_SCROLLABLE);

    row_labels_.reserve(TerminalSession::maximum_rows);
    for (std::size_t row = 0; row < TerminalSession::maximum_rows; ++row) {
        auto *label            = lv_label_create(terminal_box_);
        const std::string name = "terminal_row_" + std::to_string(row);
        lv_obj_set_name(label, name.c_str());
        lv_label_set_text(label, " ");
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(label, lv_color_hex(components::color_terminal_text), 0);
        lv_obj_set_style_text_letter_space(label, 0, 0);
        lv_obj_set_style_text_line_space(label, 0, 0);
        row_labels_.push_back(label);
    }
}

void TerminalView::build_special_keys()
{
    special_button_ = lv_button_create(header_);
    lv_obj_set_name_static(special_button_, "terminal_special_button");
    lv_obj_set_size(special_button_, 42, 42);
    lv_obj_set_style_radius(special_button_, 10, 0);
    lv_obj_set_style_border_width(special_button_, 1, 0);
    lv_obj_set_style_border_color(special_button_, lv_color_hex(components::color_border), 0);
    lv_obj_set_style_bg_color(special_button_, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_color(special_button_, lv_color_hex(0xe8eef2), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(special_button_, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(special_button_, special_button_event, LV_EVENT_CLICKED, this);

    auto *special_icon = lv_label_create(special_button_);
    lv_obj_set_name_static(special_icon, "terminal_special_button_icon");
    lv_label_set_text(special_icon, LV_SYMBOL_KEYBOARD);
    (void)components::set_text_font(special_icon, 20);
    lv_obj_set_style_text_color(special_icon, lv_color_hex(components::color_accent), 0);
    lv_obj_center(special_icon);

    exit_button_ = lv_button_create(header_);
    lv_obj_set_name_static(exit_button_, "terminal_exit_button");
    lv_obj_set_size(exit_button_, 42, 42);
    lv_obj_set_style_radius(exit_button_, 10, 0);
    lv_obj_set_style_border_width(exit_button_, 1, 0);
    lv_obj_set_style_border_color(exit_button_, lv_color_hex(components::color_border), 0);
    lv_obj_set_style_bg_color(exit_button_, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_color(exit_button_, lv_color_hex(0xe8eef2), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(exit_button_, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(exit_button_, exit_button_event, LV_EVENT_CLICKED, this);

    auto *exit_icon = lv_label_create(exit_button_);
    lv_obj_set_name_static(exit_icon, "terminal_exit_button_icon");
    lv_label_set_text(exit_icon, LV_SYMBOL_CLOSE);
    (void)components::set_text_font(exit_icon, 20);
    lv_obj_set_style_text_color(exit_icon, lv_color_hex(components::color_muted_text), 0);
    lv_obj_center(exit_icon);

    special_popup_ = lv_obj_create(surface_);
    lv_obj_set_name_static(special_popup_, "terminal_special_popup");
    lv_obj_add_flag(special_popup_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(special_popup_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(special_popup_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(special_popup_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(special_popup_, 0, 0);
    lv_obj_set_style_radius(special_popup_, 0, 0);
    lv_obj_set_style_border_width(special_popup_, 0, 0);
    lv_obj_set_style_bg_color(special_popup_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(special_popup_, LV_OPA_50, 0);
    lv_obj_clear_flag(special_popup_, LV_OBJ_FLAG_SCROLLABLE);
    cm0_input_set_focus_scope(special_popup_);

    special_window_ = lv_obj_create(special_popup_);
    lv_obj_set_name_static(special_window_, "terminal_special_window");
    lv_obj_set_style_radius(special_window_, 20, 0);
    lv_obj_set_style_border_width(special_window_, 1, 0);
    lv_obj_set_style_border_color(special_window_, lv_color_hex(components::color_border), 0);
    lv_obj_set_style_bg_color(special_window_, lv_color_hex(components::color_container_background), 0);
    lv_obj_set_style_bg_opa(special_window_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(special_window_, 16, 0);
    lv_obj_set_style_pad_row(special_window_, 10, 0);
    lv_obj_set_flex_flow(special_window_, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(special_window_, LV_OBJ_FLAG_SCROLLABLE);

    auto *popup_header = lv_obj_create(special_window_);
    lv_obj_set_name_static(popup_header, "terminal_special_header");
    lv_obj_set_width(popup_header, LV_PCT(100));
    lv_obj_set_height(popup_header, 72);
    lv_obj_set_style_radius(popup_header, 0, 0);
    lv_obj_set_style_border_width(popup_header, 0, 0);
    lv_obj_set_style_bg_opa(popup_header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(popup_header, 0, 0);
    lv_obj_set_flex_flow(popup_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(popup_header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(popup_header, LV_OBJ_FLAG_SCROLLABLE);

    auto *title_group = lv_obj_create(popup_header);
    lv_obj_set_name_static(title_group, "terminal_special_title_group");
    lv_obj_set_width(title_group, 0);
    lv_obj_set_flex_grow(title_group, 1);
    lv_obj_set_style_radius(title_group, 0, 0);
    lv_obj_set_style_border_width(title_group, 0, 0);
    lv_obj_set_style_bg_opa(title_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(title_group, 0, 0);
    lv_obj_set_style_pad_row(title_group, 2, 0);
    lv_obj_set_flex_flow(title_group, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(title_group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(title_group, LV_OBJ_FLAG_SCROLLABLE);

    auto *popup_title = lv_label_create(title_group);
    lv_obj_set_name_static(popup_title, "terminal_special_title");
    lv_label_set_text(popup_title, "Special keys");
    (void)components::set_text_font(popup_title, 22);
    lv_obj_set_style_text_color(popup_title, lv_color_hex(components::color_container_title), 0);

    auto *popup_hint = lv_label_create(title_group);
    lv_obj_set_name_static(popup_hint, "terminal_special_hint");
    lv_label_set_text(popup_hint, "Tap Ctrl, then a letter for shortcuts.");
    (void)components::set_text_font(popup_hint, 13);
    lv_obj_set_style_text_color(popup_hint, lv_color_hex(components::color_muted_text), 0);

    special_close_ = lv_button_create(popup_header);
    lv_obj_set_name_static(special_close_, "terminal_special_close");
    lv_obj_set_size(special_close_, 72, 40);
    lv_obj_set_style_radius(special_close_, 12, 0);
    lv_obj_set_style_bg_color(special_close_, lv_color_hex(components::color_keyboard_background), 0);
    lv_obj_add_event_cb(special_close_, special_close_event, LV_EVENT_CLICKED, this);
    auto *close_label = lv_label_create(special_close_);
    lv_label_set_text(close_label, "Done");
    (void)components::set_text_font(close_label, 14);
    lv_obj_set_style_text_color(close_label, lv_color_hex(components::color_text), 0);
    lv_obj_center(close_label);

    special_keys_ = lv_buttonmatrix_create(special_window_);
    lv_obj_set_name_static(special_keys_, "terminal_special_keys");
    lv_obj_set_width(special_keys_, LV_PCT(100));
    lv_obj_set_flex_grow(special_keys_, 1);
    lv_obj_add_flag(special_keys_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(special_keys_, 14, LV_PART_MAIN);
    lv_obj_set_style_border_width(special_keys_, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(special_keys_, lv_color_hex(components::color_page_background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(special_keys_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(special_keys_, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(special_keys_, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(special_keys_, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(special_keys_, 8, LV_PART_ITEMS);
    lv_obj_set_style_border_width(special_keys_, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(special_keys_, lv_color_hex(components::color_container_background), LV_PART_ITEMS);
    lv_obj_set_style_border_width(special_keys_, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(special_keys_, lv_color_hex(components::color_border), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(special_keys_, lv_color_hex(0xe8edf0), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(special_keys_, lv_color_hex(components::color_accent), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(special_keys_, lv_color_hex(components::color_text), LV_PART_ITEMS);
    lv_obj_set_style_text_color(special_keys_, lv_color_hex(components::color_on_action), LV_PART_ITEMS | LV_STATE_CHECKED);
    (void)components::set_text_font(special_keys_, 16, LV_PART_ITEMS);
    lv_obj_add_event_cb(special_keys_, special_key_event, LV_EVENT_VALUE_CHANGED, this);
    lv_buttonmatrix_set_map(special_keys_, kSpecialPopupMap);
    lv_buttonmatrix_set_button_ctrl_all(special_keys_, LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    lv_buttonmatrix_set_button_ctrl(special_keys_, 0, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_button_width(special_keys_, 11, 2);
}

void TerminalView::build_keyboard()
{
    keyboard_ = lv_buttonmatrix_create(surface_);
    lv_obj_set_name_static(keyboard_, "terminal_keyboard");
    lv_obj_set_width(keyboard_, LV_PCT(100));
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(keyboard_, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(keyboard_, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(components::color_keyboard_background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyboard_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(keyboard_, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_row(keyboard_, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard_, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(keyboard_, 6, LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard_, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard_, lv_color_hex(components::color_border), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(components::color_container_background), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(0xb9c4ca), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(keyboard_, lv_color_hex(components::color_text), LV_PART_ITEMS);
    (void)components::set_text_font(keyboard_, 18, LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard_, keyboard_event, LV_EVENT_VALUE_CHANGED, this);
    set_keyboard_mode(KeyboardMode::lower);
}

void TerminalView::set_keyboard_mode(KeyboardMode mode)
{
    keyboard_mode_ = mode;
    switch (mode) {
        case KeyboardMode::upper:
            lv_buttonmatrix_set_map(keyboard_, kKeyboardUpperMap);
            break;
        case KeyboardMode::number:
            lv_buttonmatrix_set_map(keyboard_, kKeyboardNumberMap);
            break;
        case KeyboardMode::symbol:
            lv_buttonmatrix_set_map(keyboard_, kKeyboardSymbolMap);
            break;
        case KeyboardMode::lower:
            lv_buttonmatrix_set_map(keyboard_, kKeyboardLowerMap);
            break;
    }
    lv_buttonmatrix_set_button_ctrl_all(keyboard_, LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    if (mode == KeyboardMode::lower || mode == KeyboardMode::upper) {
        lv_buttonmatrix_set_button_width(keyboard_, 19, 2);
        lv_buttonmatrix_set_button_width(keyboard_, 27, 2);
        lv_buttonmatrix_set_button_width(keyboard_, 28, 2);
        lv_buttonmatrix_set_button_width(keyboard_, 29, 2);
        lv_buttonmatrix_set_button_width(keyboard_, 30, 6);
        lv_buttonmatrix_set_button_width(keyboard_, 31, 3);
    } else {
        const std::uint32_t backspace = mode == KeyboardMode::number ? 26 : 17;
        lv_buttonmatrix_set_button_width(keyboard_, backspace, 2);
        const std::uint32_t bottom = mode == KeyboardMode::number ? 27 : 25;
        lv_buttonmatrix_set_button_width(keyboard_, bottom, 2);
        lv_buttonmatrix_set_button_width(keyboard_, bottom + 1, 7);
        lv_buttonmatrix_set_button_width(keyboard_, bottom + 2, 3);
    }
}

void TerminalView::handle_keyboard_text(const char *text)
{
    if (!text || !*text) return;
    const std::string key = text;
    if (key == "ABC")
        set_keyboard_mode(KeyboardMode::upper);
    else if (key == "abc")
        set_keyboard_mode(KeyboardMode::lower);
    else if (key == "123")
        set_keyboard_mode(KeyboardMode::number);
    else if (key == "#+=")
        set_keyboard_mode(KeyboardMode::symbol);
    else if (key == "Enter")
        view_model_.send_key(TerminalKey::enter);
    else if (key == "BS")
        view_model_.send_key(TerminalKey::backspace);
    else if (key == "Space")
        view_model_.send_text(" ");
    else
        view_model_.send_text(key);
    sync_state(true);
}

void TerminalView::apply_layout() noexcept
{
    if (applying_layout_ || !surface_ || !header_ || !work_area_ || !terminal_box_ || !special_button_ || !exit_button_ ||
        !special_popup_ || !special_window_ || !special_keys_ || !keyboard_)
        return;
    applying_layout_ = true;
    lv_obj_update_layout(surface_);
    const int width      = lv_obj_get_content_width(surface_);
    const int height     = lv_obj_get_content_height(surface_);
    const bool landscape = width >= height;
    const bool compact   = width < 420;

    lv_obj_set_height(header_, landscape ? 64 : 76);
    lv_obj_set_style_pad_hor(header_, compact ? 8 : 22, 0);
    lv_obj_set_style_pad_ver(header_, 10, 0);
    lv_obj_set_style_pad_column(header_, compact ? 6 : 10, 0);
    lv_obj_set_width(status_label_, compact ? 116 : LV_SIZE_CONTENT);
    (void)components::set_text_font(status_label_, compact ? 12 : 14);
    (void)components::set_text_font(title_, compact ? 20 : 28);

    lv_obj_set_style_pad_all(work_area_, compact ? 8 : 12, 0);
    lv_obj_set_style_pad_row(work_area_, compact ? 7 : 10, 0);
    lv_obj_set_style_pad_column(work_area_, 10, 0);
    lv_obj_set_flex_flow(work_area_, landscape ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(work_area_, LV_FLEX_ALIGN_START,
                          landscape ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    lv_obj_set_flex_grow(terminal_box_, 1);
    lv_obj_set_size(terminal_box_, landscape ? 0 : LV_PCT(100), landscape ? LV_PCT(100) : 0);
    lv_obj_set_size(special_button_, 42, 42);
    lv_obj_set_size(exit_button_, 42, 42);

    const int keyboard_height = landscape ? 158 : ((compact && height < 700) ? 190 : 258);
    lv_obj_set_height(keyboard_, keyboard_height);
    (void)components::set_text_font(keyboard_, landscape || compact ? 14 : 18, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(keyboard_, landscape || compact ? 5 : 7, LV_PART_MAIN);
    lv_obj_set_style_pad_row(keyboard_, landscape || compact ? 4 : 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard_, landscape || compact ? 4 : 6, LV_PART_MAIN);

    lv_obj_set_size(special_popup_, LV_PCT(100), LV_PCT(100));
    const int popup_margin = compact ? 10 : 20;
    const int popup_width = std::max(260, width - popup_margin * 2);
    const int popup_height = std::min(std::max(1, height - popup_margin * 2), compact ? 360 : 420);
    lv_obj_set_size(special_window_, std::min(popup_width, landscape ? 560 : width - popup_margin * 2),
                    popup_height);
    lv_obj_center(special_window_);
    lv_obj_set_style_pad_all(special_window_, compact ? 10 : 16, 0);
    lv_obj_set_style_pad_row(special_window_, compact ? 8 : 10, 0);
    lv_obj_set_height(lv_obj_get_child(special_window_, 0), compact ? 64 : 72);
    auto *title_group = lv_obj_get_child(lv_obj_get_child(special_window_, 0), 0);
    auto *popup_title = title_group ? lv_obj_get_child(title_group, 0) : nullptr;
    auto *popup_hint = title_group ? lv_obj_get_child(title_group, 1) : nullptr;
    if (popup_title) (void)components::set_text_font(popup_title, compact ? 20 : 22);
    if (popup_hint) (void)components::set_text_font(popup_hint, compact ? 12 : 13);
    lv_obj_set_size(special_close_, compact ? 66 : 72, compact ? 38 : 40);
    lv_obj_set_style_pad_all(special_keys_, compact ? 5 : 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(special_keys_, compact ? 5 : 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(special_keys_, compact ? 5 : 8, LV_PART_MAIN);
    (void)components::set_text_font(special_keys_, compact ? 13 : 16, LV_PART_ITEMS);

    lv_obj_update_layout(surface_);
    const int active_font = rendered_font_size_ > 0 ? rendered_font_size_ : TerminalModel::font_sizes[1];
    apply_terminal_font(active_font);
    applying_layout_ = false;
}

void TerminalView::apply_terminal_font(int font_size)
{
    const auto *font = components::font(static_cast<std::uint32_t>(font_size));
    if (!font || !terminal_box_) return;

    lv_obj_update_layout(terminal_box_);
    const int content_width  = std::max(1, lv_obj_get_content_width(terminal_box_));
    const int content_height = std::max(1, lv_obj_get_content_height(terminal_box_));
    const int line_height    = std::max(1, lv_font_get_line_height(font) + 2);
    const int measured_width = static_cast<int>(lv_font_get_glyph_width(font, 'M', 0));
    const int cell_width     = std::max(1, measured_width > 0 ? measured_width : font_size * 3 / 5);
    const auto rows          = static_cast<std::size_t>(std::clamp(content_height / line_height,
                                                                   static_cast<int>(TerminalSession::minimum_rows),
                                                                   static_cast<int>(TerminalSession::maximum_rows)));
    const auto columns       = static_cast<std::size_t>(std::clamp(content_width / cell_width,
                                                                   static_cast<int>(TerminalSession::minimum_columns),
                                                                   static_cast<int>(TerminalSession::maximum_columns)));

    for (std::size_t row = 0; row < row_labels_.size(); ++row) {
        auto *label = row_labels_[row];
        lv_obj_set_pos(label, 0, static_cast<int>(row) * line_height);
        lv_obj_set_size(label, content_width, line_height);
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_flag(label, LV_OBJ_FLAG_HIDDEN, row >= rows);
    }
    rendered_font_size_ = font_size;
    view_model_.set_geometry(rows, columns);
}

void TerminalView::sync_state(bool force)
{
    const auto state = view_model_.state();
    if (!force && state.revision == rendered_revision_) return;
    if (state.font_size != rendered_font_size_) apply_terminal_font(state.font_size);

    for (std::size_t row = 0; row < row_labels_.size(); ++row) {
        const bool visible = row < state.row_count;
        lv_obj_set_flag(row_labels_[row], LV_OBJ_FLAG_HIDDEN, !visible);
        if (visible) lv_label_set_text(row_labels_[row], row < state.rows.size() ? state.rows[row].c_str() : " ");
    }

    char status[128];
    if (!state.preference_saved) {
        std::snprintf(status, sizeof(status), "Font setting not saved / %d px", state.font_size);
    } else {
        std::snprintf(status, sizeof(status), "%s / %d px", state.status.c_str(), state.font_size);
    }
    lv_label_set_text(status_label_, status);
    lv_obj_set_style_text_color(
        status_label_,
        lv_color_hex(state.running || state.ctrl_armed ? components::color_accent : components::color_muted_text), 0);
    if (state.ctrl_armed)
        lv_buttonmatrix_set_button_ctrl(special_keys_, 0, LV_BUTTONMATRIX_CTRL_CHECKED);
    else
        lv_buttonmatrix_clear_button_ctrl(special_keys_, 0, LV_BUTTONMATRIX_CTRL_CHECKED);
    rendered_revision_ = state.revision;
}
