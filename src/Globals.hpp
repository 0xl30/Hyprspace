#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include <hyprutils/memory/SharedPtr.hpp>

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/animation/AnimationManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/types.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/pointer/PointerController.hpp>

// Hyprland v0.54+: cancellable input uses Event::SCallbackInfo (not legacy CEvent*).
using SCallbackInfo = Event::SCallbackInfo;

// Must match Hyprutils::Signal::CSignalT::RefArg (hyprutils/signal/Signal.hpp).
template <typename T>
using HyprSignalRefArg = std::conditional_t<std::is_trivially_copyable_v<T>, T, const T&>;

// Unpack Hyprutils::CSignalT::emit() tuple — first event arg is often stored by value (trivial types).
template <typename EventType, typename Signal>
CHyprSignalListener listenCancellable(Signal& signal, std::function<void(const EventType&, SCallbackInfo&)> handler) {
    struct Hack : Hyprutils::Signal::CSignalBase {
        using CSignalBase::registerListenerInternal;
    };
    return reinterpret_cast<Hack&>(signal).registerListenerInternal([handler](void* args) {
        using Tuple = std::tuple<HyprSignalRefArg<EventType>, HyprSignalRefArg<Event::SCallbackInfo&>>;
        auto* tup = static_cast<Tuple*>(args);
        handler(std::get<0>(*tup), std::get<1>(*tup));
    });
}

// Hyprland v0.56+: compositor state moved into dedicated trackers.
inline PHLMONITOR monitorFromID(MONITORID id) {
    return State::monitorState()->query().id(id).run();
}

inline PHLMONITOR monitorFromName(std::string_view name) {
    if (name.empty())
        return nullptr;
    return State::monitorState()->query().name(name).run();
}

inline PHLMONITOR monitorFromCursor() {
    return State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
}

inline PHLWORKSPACE workspaceByID(WORKSPACEID id) {
    return State::workspaceState()->query().id(id).run();
}

inline PHLWORKSPACE createWorkspace(WORKSPACEID id, MONITORID monid) {
    return State::workspaceState()->create(id, monid);
}

inline bool compositorUnsafe() {
    return !g_pCompositor || !g_pCompositor->m_sessionActive || State::monitorState()->monitors().empty();
}

inline HANDLE pHandle = NULL;

typedef void (*tRenderWindow)(void*, PHLWINDOW, PHLMONITOR, const Time::steady_tp&, bool, Render::eRenderPassMode, bool, bool);
typedef void (*tRenderLayer)(void*, PHLLS, PHLMONITOR, const Time::steady_tp&, bool, bool);
extern void* pRenderWindow;
extern void* pRenderLayer;
namespace Config {
    extern CHyprColor panelBaseColor;
    extern CHyprColor panelBorderColor;
    extern CHyprColor workspaceActiveBackground;
    extern CHyprColor workspaceInactiveBackground;
    extern CHyprColor workspaceActiveBorder;
    extern CHyprColor workspaceInactiveBorder;

    extern int panelHeight;
    extern int panelBorderWidth;
    extern int workspaceMargin;
    extern int reservedArea;
    extern int workspaceBorderSize;
    extern bool adaptiveHeight;
    extern bool centerAligned;
    extern bool onBottom;
    extern bool hideBackgroundLayers;
    extern bool hideTopLayers;
    extern bool hideOverlayLayers;
    extern bool drawActiveWorkspace;
    extern bool hideRealLayers;
    extern bool affectStrut;

    extern bool autoDrag;
    extern bool autoScroll;
    extern bool exitOnClick;
    extern bool switchOnDrop;
    extern bool exitOnSwitch;
    extern bool showNewWorkspace;
    extern bool showEmptyWorkspace;
    extern bool showSpecialWorkspace;

    extern bool disableGestures;
    extern bool reverseSwipe;

    extern bool disableBlur;
    extern float overrideAnimSpeed;
    extern float dragAlpha;
    extern std::string exitKey;

    extern int clickReleaseThresholdMs;
    extern int swipeFingers;
    extern int swipeDistance;
    extern int swipeForceSpeed;
    extern float swipeCancelRatio;
    extern float swipeThreshold;
    extern float swipeClosedPadding;
    extern float workspaceScrollSpeed;
}

inline double panelTravel(PHLMONITOR owner) {
    if (!owner)
        return 0.;

    return (Config::panelHeight + Config::reservedArea) * owner->m_scale;
}
