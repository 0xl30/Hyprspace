#include "Overview.hpp"
#include "Globals.hpp"

#include <algorithm>

#include <hyprland/src/config/shared/animation/AnimationTree.hpp>

namespace {

double closedSwipeOffset() {
    return -Config::swipeClosedPadding;
}

double shownSwipeOffset(PHLMONITOR owner) {
    return panelTravel(owner);
}

void requestFullMonitorRedraw(PHLMONITOR owner) {
    if (!owner)
        return;

    owner->m_damage.damageEntire();
    owner->scheduleFrame();
}

PHLANIMVAR<float>& layerFadeAlpha(PHLLS layer) {
    return layer->alpha()[Desktop::View::LS_ALPHA_FADE];
}

} // namespace

CHyprspaceWidget::CHyprspaceWidget(uint64_t inOwnerID) : ownerID(inOwnerID) {
    resetAnimationState(getOwner());
}

CHyprspaceWidget::~CHyprspaceWidget() {
    cleanup(getOwner());
    releaseAnimations();
}

void CHyprspaceWidget::restoreHiddenLayers() {
    for (const auto& [layer, alpha] : oLayerAlpha) {
        if (!layer || !layer->m_mapped)
            continue;

        *layerFadeAlpha(layer) = alpha;
    }

    oLayerAlpha.clear();
}

void CHyprspaceWidget::restoreFullscreenWindows() {
    for (const auto& [windowRef, modes] : prevFullscreen) {
        const auto window = windowRef.lock();
        if (!window)
            continue;

        Fullscreen::controller()->setFullscreenMode(window, modes.internal, modes.client);
        if (modes.internal == Fullscreen::FSMODE_FULLSCREEN)
            window->m_wantsInitialFullscreen = false;
    }

    prevFullscreen.clear();
}

void CHyprspaceWidget::releaseAnimations() {
    if (curYOffset) {
        curYOffset->warp(true);
        curYOffset.reset();
    }

    if (workspaceScrollOffset) {
        workspaceScrollOffset->warp(true);
        workspaceScrollOffset.reset();
    }

    m_animationConfig.reset();
}

bool CHyprspaceWidget::animationsOk() const {
    return curYOffset && curYOffset->ok() && workspaceScrollOffset && workspaceScrollOffset->ok();
}

void CHyprspaceWidget::resetAnimationState(PHLMONITOR owner) {
    releaseAnimations();

    const auto base = Config::animationTree()->getAnimationPropertyConfig("windows");
    m_animationConfig = makeShared<Hyprutils::Animation::SAnimationPropertyConfig>(*base);
    if (Config::overrideAnimSpeed > 0)
        m_animationConfig->internalSpeed = Config::overrideAnimSpeed;
    m_animationConfig->pValues = m_animationConfig;
    curAnimationConfig           = *m_animationConfig;

    Animation::mgr()->createAnimation(0.F, curYOffset, m_animationConfig, AVARDAMAGE_ENTIRE);
    Animation::mgr()->createAnimation(0.F, workspaceScrollOffset, m_animationConfig, AVARDAMAGE_ENTIRE);

    const auto hiddenOffset = panelTravel(owner);
    curYOffset->setValueAndWarp(active ? 0.F : hiddenOffset);
    workspaceScrollOffset->setValueAndWarp(0.F);
    curSwipeOffset = active ? shownSwipeOffset(owner) : closedSwipeOffset();
}

void CHyprspaceWidget::cleanup(PHLMONITOR owner) {
    restoreHiddenLayers();
    restoreFullscreenWindows();
    workspaceBoxes.clear();
    swiping           = false;
    activeBeforeSwipe = false;
    avgSwipeSpeed     = 0.;
    swipePoints       = 0;
    active            = false;

    if (owner) {
        owner->m_reservedArea = Desktop::CReservedArea();
        g_pHyprRenderer->arrangeLayersForMonitor(ownerID);
        g_layoutManager->recalculateMonitor(owner);
        requestFullMonitorRedraw(owner);
    }
}

PHLMONITOR CHyprspaceWidget::getOwner() {
    return monitorFromID(ownerID);
}

void CHyprspaceWidget::show() {
    auto owner = getOwner();
    if (!owner || !owner->m_enabled || compositorUnsafe() || !animationsOk())
        return;

    if (prevFullscreen.empty()) {
        for (auto& wsRef : State::workspaceState()->workspaces()) {
            const auto ws = wsRef.lock();
            if (!ws || !ws->m_monitor || ws->m_monitor->m_id != ownerID)
                continue;

            const auto window = Fullscreen::controller()->getFullscreenWindow(ws);
            const auto modes  = Fullscreen::controller()->getFullscreenModes(ws);
            if (!window || modes.internal == Fullscreen::FSMODE_NONE)
                continue;

            if (modes.internal == Fullscreen::FSMODE_FULLSCREEN)
                window->m_wantsInitialFullscreen = true;

            prevFullscreen.emplace_back(PHLWINDOWREF(window), modes);
            Fullscreen::controller()->setFullscreenMode(window, Fullscreen::FSMODE_NONE, Fullscreen::FSMODE_NONE);
        }
    }

    if (oLayerAlpha.empty() && Config::hideRealLayers) {
        for (int layerIdx : {2, 3}) {
            for (auto& layerRef : owner->m_layerSurfaceLayers[layerIdx]) {
                const auto layer = layerRef.lock();
                if (!layer)
                    continue;

                auto& fade = layerFadeAlpha(layer);
                oLayerAlpha.emplace_back(layer, fade->goal());
                *fade = 0.F;
            }
        }
    }

    active = true;

    if (!swiping) {
        *curYOffset    = 0.F;
        curSwipeOffset = shownSwipeOffset(owner);
    }

    updateLayout();
    g_pHyprRenderer->damageMonitor(owner);
    requestFullMonitorRedraw(owner);
}

void CHyprspaceWidget::hide() {
    auto owner = getOwner();
    if (!owner || !animationsOk())
        return;

    restoreHiddenLayers();
    restoreFullscreenWindows();

    active = false;

    if (!swiping) {
        *curYOffset    = shownSwipeOffset(owner);
        curSwipeOffset = closedSwipeOffset();
    }

    updateLayout();
    requestFullMonitorRedraw(owner);
}

void CHyprspaceWidget::updateConfig() {
    resetAnimationState(getOwner());
}

bool CHyprspaceWidget::isActive() {
    return active;
}
