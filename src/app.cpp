#include "app.hpp"

#include "app_identity.h"
#include "pages/terminal/terminal_view.hpp"

#include <memory>

namespace {

struct AppState {
    std::unique_ptr<TerminalViewModel> terminal_view_model;
    std::unique_ptr<TerminalView> terminal_view;
};

AppState app_state;

void app_open(const cm0_app_context_t *context)
{
    app_state.terminal_view_model = std::make_unique<TerminalViewModel>();
    app_state.terminal_view       = std::make_unique<TerminalView>(*app_state.terminal_view_model);
    app_state.terminal_view->create(context->root, LILYGO_UI_TERMINAL_APP_NAME, context->request_exit,
                                    context->user_data);
}

void app_close()
{
    if (app_state.terminal_view) app_state.terminal_view->destroy();
    app_state.terminal_view.reset();
    app_state.terminal_view_model.reset();
}

const cm0_app_descriptor_t app_descriptor{
    CM0_APP_API_VERSION, sizeof(cm0_app_descriptor_t), LILYGO_UI_TERMINAL_APP_ID, app_open, app_close,
};

}  // namespace

extern "C" const cm0_app_descriptor_t *cm0_app_get_descriptor()
{
    return &app_descriptor;
}
