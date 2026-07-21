#include <algorithm>

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprland/src/devices/ITouch.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/plugins/PluginSystem.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include "Globals.hpp"
#include "Overview.hpp"

void* pRenderWindow;
void* pRenderLayer;

std::vector<std::shared_ptr<CHyprspaceWidget>> g_overviewWidgets;

CHyprColor  Config::panelBaseColor              = CHyprColor(0xCC111111);
CHyprColor  Config::panelBorderColor            = CHyprColor(0x66FF99CC);
CHyprColor  Config::workspaceActiveBackground   = CHyprColor(0x66111111);
CHyprColor  Config::workspaceInactiveBackground = CHyprColor(0xAA111111);
CHyprColor  Config::workspaceActiveBorder       = CHyprColor(1, 1, 1, 0.3);
CHyprColor  Config::workspaceInactiveBorder     = CHyprColor(1, 1, 1, 0);

int         Config::panelHeight             = 250;
int         Config::panelBorderWidth        = 2;
int         Config::workspaceMargin         = 12;
int         Config::reservedArea            = 0;
int         Config::workspaceBorderSize     = 1;
bool        Config::adaptiveHeight          = false;
bool        Config::centerAligned           = true;
bool        Config::onBottom                = false;
bool        Config::hideBackgroundLayers    = false;
bool        Config::hideTopLayers           = false;
bool        Config::hideOverlayLayers       = false;
bool        Config::drawActiveWorkspace     = true;
bool        Config::hideRealLayers          = false;
bool        Config::affectStrut             = false;
bool        Config::autoDrag                = true;
bool        Config::autoScroll              = true;
bool        Config::exitOnClick             = true;
bool        Config::switchOnDrop            = false;
bool        Config::exitOnSwitch            = false;
bool        Config::showNewWorkspace        = true;
bool        Config::showEmptyWorkspace      = true;
bool        Config::showSpecialWorkspace    = false;
bool        Config::disableGestures         = false;
bool        Config::reverseSwipe            = false;
bool        Config::disableBlur             = false;
float       Config::overrideAnimSpeed       = 0.F;
float       Config::dragAlpha               = 0.2F;
std::string Config::exitKey                 = "Escape";
int         Config::clickReleaseThresholdMs = 200;
int         Config::swipeFingers            = 3;
int         Config::swipeDistance           = 300;
int         Config::swipeForceSpeed         = 30;
float       Config::swipeCancelRatio        = 0.5F;
float       Config::swipeThreshold          = 10.F;
float       Config::swipeClosedPadding      = 10.F;
float       Config::workspaceScrollSpeed    = 2.F;

CHyprSignalListener g_pRenderHook;
CHyprSignalListener g_pConfigReloadHook;
CHyprSignalListener g_pOpenLayerHook;
CHyprSignalListener g_pCloseLayerHook;
CHyprSignalListener g_pMouseButtonHook;
CHyprSignalListener g_pMouseAxisHook;
CHyprSignalListener g_pTouchDownHook;
CHyprSignalListener g_pTouchMoveHook;
CHyprSignalListener g_pTouchUpHook;
CHyprSignalListener g_pSwipeBeginHook;
CHyprSignalListener g_pSwipeUpdateHook;
CHyprSignalListener g_pSwipeEndHook;
CHyprSignalListener g_pKeyPressHook;
CHyprSignalListener g_pSwitchWorkspaceHook;
CHyprSignalListener g_pAddMonitorHook;
CHyprSignalListener g_pPreRemoveMonitorHook;
CHyprSignalListener g_pRemoveMonitorHook;
CHyprSignalListener g_pStartHook;

void reloadConfig();

namespace {

using Config::Values::CBoolValue;
using Config::Values::CColorValue;
using Config::Values::CFloatValue;
using Config::Values::CIntValue;
using Config::Values::CStringValue;

struct SPluginConfigValues {
    SP<CColorValue> panelColor, panelBorderColor, workspaceActiveBackground, workspaceInactiveBackground, workspaceActiveBorder, workspaceInactiveBorder;
    SP<CIntValue>   panelHeight, panelBorderWidth, workspaceMargin, reservedArea, workspaceBorderSize;
    SP<CBoolValue>  adaptiveHeight, centerAligned, onBottom, hideBackgroundLayers, hideTopLayers, hideOverlayLayers, drawActiveWorkspace, hideRealLayers, affectStrut;
    SP<CBoolValue>  autoDrag, autoScroll, exitOnClick, switchOnDrop, exitOnSwitch, showNewWorkspace, showEmptyWorkspace, showSpecialWorkspace;
    SP<CIntValue>   swipeFingers, swipeDistance, swipeForceSpeed, clickReleaseThresholdMs;
    SP<CBoolValue>  disableGestures, reverseSwipe, disableBlur;
    SP<CFloatValue> swipeCancelRatio, swipeThreshold, swipeClosedPadding, workspaceScrollSpeed, overrideAnimSpeed, dragAlpha;
    SP<CStringValue> exitKey;
};

SPluginConfigValues g_pluginConfigValues;

template <typename T>
SP<T> registerPluginValue(const SP<T>& value) {
    if (HyprlandAPI::addConfigValueV2(pHandle, value))
        return value;

    Log::logger->log(Log::ERR, "Hyprspace: failed to register config value {}", value->name());
    return nullptr;
}

int readIntValue(const SP<CIntValue>& value, int fallback) {
    return value ? static_cast<int>(value->value()) : fallback;
}

bool readBoolValue(const SP<CBoolValue>& value, bool fallback) {
    return value ? value->value() : fallback;
}

float readFloatValue(const SP<CFloatValue>& value, float fallback) {
    return value ? value->value() : fallback;
}

std::string readStringValue(const SP<CStringValue>& value, const std::string& fallback) {
    return value ? value->value() : fallback;
}

CHyprColor readColorValue(const SP<CColorValue>& value, const CHyprColor& fallback) {
    return value ? CHyprColor(static_cast<uint32_t>(value->value())) : fallback;
}

void endSwipeIfNeeded(const std::shared_ptr<CHyprspaceWidget>& widget) {
    if (!widget || !widget->isSwiping())
        return;

    IPointer::SSwipeEndEvent cancelled;
    cancelled.cancelled = true;
    widget->endSwipe(cancelled);
}

void teardownMonitorWidget(PHLMONITOR monitor) {
    if (!monitor)
        return;

    for (auto& widget : g_overviewWidgets) {
        if (!widget)
            continue;

        const auto owner = widget->getOwner();
        if (owner && owner != monitor)
            continue;

        endSwipeIfNeeded(widget);
        if (widget->isActive())
            widget->hide();
        widget->cleanup(monitor);
    }
}

void removeMonitorWidget(PHLMONITOR monitor) {
    teardownMonitorWidget(monitor);

    g_overviewWidgets.erase(std::remove_if(g_overviewWidgets.begin(), g_overviewWidgets.end(),
                                           [monitor](const std::shared_ptr<CHyprspaceWidget>& widget) {
                                               if (!widget)
                                                   return true;

                                               const auto owner = widget->getOwner();
                                               return owner == monitor || !owner;
                                           }),
                            g_overviewWidgets.end());
}

void registerConfigValues() {
    g_pluginConfigValues.panelColor                  = registerPluginValue(SP<CColorValue>(new CColorValue("plugin:hyprspace:panel_color", "Overview panel fill color", Config::panelBaseColor.getAsHex())));
    g_pluginConfigValues.panelBorderColor            = registerPluginValue(SP<CColorValue>(new CColorValue("plugin:hyprspace:panel_border_color", "Overview panel border color", Config::panelBorderColor.getAsHex())));
    g_pluginConfigValues.workspaceActiveBackground   = registerPluginValue(SP<CColorValue>(new CColorValue("plugin:hyprspace:workspace_active_background", "Active workspace preview background", Config::workspaceActiveBackground.getAsHex())));
    g_pluginConfigValues.workspaceInactiveBackground = registerPluginValue(SP<CColorValue>(new CColorValue("plugin:hyprspace:workspace_inactive_background", "Inactive workspace preview background", Config::workspaceInactiveBackground.getAsHex())));
    g_pluginConfigValues.workspaceActiveBorder       = registerPluginValue(SP<CColorValue>(new CColorValue("plugin:hyprspace:workspace_active_border", "Active workspace preview border", Config::workspaceActiveBorder.getAsHex())));
    g_pluginConfigValues.workspaceInactiveBorder     = registerPluginValue(SP<CColorValue>(new CColorValue("plugin:hyprspace:workspace_inactive_border", "Inactive workspace preview border", Config::workspaceInactiveBorder.getAsHex())));

    g_pluginConfigValues.panelHeight        = registerPluginValue(SP<CIntValue>(new CIntValue("plugin:hyprspace:panel_height", "Overview panel height in logical pixels", Config::panelHeight)));
    g_pluginConfigValues.panelBorderWidth   = registerPluginValue(SP<CIntValue>(new CIntValue("plugin:hyprspace:panel_border_width", "Overview panel border width in pixels", Config::panelBorderWidth)));
    g_pluginConfigValues.workspaceMargin    = registerPluginValue(SP<CIntValue>(new CIntValue("plugin:hyprspace:workspace_margin", "Spacing between workspace previews", Config::workspaceMargin)));
    g_pluginConfigValues.reservedArea       = registerPluginValue(SP<CIntValue>(new CIntValue("plugin:hyprspace:reserved_area", "Padding reserved inside the overview panel", Config::reservedArea)));
    g_pluginConfigValues.workspaceBorderSize = registerPluginValue(SP<CIntValue>(new CIntValue("plugin:hyprspace:workspace_border_size", "Workspace preview border width", Config::workspaceBorderSize)));

    g_pluginConfigValues.adaptiveHeight       = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:adaptive_height", "Reserved for future adaptive overview sizing", Config::adaptiveHeight)));
    g_pluginConfigValues.centerAligned        = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:center_aligned", "Center workspace previews when they fit", Config::centerAligned)));
    g_pluginConfigValues.onBottom             = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:on_bottom", "Render the overview panel at the bottom edge", Config::onBottom)));
    g_pluginConfigValues.hideBackgroundLayers = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:hide_background_layers", "Skip background and bottom layer previews", Config::hideBackgroundLayers)));
    g_pluginConfigValues.hideTopLayers        = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:hide_top_layers", "Skip top layer previews", Config::hideTopLayers)));
    g_pluginConfigValues.hideOverlayLayers    = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:hide_overlay_layers", "Skip overlay layer previews", Config::hideOverlayLayers)));
    g_pluginConfigValues.drawActiveWorkspace  = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:draw_active_workspace", "Draw the active workspace contents in the overview", Config::drawActiveWorkspace)));
    g_pluginConfigValues.hideRealLayers       = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:hide_real_layers", "Temporarily hide real top and overlay layers while overview is active", Config::hideRealLayers)));
    g_pluginConfigValues.affectStrut          = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:affect_strut", "Reserve monitor space while the overview is visible", Config::affectStrut)));

    g_pluginConfigValues.autoDrag           = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:auto_drag", "Start dragging a hovered window on press", Config::autoDrag)));
    g_pluginConfigValues.autoScroll         = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:auto_scroll", "Allow wheel scrolling outside the panel to switch workspaces", Config::autoScroll)));
    g_pluginConfigValues.exitOnClick        = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:exit_on_click", "Close the overview when clicking outside a workspace", Config::exitOnClick)));
    g_pluginConfigValues.switchOnDrop       = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:switch_on_drop", "Switch to a workspace after dropping a window into it", Config::switchOnDrop)));
    g_pluginConfigValues.exitOnSwitch       = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:exit_on_switch", "Close the overview after a workspace switch", Config::exitOnSwitch)));
    g_pluginConfigValues.showNewWorkspace   = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:show_new_workspace", "Append a synthetic empty workspace preview at the end", Config::showNewWorkspace)));
    g_pluginConfigValues.showEmptyWorkspace = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:show_empty_workspace", "Display empty workspaces between populated ones", Config::showEmptyWorkspace)));
    g_pluginConfigValues.showSpecialWorkspace = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:show_special_workspace", "Display the special workspace preview", Config::showSpecialWorkspace)));

    g_pluginConfigValues.disableGestures    = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:disable_gestures", "Disable overview gesture handling", Config::disableGestures)));
    g_pluginConfigValues.reverseSwipe       = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:reverse_swipe", "Reverse overview swipe direction", Config::reverseSwipe)));
    g_pluginConfigValues.swipeFingers       = registerPluginValue(SP<CIntValue>(new CIntValue("plugin:hyprspace:swipe_fingers", "Number of fingers required for overview swipes", Config::swipeFingers, {.min = 1})));
    g_pluginConfigValues.swipeDistance      = registerPluginValue(SP<CIntValue>(new CIntValue("plugin:hyprspace:swipe_distance", "Gesture distance divisor for overview swipes", Config::swipeDistance, {.min = 1})));
    g_pluginConfigValues.swipeForceSpeed    = registerPluginValue(SP<CIntValue>(new CIntValue("plugin:hyprspace:swipe_force_speed", "Swipe speed required to force open or close", Config::swipeForceSpeed, {.min = 0})));
    g_pluginConfigValues.swipeCancelRatio   = registerPluginValue(SP<CFloatValue>(new CFloatValue("plugin:hyprspace:swipe_cancel_ratio", "Swipe travel ratio needed to complete the gesture", Config::swipeCancelRatio, {.min = 0.F, .max = 1.F})));
    g_pluginConfigValues.swipeThreshold     = registerPluginValue(SP<CFloatValue>(new CFloatValue("plugin:hyprspace:swipe_threshold", "Swipe offset threshold that toggles visible state", Config::swipeThreshold, {.min = 0.F})));
    g_pluginConfigValues.swipeClosedPadding = registerPluginValue(SP<CFloatValue>(new CFloatValue("plugin:hyprspace:swipe_closed_padding", "Closed-state overshoot padding for swipe gestures", Config::swipeClosedPadding, {.min = 0.F})));
    g_pluginConfigValues.workspaceScrollSpeed = registerPluginValue(SP<CFloatValue>(new CFloatValue("plugin:hyprspace:workspace_scroll_speed", "Horizontal panel scroll multiplier for wheel and swipe input", Config::workspaceScrollSpeed, {.min = 0.F})));

    g_pluginConfigValues.disableBlur      = registerPluginValue(SP<CBoolValue>(new CBoolValue("plugin:hyprspace:disable_blur", "Disable blur while rendering the overview", Config::disableBlur)));
    g_pluginConfigValues.overrideAnimSpeed = registerPluginValue(SP<CFloatValue>(new CFloatValue("plugin:hyprspace:override_anim_speed", "Override the window animation speed used by the overview", Config::overrideAnimSpeed, {.min = 0.F})));
    g_pluginConfigValues.dragAlpha        = registerPluginValue(SP<CFloatValue>(new CFloatValue("plugin:hyprspace:drag_alpha", "Temporary alpha applied to dragged windows inside the overview", Config::dragAlpha, {.min = 0.F, .max = 1.F})));
    g_pluginConfigValues.exitKey          = registerPluginValue(SP<CStringValue>(new CStringValue("plugin:hyprspace:exit_key", "Keysyms used to close the overview, empty disables it", Config::exitKey)));
    g_pluginConfigValues.clickReleaseThresholdMs = registerPluginValue(SP<CIntValue>(new CIntValue("plugin:hyprspace:click_release_threshold_ms", "Maximum press duration that still counts as a click", Config::clickReleaseThresholdMs, {.min = 0})));
}

} // namespace

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

std::shared_ptr<CHyprspaceWidget> getWidgetForMonitor(PHLMONITORREF monitor) {
    for (auto& widget : g_overviewWidgets) {
        if (!widget)
            continue;

        if (widget->getOwner() == monitor)
            return widget;
    }

    return nullptr;
}

void refreshWidgets() {
    for (auto& widget : g_overviewWidgets) {
        if (widget && widget->isActive())
            widget->show();
    }
}

bool  g_layoutNeedsRefresh = true;
float g_oAlpha             = -1;

void onRender(eRenderStage renderStage) {
    if (compositorUnsafe())
        return;

    if (renderStage == eRenderStage::RENDER_PRE) {
        if (g_layoutNeedsRefresh) {
            refreshWidgets();
            g_layoutNeedsRefresh = false;
        }
    } else if (renderStage == eRenderStage::RENDER_PRE_WINDOWS) {
        const auto widget = getWidgetForMonitor(g_pHyprRenderer->m_renderData.pMonitor);
        if (widget && widget->getOwner()) {
            const auto dragTarget = g_layoutManager->dragController()->target();
            const auto curWindow  = dragTarget ? dragTarget->window() : nullptr;
            if (curWindow && widget->isActive()) {
                g_oAlpha = curWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE)->goal();
                curWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE)->setValueAndWarp(0);
            } else
                g_oAlpha = -1;
        } else
            g_oAlpha = -1;
    } else if (renderStage == eRenderStage::RENDER_POST_WINDOWS) {
        const auto widget = getWidgetForMonitor(g_pHyprRenderer->m_renderData.pMonitor);
        if (!widget || !widget->getOwner()) {
            g_oAlpha = -1;
            return;
        }

        widget->draw();

        if (g_oAlpha == -1)
            return;

        const auto dragTarget = g_layoutManager->dragController()->target();
        const auto curWindow  = dragTarget ? dragTarget->window() : nullptr;
        if (!curWindow) {
            g_oAlpha = -1;
            return;
        }

        if (!pRenderWindow) {
            g_oAlpha = -1;
            return;
        }

        curWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE)->setValueAndWarp(Config::dragAlpha);
        curWindow->m_ruleApplicator->noBlur().set(true, Desktop::Types::PRIORITY_SET_PROP);
        const auto time = Time::steadyNow();
        (*(tRenderWindow)pRenderWindow)(g_pHyprRenderer.get(), curWindow, widget->getOwner(), time, true, Render::RENDER_PASS_MAIN, false, false);
        curWindow->m_ruleApplicator->noBlur().unset(Desktop::Types::PRIORITY_SET_PROP);
        curWindow->alpha(Desktop::View::WINDOW_ALPHA_ACTIVE)->setValueAndWarp(g_oAlpha);
        g_oAlpha = -1;
    }
}

void onWorkspaceChange(PHLWORKSPACE workspace) {
    if (!workspace || !workspace->m_monitor)
        return;

    const auto widget = getWidgetForMonitor(monitorFromID(workspace->m_monitor->m_id));
    if (widget && widget->isActive())
        widget->show();
}

void onMouseButton(const IPointer::SButtonEvent& event, SCallbackInfo& info) {
    const SP<IPointer> pointer = g_pSeatManager->m_mouse.lock();
    if (!pointer || event.button != BTN_LEFT)
        return;

    const auto monitor = monitorFromCursor();
    if (!monitor)
        return;

    const auto widget = getWidgetForMonitor(monitor);
    if (widget && widget->isActive())
        info.cancelled = !widget->buttonEvent(event.state == WL_POINTER_BUTTON_STATE_PRESSED, g_pInputManager->getMouseCoordsInternal());
}

void onMouseAxis(const IPointer::SAxisEvent& event, SCallbackInfo& info) {
    const auto monitor = monitorFromCursor();
    if (!monitor)
        return;

    const auto widget = getWidgetForMonitor(monitor);
    if (widget && widget->isActive())
        info.cancelled = !widget->axisEvent(event.delta, event.axis, g_pInputManager->getMouseCoordsInternal());
}

void onSwipeBegin(const IPointer::SSwipeBeginEvent& event, SCallbackInfo& info) {
    if (Config::disableGestures)
        return;

    const auto widget = getWidgetForMonitor(monitorFromCursor());
    if (widget)
        widget->beginSwipe(event);

    for (auto& other : g_overviewWidgets) {
        if (other && other != widget && other->isSwiping()) {
            IPointer::SSwipeEndEvent cancelled;
            cancelled.cancelled = true;
            other->endSwipe(cancelled);
        }
    }
}

void onSwipeUpdate(const IPointer::SSwipeUpdateEvent& event, SCallbackInfo& info) {
    if (Config::disableGestures)
        return;

    const auto widget = getWidgetForMonitor(monitorFromCursor());
    if (widget)
        info.cancelled = !widget->updateSwipe(event);
}

void onSwipeEnd(const IPointer::SSwipeEndEvent& event, SCallbackInfo& info) {
    if (Config::disableGestures)
        return;

    const auto widget = getWidgetForMonitor(monitorFromCursor());
    if (widget)
        widget->endSwipe(event);
}

void onKeyPress(const IKeyboard::SKeyEvent& event, SCallbackInfo& info) {
    const SP<IKeyboard> keyboard = g_pSeatManager->m_keyboard.lock();
    if (!keyboard || !keyboard->m_xkbSymState || Config::exitKey.empty())
        return;

    const auto        keycode      = event.keycode + 8;
    const xkb_keysym_t keysym      = xkb_state_key_get_one_sym(keyboard->m_xkbSymState, keycode);
    const xkb_keysym_t configured  = xkb_keysym_from_name(Config::exitKey.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);

    if (configured == XKB_KEY_NoSymbol || keysym != configured)
        return;

    bool overviewActive = false;
    for (auto& widget : g_overviewWidgets) {
        if (widget && widget->isActive()) {
            widget->hide();
            overviewActive = true;
        }
    }

    if (overviewActive)
        info.cancelled = true;
}

PHLMONITOR g_pTouchedMonitor;

void onTouchDown(const ITouch::SDownEvent& event, SCallbackInfo& info) {
    if (!event.device)
        return;

    auto targetMonitor = monitorFromName(!event.device->m_boundOutput.empty() ? event.device->m_boundOutput : "");
    targetMonitor      = targetMonitor ? targetMonitor : monitorFromCursor();

    const auto widget = getWidgetForMonitor(targetMonitor);
    if (widget && targetMonitor && widget->isActive()) {
        const Vector2D pos = targetMonitor->m_position + event.pos * targetMonitor->m_size;
        info.cancelled     = !widget->buttonEvent(true, pos);
        if (info.cancelled) {
            g_pTouchedMonitor = targetMonitor;
            Pointer::pointerController()->warpTo(pos);
            g_pInputManager->refocus();
        }
    }
}

void onTouchMove(const ITouch::SMotionEvent& event, SCallbackInfo& info) {
    if (!g_pTouchedMonitor)
        return;

    Pointer::pointerController()->warpTo(g_pTouchedMonitor->m_position + g_pTouchedMonitor->m_size * event.pos);
    g_pInputManager->simulateMouseMovement();
}

void onTouchUp(const ITouch::SUpEvent& event, SCallbackInfo& info) {
    const auto widget = getWidgetForMonitor(g_pTouchedMonitor);
    if (widget && g_pTouchedMonitor && widget->isActive())
        info.cancelled = !widget->buttonEvent(false, g_pInputManager->getMouseCoordsInternal());

    g_pTouchedMonitor = nullptr;
}

static SDispatchResult dispatchToggleOverview(std::string arg) {
    const auto currentMonitor = monitorFromCursor();
    const auto widget         = getWidgetForMonitor(currentMonitor);
    if (!widget)
        return {};

    if (arg.contains("all")) {
        const bool anyActive = widget->isActive();
        for (auto& overviewWidget : g_overviewWidgets) {
            if (!overviewWidget)
                continue;

            if (anyActive && overviewWidget->isActive())
                overviewWidget->hide();
            else if (!anyActive && !overviewWidget->isActive())
                overviewWidget->show();
        }
    } else
        widget->isActive() ? widget->hide() : widget->show();

    return {};
}

static SDispatchResult dispatchOpenOverview(std::string arg) {
    if (arg.contains("all")) {
        for (auto& widget : g_overviewWidgets) {
            if (widget && !widget->isActive())
                widget->show();
        }
    } else {
        const auto widget = getWidgetForMonitor(monitorFromCursor());
        if (widget && !widget->isActive())
            widget->show();
    }

    return {};
}

static SDispatchResult dispatchCloseOverview(std::string arg) {
    if (arg.contains("all")) {
        for (auto& widget : g_overviewWidgets) {
            if (widget && widget->isActive())
                widget->hide();
        }
    } else {
        const auto widget = getWidgetForMonitor(monitorFromCursor());
        if (widget && widget->isActive())
            widget->hide();
    }

    return {};
}

int luaOverview(lua_State* L) {
    const char* arg = luaL_optstring(L, 1, "toggle");
    const std::string command = arg ? arg : "toggle";

    // Pull the latest Lua-side config into the runtime snapshot before changing overview state.
    reloadConfig();

    if (command == "toggle")
        dispatchToggleOverview("");
    else if (command == "open")
        dispatchOpenOverview("");
    else if (command == "close")
        dispatchCloseOverview("");
    else if (command == "toggle_all")
        dispatchToggleOverview("all");
    else if (command == "open_all")
        dispatchOpenOverview("all");
    else if (command == "close_all")
        dispatchCloseOverview("all");
    else
        return luaL_error(L, "Hyprspace.overview: expected toggle, open, close, toggle_all, open_all, or close_all");

    return 0;
}

void* findFunctionBySymbol(HANDLE inHandle, const std::string func, const std::string sym) {
    for (const auto& match : HyprlandAPI::findFunctionsByName(inHandle, func)) {
        if (match.demangled.contains(sym))
            return match.address;
    }

    return nullptr;
}

void reloadConfig() {
    Config::panelBaseColor              = readColorValue(g_pluginConfigValues.panelColor, Config::panelBaseColor);
    Config::panelBorderColor            = readColorValue(g_pluginConfigValues.panelBorderColor, Config::panelBorderColor);
    Config::workspaceActiveBackground   = readColorValue(g_pluginConfigValues.workspaceActiveBackground, Config::workspaceActiveBackground);
    Config::workspaceInactiveBackground = readColorValue(g_pluginConfigValues.workspaceInactiveBackground, Config::workspaceInactiveBackground);
    Config::workspaceActiveBorder       = readColorValue(g_pluginConfigValues.workspaceActiveBorder, Config::workspaceActiveBorder);
    Config::workspaceInactiveBorder     = readColorValue(g_pluginConfigValues.workspaceInactiveBorder, Config::workspaceInactiveBorder);

    Config::panelHeight          = readIntValue(g_pluginConfigValues.panelHeight, Config::panelHeight);
    Config::panelBorderWidth     = readIntValue(g_pluginConfigValues.panelBorderWidth, Config::panelBorderWidth);
    Config::workspaceMargin      = readIntValue(g_pluginConfigValues.workspaceMargin, Config::workspaceMargin);
    Config::reservedArea         = readIntValue(g_pluginConfigValues.reservedArea, Config::reservedArea);
    Config::workspaceBorderSize  = readIntValue(g_pluginConfigValues.workspaceBorderSize, Config::workspaceBorderSize);
    Config::adaptiveHeight       = readBoolValue(g_pluginConfigValues.adaptiveHeight, Config::adaptiveHeight);
    Config::centerAligned        = readBoolValue(g_pluginConfigValues.centerAligned, Config::centerAligned);
    Config::onBottom             = readBoolValue(g_pluginConfigValues.onBottom, Config::onBottom);
    Config::hideBackgroundLayers = readBoolValue(g_pluginConfigValues.hideBackgroundLayers, Config::hideBackgroundLayers);
    Config::hideTopLayers        = readBoolValue(g_pluginConfigValues.hideTopLayers, Config::hideTopLayers);
    Config::hideOverlayLayers    = readBoolValue(g_pluginConfigValues.hideOverlayLayers, Config::hideOverlayLayers);
    Config::drawActiveWorkspace  = readBoolValue(g_pluginConfigValues.drawActiveWorkspace, Config::drawActiveWorkspace);
    Config::hideRealLayers       = readBoolValue(g_pluginConfigValues.hideRealLayers, Config::hideRealLayers);
    Config::affectStrut          = readBoolValue(g_pluginConfigValues.affectStrut, Config::affectStrut);

    Config::autoDrag             = readBoolValue(g_pluginConfigValues.autoDrag, Config::autoDrag);
    Config::autoScroll           = readBoolValue(g_pluginConfigValues.autoScroll, Config::autoScroll);
    Config::exitOnClick          = readBoolValue(g_pluginConfigValues.exitOnClick, Config::exitOnClick);
    Config::switchOnDrop         = readBoolValue(g_pluginConfigValues.switchOnDrop, Config::switchOnDrop);
    Config::exitOnSwitch         = readBoolValue(g_pluginConfigValues.exitOnSwitch, Config::exitOnSwitch);
    Config::showNewWorkspace     = readBoolValue(g_pluginConfigValues.showNewWorkspace, Config::showNewWorkspace);
    Config::showEmptyWorkspace   = readBoolValue(g_pluginConfigValues.showEmptyWorkspace, Config::showEmptyWorkspace);
    Config::showSpecialWorkspace = readBoolValue(g_pluginConfigValues.showSpecialWorkspace, Config::showSpecialWorkspace);

    Config::disableGestures      = readBoolValue(g_pluginConfigValues.disableGestures, Config::disableGestures);
    Config::reverseSwipe         = readBoolValue(g_pluginConfigValues.reverseSwipe, Config::reverseSwipe);
    Config::swipeFingers         = readIntValue(g_pluginConfigValues.swipeFingers, Config::swipeFingers);
    Config::swipeDistance        = readIntValue(g_pluginConfigValues.swipeDistance, Config::swipeDistance);
    Config::swipeForceSpeed      = readIntValue(g_pluginConfigValues.swipeForceSpeed, Config::swipeForceSpeed);
    Config::swipeCancelRatio     = readFloatValue(g_pluginConfigValues.swipeCancelRatio, Config::swipeCancelRatio);
    Config::swipeThreshold       = readFloatValue(g_pluginConfigValues.swipeThreshold, Config::swipeThreshold);
    Config::swipeClosedPadding   = readFloatValue(g_pluginConfigValues.swipeClosedPadding, Config::swipeClosedPadding);
    Config::workspaceScrollSpeed = readFloatValue(g_pluginConfigValues.workspaceScrollSpeed, Config::workspaceScrollSpeed);

    Config::disableBlur             = readBoolValue(g_pluginConfigValues.disableBlur, Config::disableBlur);
    Config::overrideAnimSpeed       = readFloatValue(g_pluginConfigValues.overrideAnimSpeed, Config::overrideAnimSpeed);
    Config::dragAlpha               = readFloatValue(g_pluginConfigValues.dragAlpha, Config::dragAlpha);
    Config::exitKey                 = readStringValue(g_pluginConfigValues.exitKey, Config::exitKey);
    Config::clickReleaseThresholdMs = readIntValue(g_pluginConfigValues.clickReleaseThresholdMs, Config::clickReleaseThresholdMs);

    for (auto& widget : g_overviewWidgets) {
        if (!widget)
            continue;

        if (widget->isActive())
            widget->hide();
        endSwipeIfNeeded(widget);
        widget->updateConfig();
    }
}

void registerMonitors() {
    for (auto& monitor : State::monitorState()->monitors()) {
        if (getWidgetForMonitor(monitor))
            continue;

        g_overviewWidgets.emplace_back(std::make_shared<CHyprspaceWidget>(monitor->m_id));
    }
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE inHandle) {
    pHandle = inHandle;

    Log::logger->log(Log::DEBUG, "Loading Hyprspace");

    registerConfigValues();
    HyprlandAPI::addLuaFunction(pHandle, "Hyprspace", "overview", ::luaOverview);

    g_pConfigReloadHook = Event::bus()->m_events.config.reloaded.listen([]() { reloadConfig(); });
    g_pStartHook        = Event::bus()->m_events.start.listen([]() {
        reloadConfig();
        registerMonitors();
    });
    HyprlandAPI::reloadConfig();

    HyprlandAPI::addDispatcherV2(pHandle, "overview:toggle", ::dispatchToggleOverview);
    HyprlandAPI::addDispatcherV2(pHandle, "overview:open", ::dispatchOpenOverview);
    HyprlandAPI::addDispatcherV2(pHandle, "overview:close", ::dispatchCloseOverview);

    g_pRenderHook          = Event::bus()->m_events.render.stage.listen([](eRenderStage stage) { onRender(stage); });
    g_pOpenLayerHook       = Event::bus()->m_events.layer.opened.listen([](PHLLS) { g_layoutNeedsRefresh = true; });
    g_pCloseLayerHook      = Event::bus()->m_events.layer.closed.listen([](PHLLS) { g_layoutNeedsRefresh = true; });
    g_pMouseButtonHook     = listenCancellable<IPointer::SButtonEvent>(Event::bus()->m_events.input.mouse.button, onMouseButton);
    g_pMouseAxisHook       = listenCancellable<IPointer::SAxisEvent>(Event::bus()->m_events.input.mouse.axis, onMouseAxis);
    g_pTouchDownHook       = listenCancellable<ITouch::SDownEvent>(Event::bus()->m_events.input.touch.down, onTouchDown);
    g_pTouchMoveHook       = listenCancellable<ITouch::SMotionEvent>(Event::bus()->m_events.input.touch.motion, onTouchMove);
    g_pTouchUpHook         = listenCancellable<ITouch::SUpEvent>(Event::bus()->m_events.input.touch.up, onTouchUp);
    g_pSwipeBeginHook      = listenCancellable<IPointer::SSwipeBeginEvent>(Event::bus()->m_events.gesture.swipe.begin, onSwipeBegin);
    g_pSwipeUpdateHook     = listenCancellable<IPointer::SSwipeUpdateEvent>(Event::bus()->m_events.gesture.swipe.update, onSwipeUpdate);
    g_pSwipeEndHook        = listenCancellable<IPointer::SSwipeEndEvent>(Event::bus()->m_events.gesture.swipe.end, onSwipeEnd);
    g_pKeyPressHook        = listenCancellable<IKeyboard::SKeyEvent>(Event::bus()->m_events.input.keyboard.key, onKeyPress);
    g_pSwitchWorkspaceHook = Event::bus()->m_events.workspace.active.listen(onWorkspaceChange);

    pRenderWindow = findFunctionBySymbol(pHandle, "renderWindow", "IHyprRenderer::renderWindow");
    if (!pRenderWindow)
        pRenderWindow = findFunctionBySymbol(pHandle, "renderWindow", "CHyprRenderer::renderWindow");

    pRenderLayer = findFunctionBySymbol(pHandle, "renderLayer", "IHyprRenderer::renderLayer");
    if (!pRenderLayer)
        pRenderLayer = findFunctionBySymbol(pHandle, "renderLayer", "CHyprRenderer::renderLayer");

    registerMonitors();
    g_pAddMonitorHook        = Event::bus()->m_events.monitor.added.listen([](PHLMONITOR) { registerMonitors(); });
    g_pPreRemoveMonitorHook  = Event::bus()->m_events.monitor.preRemoved.listen([](PHLMONITOR monitor) { teardownMonitorWidget(monitor); });
    g_pRemoveMonitorHook     = Event::bus()->m_events.monitor.removed.listen([](PHLMONITOR monitor) { removeMonitorWidget(monitor); });

    return {"Hyprspace", "Workspace overview", "KZdkm", "0.3"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_pRenderHook.reset();
    g_pConfigReloadHook.reset();
    g_pOpenLayerHook.reset();
    g_pCloseLayerHook.reset();
    g_pMouseButtonHook.reset();
    g_pMouseAxisHook.reset();
    g_pTouchDownHook.reset();
    g_pTouchMoveHook.reset();
    g_pTouchUpHook.reset();
    g_pSwipeBeginHook.reset();
    g_pSwipeUpdateHook.reset();
    g_pSwipeEndHook.reset();
    g_pKeyPressHook.reset();
    g_pSwitchWorkspaceHook.reset();
    g_pAddMonitorHook.reset();
    g_pPreRemoveMonitorHook.reset();
    g_pRemoveMonitorHook.reset();
    g_pStartHook.reset();

    for (auto& widget : g_overviewWidgets) {
        if (!widget)
            continue;

        endSwipeIfNeeded(widget);
        if (widget->isActive())
            widget->hide();
        widget->cleanup(widget->getOwner());
    }
    g_overviewWidgets.clear();

    pRenderWindow = nullptr;
    pRenderLayer  = nullptr;
    pHandle       = nullptr;
}
