#include "Overview.hpp"
#include "Globals.hpp"
#include <hyprland/src/config/legacy/ConfigManager.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

// FIXME: preserve original workspace rules
void CHyprspaceWidget::updateLayout() {

    if (!config.affectStrut->value()) return;

    const auto currentHeight = config.panelHeight->value() + config.reservedArea->value();
    const auto pMonitor = getOwner();
    if (!pMonitor) return;

    static auto PGAPSINDATA  = CConfigValue<Config::IComplexConfigValue>("general:gaps_in");
    static auto PGAPSOUTDATA = CConfigValue<Config::IComplexConfigValue>("general:gaps_out");
    if (!PGAPSINDATA.good() || !PGAPSOUTDATA.good()) return;

    const auto PGAPSINBASE  = PGAPSINDATA.ptr();
    const auto PGAPSOUTBASE = PGAPSOUTDATA.ptr();
    if (!PGAPSINBASE || !PGAPSOUTBASE || PGAPSINBASE->getDataType() != Config::CVD_TYPE_CSS_VALUE || PGAPSOUTBASE->getDataType() != Config::CVD_TYPE_CSS_VALUE) return;

    auto* const PGAPSIN  = static_cast<Config::CCssGapData*>(PGAPSINBASE);
    auto* const PGAPSOUT = static_cast<Config::CCssGapData*>(PGAPSOUTBASE);

    if (active) {
        if (!config.onBottom->value())
            pMonitor->m_reservedArea = Desktop::CReservedArea(currentHeight, 0, 0, 0);
        else
            pMonitor->m_reservedArea = Desktop::CReservedArea(0, 0, currentHeight, 0);
    } else {
        pMonitor->m_reservedArea = Desktop::CReservedArea();
    }

    g_pHyprRenderer->arrangeLayersForMonitor(ownerID);

    // gaps are created via workspace rules
    // there are no way to write to m_dWorkspaceRules directly
    // and we want to refrain from using function hooks
    // so we create a workspace rule for ALL workspaces through handleWorkspaceRules
    // Geneva Convention violation type hack but idc atm
    if (active) {
        const auto oActiveWorkspace = pMonitor->m_activeWorkspace;
        if (!oActiveWorkspace) return;

        for (auto& ws : State::workspaceState()->workspaces()) { // HACK: recalculate other workspaces without reserved area
            if (ws && ws->m_monitor && ws->m_monitor->m_id == ownerID && ws->m_id != oActiveWorkspace->m_id) {
                pMonitor->m_activeWorkspace = ws.lock();
                const auto curRules = std::to_string(pMonitor->activeWorkspaceID()) + ", gapsin:" + PGAPSIN->toString() + ", gapsout:" + PGAPSOUT->toString();
                if (config.overrideGaps->value()) {
                    if (const auto legacy = Config::Legacy::mgr().lock())
                        legacy->handleWorkspaceRules("", curRules);
                }
                g_layoutManager->recalculateMonitor(pMonitor);
            }
        }
        pMonitor->m_activeWorkspace = oActiveWorkspace;

        const auto curRules = std::to_string(pMonitor->activeWorkspaceID()) + ", gapsin:" + std::to_string(config.gapsIn->value()) + ", gapsout:" + std::to_string(config.gapsOut->value());
        if (config.overrideGaps->value()) {
            if (const auto legacy = Config::Legacy::mgr().lock())
                legacy->handleWorkspaceRules("", curRules);
        }
        g_layoutManager->recalculateMonitor(pMonitor);

    }
    else {
        for (auto& ws : State::workspaceState()->workspaces()) {
            if (ws && ws->m_monitor && ws->m_monitor->m_id == ownerID) {
                const auto curRules = std::to_string(ws->m_id) + ", gapsin:" + PGAPSIN->toString() + ", gapsout:" + PGAPSOUT->toString();
                if (config.overrideGaps->value()) {
                    if (const auto legacy = Config::Legacy::mgr().lock())
                        legacy->handleWorkspaceRules("", curRules);
                }
            }
        }
        g_layoutManager->recalculateMonitor(pMonitor);
    }
}
