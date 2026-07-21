#include "Overview.hpp"
#include "Globals.hpp"
#include <algorithm>
#include <unordered_map>

#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/RendererHintsPassElement.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

namespace {

struct SWorkspaceWindows {
    std::vector<PHLWINDOW> tiled;
    std::vector<PHLWINDOW> floating;
};

void renderRect(CBox box, CHyprColor color) {
    CRectPassElement::SRectData rectData;
    rectData.color = color;
    rectData.box   = box;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rectData));
}

void renderRectWithBlur(CBox box, CHyprColor color) {
    CRectPassElement::SRectData rectData;
    rectData.color = color;
    rectData.box   = box;
    rectData.blur  = true;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rectData));
}

void renderBorder(CBox box, const Config::CGradientValueData& gradient, int size) {
    CBorderPassElement::SBorderData borderData;
    borderData.box        = box;
    borderData.grad1      = gradient;
    borderData.round      = 0;
    borderData.a          = 1.F;
    borderData.borderSize = size;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(borderData));
}

void renderWindowStub(PHLWINDOW window, PHLMONITOR monitor, PHLWORKSPACE workspaceOverride, CBox rectOverride, const Time::steady_tp& time) {
    if (!window || !monitor || !workspaceOverride)
        return;

    if (!window->m_isMapped || !window->wlSurface() || !window->wlSurface()->resource() || !window->m_ruleApplicator)
        return;

    Render::SRenderModifData renderModif;

    const auto  workspace    = window->m_workspace;
    const auto  fsModes      = Fullscreen::controller()->getFullscreenModes(window);
    const auto  realPosition = window->positionAnimation()->value();
    const auto  realSize     = window->sizeAnimation()->value();
    const auto  pinned       = window->m_pinned;
    const auto  floating     = window->m_isFloating;
    const float logicalW            = std::max(static_cast<float>(realSize.x), 5.F);
    const float scaleMod            = rectOverride.w / std::max(logicalW * monitor->m_scale, 5.F);
    if (!(scaleMod > 0.F) || !(rectOverride.w > 0 && rectOverride.h > 0))
        return;

    const Vector2D logicalTL = realPosition + window->m_floatingOffset;
    const Vector2D scaledTL  = (logicalTL - monitor->m_position) * monitor->m_scale;
    const Vector2D translate = rectOverride.pos() / scaleMod - scaledTL;

    renderModif.modifs.push_back(std::make_pair(Render::SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE, std::any(translate)));
    renderModif.modifs.push_back(std::make_pair(Render::SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, std::any(scaleMod)));
    renderModif.enabled = true;

    window->m_workspace  = workspaceOverride;
    window->m_ruleApplicator->nearestNeighbor().set(false, Desktop::Types::PRIORITY_SET_PROP);
    window->m_isFloating = false;
    window->m_pinned     = true;
    window->m_ruleApplicator->rounding().set(window->rounding() * scaleMod * monitor->m_scale, Desktop::Types::PRIORITY_SET_PROP);

    const bool wasFullscreen = fsModes.internal != Fullscreen::FSMODE_NONE;
    if (wasFullscreen)
        Fullscreen::controller()->setFullscreenMode(window, Fullscreen::FSMODE_NONE, Fullscreen::FSMODE_NONE);

    Hyprutils::Utils::CScopeGuard restoreWindowState([&] {
        if (wasFullscreen)
            Fullscreen::controller()->setFullscreenMode(window, fsModes.internal, fsModes.client);
        window->m_workspace  = workspace;
        window->m_ruleApplicator->nearestNeighbor().unset(Desktop::Types::PRIORITY_SET_PROP);
        window->m_isFloating = floating;
        window->m_pinned     = pinned;
        window->m_ruleApplicator->rounding().unset(Desktop::Types::PRIORITY_SET_PROP);
    });

    g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{.renderModif = renderModif}));
    Hyprutils::Utils::CScopeGuard clearHints([] {
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{.renderModif = Render::SRenderModifData{}}));
    });

    g_pHyprRenderer->damageWindow(window);
    if (pRenderWindow)
        (*(tRenderWindow)pRenderWindow)(g_pHyprRenderer.get(), window, monitor, time, true, Render::RENDER_PASS_ALL, false, false);
}

void renderLayerStub(PHLLS layer, PHLMONITOR monitor, CBox rectOverride, const Time::steady_tp& time) {
    if (!layer || !monitor)
        return;

    if (!layer->m_mapped || !layer->m_layerSurface || !layer->wlSurface() || !layer->wlSurface()->resource())
        return;

    const Vector2D realPosition = layer->positionAnimation()->value();
    const Vector2D realSize     = layer->sizeAnimation()->value();
    auto&          fadeAlpha    = layer->alpha()[Desktop::View::LS_ALPHA_FADE];
    const float    alpha        = fadeAlpha->value();

    const float scale = rectOverride.w / realSize.x;
    if (!(scale > 0.F) || !(rectOverride.w > 0 && rectOverride.h > 0))
        return;

    Render::SRenderModifData renderModif;
    renderModif.modifs.push_back(std::make_pair(Render::SRenderModifData::eRenderModifType::RMOD_TYPE_TRANSLATE, std::any(monitor->m_position + (rectOverride.pos() / scale) - realPosition)));
    renderModif.modifs.push_back(std::make_pair(Render::SRenderModifData::eRenderModifType::RMOD_TYPE_SCALE, std::any(scale)));
    renderModif.enabled = true;

    fadeAlpha->setValueAndWarp(1.F);

    g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{.renderModif = renderModif}));
    Hyprutils::Utils::CScopeGuard clearHints([] {
        g_pHyprRenderer->m_renderPass.add(makeUnique<CRendererHintsPassElement>(CRendererHintsPassElement::SData{.renderModif = Render::SRenderModifData{}}));
    });
    if (pRenderLayer)
        (*(tRenderLayer)pRenderLayer)(g_pHyprRenderer.get(), layer, monitor, time, false, false);

    fadeAlpha->setValueAndWarp(alpha);
}

bool renderWindowPreview(PHLWINDOW window, PHLWORKSPACE workspace, PHLMONITOR owner, double workspaceX, double workspaceY, double monitorScaleFactor, const Time::steady_tp& time) {
    if (!window || !workspace || !owner)
        return false;

    const double wX = workspaceX + ((window->positionAnimation()->value().x - owner->m_position.x) * monitorScaleFactor * owner->m_scale);
    const double wY = workspaceY + ((window->positionAnimation()->value().y - owner->m_position.y) * monitorScaleFactor * owner->m_scale);
    const double wW = window->sizeAnimation()->value().x * monitorScaleFactor * owner->m_scale;
    const double wH = window->sizeAnimation()->value().y * monitorScaleFactor * owner->m_scale;
    if (!(wW > 0 && wH > 0))
        return false;

    renderWindowStub(window, owner, workspace, {wX, wY, wW, wH}, time);
    return true;
}

} // namespace

void CHyprspaceWidget::draw() {
    workspaceBoxes.clear();

    if (compositorUnsafe() || !animationsOk())
        return;

    if (!active && !curYOffset->isBeingAnimated())
        return;

    const auto owner = getOwner();
    if (!owner || !owner->m_enabled)
        return;

    const CBox monitorClip = {{0, 0}, owner->m_transformedSize};
    const auto time        = Time::steadyNow();

    owner->m_blurFBShouldRender = true;

    const int panelDirection = Config::onBottom ? -1 : 1;

    CBox panelBox = {owner->m_position.x,
                     owner->m_position.y + (Config::onBottom * (owner->m_transformedSize.y - ((Config::panelHeight + Config::reservedArea) * owner->m_scale))) - (panelDirection * curYOffset->value()),
                     owner->m_transformedSize.x,
                     (Config::panelHeight + Config::reservedArea) * owner->m_scale};

    panelBox.x -= owner->m_position.x;
    panelBox.y -= owner->m_position.y;

    g_pHyprRenderer->m_renderData.clipBox = monitorClip;

    if (!Config::disableBlur)
        renderRectWithBlur(panelBox, Config::panelBaseColor);
    else
        renderRect(panelBox, Config::panelBaseColor);

    if (Config::panelBorderWidth > 0) {
        CBox borderBox = {panelBox.x,
                          owner->m_position.y + (Config::onBottom * owner->m_transformedSize.y) + (Config::panelHeight + Config::reservedArea - curYOffset->value() * owner->m_scale) * panelDirection,
                          owner->m_transformedSize.x,
                          static_cast<double>(Config::panelBorderWidth)};
        borderBox.y -= owner->m_position.y;
        renderRect(borderBox, Config::panelBorderColor);
    }

    g_pHyprRenderer->damageMonitor(owner);

    std::vector<WORKSPACEID> workspaces;

    if (Config::showSpecialWorkspace)
        workspaces.push_back(SPECIAL_WORKSPACE_START);

    WORKSPACEID highestID = 1;
    for (const auto& wsRef : State::workspaceState()->workspaces()) {
        const auto ws = wsRef.lock();
        if (!ws || ws->m_id < 1 || !ws->m_monitor || ws->m_monitor->m_id != ownerID)
            continue;

        workspaces.push_back(ws->m_id);
        highestID = std::max(highestID, ws->m_id);
    }

    if (Config::showEmptyWorkspace) {
        const WORKSPACEID wsIDStart = 1;
        const WORKSPACEID wsIDEnd   = highestID;

        for (WORKSPACEID id = wsIDStart; id <= wsIDEnd; id++) {
            if (id == owner->activeSpecialWorkspaceID())
                continue;

            if (workspaceByID(id) == nullptr)
                workspaces.push_back(id);
        }
    }

    if (Config::showNewWorkspace) {
        while (workspaceByID(highestID) != nullptr)
            highestID++;
        workspaces.push_back(highestID);
    }

    std::sort(workspaces.begin(), workspaces.end());
    workspaces.erase(std::unique(workspaces.begin(), workspaces.end()), workspaces.end());

    const auto workspaceCount = static_cast<int>(workspaces.size());
    if (workspaceCount == 0)
        return;

    const double monitorScaleFactor = ((Config::panelHeight - 2 * Config::workspaceMargin) / owner->m_transformedSize.y) * owner->m_scale;
    const double workspaceBoxW      = owner->m_transformedSize.x * monitorScaleFactor;
    const double workspaceBoxH      = owner->m_transformedSize.y * monitorScaleFactor;
    const double workspaceGroupW    = workspaceBoxW * workspaceCount + (Config::workspaceMargin * owner->m_scale) * (workspaceCount - 1);
    double       workspaceOffsetX   = Config::centerAligned ? workspaceScrollOffset->value() + (panelBox.w / 2.) - (workspaceGroupW / 2.) : workspaceScrollOffset->value() + Config::workspaceMargin;
    const double workspaceOffsetY   = !Config::onBottom ? (((Config::reservedArea + Config::workspaceMargin) * owner->m_scale) - curYOffset->value()) : (owner->m_transformedSize.y - ((Config::reservedArea + Config::workspaceMargin) * owner->m_scale) - workspaceBoxH + curYOffset->value());
    const double workspaceOverflow  = std::max<double>(((workspaceGroupW - panelBox.w) / 2.) + (Config::workspaceMargin * owner->m_scale), 0.);

    *workspaceScrollOffset = std::clamp<double>(workspaceScrollOffset->goal(), -workspaceOverflow, workspaceOverflow);

    if (!(workspaceBoxW > 0 && workspaceBoxH > 0))
        return;

    std::unordered_map<WORKSPACEID, SWorkspaceWindows> windowsByWorkspace;
    windowsByWorkspace.reserve(workspaceCount + 2);
    for (const auto& window : Desktop::windowState()->windows()) {
        if (!window || !window->m_workspace)
            continue;

        auto& bucket = windowsByWorkspace[window->m_workspace->m_id];
        if (window->m_isFloating)
            bucket.floating.push_back(window);
        else
            bucket.tiled.push_back(window);
    }

    for (const auto wsID : workspaces) {
        const auto ws  = workspaceByID(wsID);
        CBox       box = {workspaceOffsetX, workspaceOffsetY, workspaceBoxW, workspaceBoxH};

        if (ws == owner->m_activeWorkspace) {
            if (Config::workspaceBorderSize >= 1 && Config::workspaceActiveBorder.a > 0)
                renderBorder(box, Config::CGradientValueData(Config::workspaceActiveBorder), Config::workspaceBorderSize);

            if (!Config::disableBlur)
                renderRectWithBlur(box, Config::workspaceActiveBackground);
            else
                renderRect(box, Config::workspaceActiveBackground);

            if (!Config::drawActiveWorkspace) {
                workspaceOffsetX += workspaceBoxW + (Config::workspaceMargin * owner->m_scale);
                continue;
            }
        } else {
            if (Config::workspaceBorderSize >= 1 && Config::workspaceInactiveBorder.a > 0)
                renderBorder(box, Config::CGradientValueData(Config::workspaceInactiveBorder), Config::workspaceBorderSize);

            if (!Config::disableBlur)
                renderRectWithBlur(box, Config::workspaceInactiveBackground);
            else
                renderRect(box, Config::workspaceInactiveBackground);
        }

        if (!Config::hideBackgroundLayers) {
            for (const auto& layerRef : owner->m_layerSurfaceLayers[0]) {
                const auto layer = layerRef.lock();
                if (!layer)
                    continue;

                CBox layerBox = {box.pos() + (layer->positionAnimation()->value() - owner->m_position) * monitorScaleFactor, layer->sizeAnimation()->value() * monitorScaleFactor};
                renderLayerStub(layer, owner, layerBox, time);
            }

            for (const auto& layerRef : owner->m_layerSurfaceLayers[1]) {
                const auto layer = layerRef.lock();
                if (!layer)
                    continue;

                CBox layerBox = {box.pos() + (layer->positionAnimation()->value() - owner->m_position) * monitorScaleFactor, layer->sizeAnimation()->value() * monitorScaleFactor};
                renderLayerStub(layer, owner, layerBox, time);
            }
        }

        if (owner->m_activeWorkspace == ws && Config::affectStrut) {
            CBox miniPanelBox = {workspaceOffsetX, workspaceOffsetY, panelBox.w * monitorScaleFactor, panelBox.h * monitorScaleFactor};
            if (Config::onBottom)
                miniPanelBox = {workspaceOffsetX, workspaceOffsetY + workspaceBoxH - panelBox.h * monitorScaleFactor, panelBox.w * monitorScaleFactor, panelBox.h * monitorScaleFactor};

            if (!Config::disableBlur)
                renderRectWithBlur(miniPanelBox, CHyprColor(0, 0, 0, 0));
            else
                renderRect(miniPanelBox, CHyprColor(0, 0, 0, 0));
        }

        if (ws) {
            const auto windowsIt = windowsByWorkspace.find(ws->m_id);
            if (windowsIt != windowsByWorkspace.end()) {
                for (const auto& window : windowsIt->second.tiled)
                    renderWindowPreview(window, ws, owner, workspaceOffsetX, workspaceOffsetY, monitorScaleFactor, time);

                const auto focused = ws->getLastFocusedWindow();
                for (const auto& window : windowsIt->second.floating) {
                    if (window == focused)
                        continue;

                    renderWindowPreview(window, ws, owner, workspaceOffsetX, workspaceOffsetY, monitorScaleFactor, time);
                }

                if (focused && focused->m_isFloating)
                    renderWindowPreview(focused, ws, owner, workspaceOffsetX, workspaceOffsetY, monitorScaleFactor, time);
            }
        }

        if (owner->m_activeWorkspace != ws || !Config::hideRealLayers) {
            if (!Config::hideTopLayers) {
                for (const auto& layerRef : owner->m_layerSurfaceLayers[2]) {
                    const auto layer = layerRef.lock();
                    if (!layer)
                        continue;

                    CBox layerBox = {box.pos() + (layer->positionAnimation()->value() - owner->m_position) * monitorScaleFactor, layer->sizeAnimation()->value() * monitorScaleFactor};
                    renderLayerStub(layer, owner, layerBox, time);
                }
            }

            if (!Config::hideOverlayLayers) {
                for (const auto& layerRef : owner->m_layerSurfaceLayers[3]) {
                    const auto layer = layerRef.lock();
                    if (!layer)
                        continue;

                    CBox layerBox = {box.pos() + (layer->positionAnimation()->value() - owner->m_position) * monitorScaleFactor, layer->sizeAnimation()->value() * monitorScaleFactor};
                    renderLayerStub(layer, owner, layerBox, time);
                }
            }
        }

        // Input hitboxes are evaluated in monitor-space logical coordinates.
        box.scale(1 / owner->m_scale);
        box.x += owner->m_position.x;
        box.y += owner->m_position.y;
        workspaceBoxes.emplace_back(wsID, box);

        workspaceOffsetX += workspaceBoxW + Config::workspaceMargin * owner->m_scale;
    }

    g_pHyprRenderer->m_renderData.clipBox = monitorClip;
}
