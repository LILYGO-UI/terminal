#ifndef LILYGO_UI_TERMINAL_COMPONENTS_HPP
#define LILYGO_UI_TERMINAL_COMPONENTS_HPP

#include <lvgl.h>

#include <cstdint>

namespace components {

inline constexpr std::uint32_t color_page_background      = 0xf2f2f7;
inline constexpr std::uint32_t color_container_background = 0xffffff;
inline constexpr std::uint32_t color_container_title      = 0x1a1a1a;
inline constexpr std::uint32_t color_text                 = 0x000000;
inline constexpr std::uint32_t color_border               = 0xd8dde3;
inline constexpr std::uint32_t color_action               = 0x20262d;
inline constexpr std::uint32_t color_on_action            = 0xffffff;
inline constexpr std::uint32_t color_accent               = 0x147d64;
inline constexpr std::uint32_t color_terminal_background  = 0x0b0f12;
inline constexpr std::uint32_t color_terminal_text        = 0xc7f0d8;
inline constexpr std::uint32_t color_control_background   = 0x26313a;
inline constexpr std::uint32_t color_keyboard_background  = 0xdfe4e8;
inline constexpr std::uint32_t color_muted_text           = 0x65717b;

[[nodiscard]] const lv_font_t *font(std::uint32_t size) noexcept;
[[nodiscard]] bool set_text_font(lv_obj_t *object, std::uint32_t size, lv_style_selector_t selector = 0) noexcept;

lv_obj_t *create_page(lv_obj_t *parent);
lv_obj_t *create_header(lv_obj_t *parent, const char *title);
lv_obj_t *create_card(lv_obj_t *parent);
lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback, void *user_data);

}  // namespace components

#endif
