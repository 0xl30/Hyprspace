#include "Overview.hpp"
#include "Globals.hpp"

#include <algorithm>

void CHyprspaceWidget::updateLayout() {
    const auto monitor = getOwner();
    if (!monitor)
        return;

    const auto reservedHeight = std::max(0, Config::panelHeight + Config::reservedArea);
    if (active && Config::affectStrut) {
        monitor->m_reservedArea = Config::onBottom ? Desktop::CReservedArea(0, 0, reservedHeight, 0) : Desktop::CReservedArea(reservedHeight, 0, 0, 0);
    } else {
        monitor->m_reservedArea = Desktop::CReservedArea();
    }

    g_pHyprRenderer->arrangeLayersForMonitor(ownerID);
    g_layoutManager->recalculateMonitor(monitor);
    monitor->scheduleFrame();
}
