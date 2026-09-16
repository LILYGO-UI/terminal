#include "pages/terminal/terminal_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace {

const char *const kSpecialWideMap[] = {
    "Ctrl", "Esc", "Tab", "Enter",        "A-",         "A+",           "Clear",         "Restart", "\n",
    "C",    "D",   "L",   LV_SYMBOL_LEFT, LV_SYMBOL_UP, LV_SYMBOL_DOWN, LV_SYMBOL_RIGHT, "BS",      "",
};

const char *const kSpecialCompactMap[] = {
    "Ctrl", "Esc", "Tab", "Enter", "\n", "A-",           "A+",         "Clear",        "Restart",       "\n",
    "C",    "D",   "L",   "BS",    "\n", LV_SYMBOL_LEFT, LV_SYMBOL_UP, LV_SYMBOL_DOWN, LV_SYMBOL_RIGHT, "",
};

const char *const kSpecialSideMap[] = {
    "Ctrl",  "Esc",        "\n",           "Tab",
    "Enter", "\n",         "A-",           "A+",
    "\n",    "Clear",      "Restart",      "\n",
    "C",     "D",          "\n",           "L",
    "BS",    "\n",         LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT,
    "\n",    LV_SYMBOL_UP, LV_SYMBOL_DOWN, "",
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

std::optional<TerminalKey> special_key_for_label(std::string_view label)
{
    if (label == "Ctrl") return TerminalKey::ctrl;
    if (label == "Esc") return TerminalKey::escape;
    if (label == "Tab") return TerminalKey::tab;
    if (label == "Enter") return TerminalKey::enter;
    if (label == "A-") return TerminalKey::font_down;
    if (label == "A+") return TerminalKey::font_up;
    if (label == "Clear") return TerminalKey::clear;
    if (label == "Restart") return TerminalKey::restart;
    if (label == "C") return TerminalKey::letter_c;
    if (label == "D") return TerminalKey::letter_d;
    if (label == "L") return TerminalKey::letter_l;
    if (label == "BS") return TerminalKey::backspace;
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

lv_obj_t *TerminalView::create(lv_obj_t *parent, const char *application_name)
{
    surface_ = components::create_page(parent);
    lv_obj_set_name_static(surface_, "terminal_surface");
    lv_obj_add_event_cb(surface_, size_changed_event, LV_EVENT_SIZE_CHANGED, this);
    lv_obj_add_event_cb(surface_, hardware_key_event, LV_EVENT_KEY, this);

    build_header(application_name);
    work_area_ = lv_obj_create(surface_);
    lv_obj_set_name_static(work_area_, "terminal_work_area");
    lv_obj_set_width(work_area_, LV_PCT(100));
    lv_obj_set_flex_grow(work_area_, 1);
    prepare_container(work_area_);

    build_terminal();
    build_special_keys();
    build_keyboard();
    apply_layout();
    (void)view_model_.start();
    sync_state(true);

    timer_ = lv_timer_create(timer_tick, 45, this);
    lv_timer_ready(timer_);
    return surface_;
}

void TerminalView::destroy() noexcept
{
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
    view_model_.stop();
    if (surface_) lv_obj_delete(surface_);
    surface_      = nullptr;
    header_       = nullptr;
    title_        = nullptr;
    status_label_ = nullptr;
    work_area_    = nullptr;
    terminal_box_ = nullptr;
    special_keys_ = nullptr;
    keyboard_     = nullptr;
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
        case LV_KEY_ENTER:
            view->view_model_.send_key(TerminalKey::enter);
            return;
        case LV_KEY_ESC:
            view->view_model_.send_key(TerminalKey::escape);
            return;
        case LV_KEY_NEXT:
            view->view_model_.send_key(TerminalKey::tab);
            return;
        case LV_KEY_BACKSPACE:
        case LV_KEY_DEL:
            view->view_model_.send_key(TerminalKey::backspace);
            return;
        case LV_KEY_UP:
            view->view_model_.send_key(TerminalKey::arrow_up);
            return;
        case LV_KEY_DOWN:
            view->view_model_.send_key(TerminalKey::arrow_down);
            return;
        case LV_KEY_LEFT:
            view->view_model_.send_key(TerminalKey::arrow_left);
            return;
        case LV_KEY_RIGHT:
            view->view_model_.send_key(TerminalKey::arrow_right);
            return;
        default:
            break;
    }
    if (key >= 32 && key <= 126) {
        const char text[]{static_cast<char>(key), '\0'};
        view->view_model_.send_text(text);
    }
}

void TerminalView::size_changed_event(lv_event_t *event)
{
    static_cast<TerminalView *>(lv_event_get_user_data(event))->apply_layout();
}

void TerminalView::timer_tick(lv_timer_t *timer)
{
    static_cast<TerminalView *>(lv_timer_get_user_data(timer))->sync_state();
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
    lv_obj_set_style_border_color(terminal_box_, lv_color_hex(0x34434c), 0);
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
    special_keys_ = lv_buttonmatrix_create(work_area_);
    lv_obj_set_name_static(special_keys_, "terminal_special_keys");
    lv_obj_add_flag(special_keys_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_radius(special_keys_, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(special_keys_, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(special_keys_, lv_color_hex(components::color_page_background), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(special_keys_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(special_keys_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(special_keys_, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(special_keys_, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(special_keys_, 6, LV_PART_ITEMS);
    lv_obj_set_style_border_width(special_keys_, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(special_keys_, lv_color_hex(components::color_control_background), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(special_keys_, lv_color_hex(0x3c4a55), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(special_keys_, lv_color_hex(components::color_accent), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(special_keys_, lv_color_hex(components::color_on_action), LV_PART_ITEMS);
    (void)components::set_text_font(special_keys_, 14, LV_PART_ITEMS);
    lv_obj_add_event_cb(special_keys_, special_key_event, LV_EVENT_VALUE_CHANGED, this);
    lv_buttonmatrix_set_map(special_keys_, kSpecialWideMap);
    lv_buttonmatrix_set_button_ctrl_all(special_keys_, LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    lv_buttonmatrix_set_button_ctrl(special_keys_, 0, LV_BUTTONMATRIX_CTRL_CHECKABLE);
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
    if (applying_layout_ || !surface_ || !header_ || !work_area_ || !terminal_box_ || !special_keys_ || !keyboard_)
        return;
    applying_layout_ = true;
    lv_obj_update_layout(surface_);
    const int width      = lv_obj_get_content_width(surface_);
    const int height     = lv_obj_get_content_height(surface_);
    const bool landscape = width >= height;
    const bool compact   = width < 420;

    lv_obj_set_height(header_, landscape ? 64 : 76);
    lv_obj_set_style_pad_hor(header_, compact ? 14 : 22, 0);
    lv_obj_set_style_pad_ver(header_, 10, 0);
    lv_obj_set_style_pad_column(header_, 10, 0);
    lv_obj_set_width(status_label_, compact ? 144 : LV_SIZE_CONTENT);
    (void)components::set_text_font(title_, compact ? 22 : 28);

    lv_obj_set_style_pad_all(work_area_, compact ? 8 : 12, 0);
    lv_obj_set_style_pad_row(work_area_, compact ? 7 : 10, 0);
    lv_obj_set_style_pad_column(work_area_, 10, 0);
    lv_obj_set_flex_flow(work_area_, landscape ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);

    lv_obj_set_flex_grow(terminal_box_, 1);
    lv_obj_set_size(terminal_box_, landscape ? 0 : LV_PCT(100), landscape ? LV_PCT(100) : 0);
    lv_obj_set_size(special_keys_, landscape ? 194 : LV_PCT(100), landscape ? LV_PCT(100) : (compact ? 132 : 106));
    lv_buttonmatrix_set_map(special_keys_,
                            landscape ? kSpecialSideMap : (compact ? kSpecialCompactMap : kSpecialWideMap));
    lv_buttonmatrix_set_button_ctrl_all(special_keys_, LV_BUTTONMATRIX_CTRL_CLICK_TRIG);
    lv_buttonmatrix_set_button_ctrl(special_keys_, 0, LV_BUTTONMATRIX_CTRL_CHECKABLE);

    const int keyboard_height = landscape ? 158 : ((compact && height < 700) ? 190 : 258);
    lv_obj_set_height(keyboard_, keyboard_height);
    (void)components::set_text_font(keyboard_, landscape || compact ? 14 : 18, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(keyboard_, landscape || compact ? 5 : 7, LV_PART_MAIN);
    lv_obj_set_style_pad_row(keyboard_, landscape || compact ? 4 : 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard_, landscape || compact ? 4 : 6, LV_PART_MAIN);

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
