/*
    SPDX-FileCopyrightText: 2026 Benji Weber
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "windowscancover.h"

#include <rules.h>
#include <window.h>
#include <workspace.h>

#include <KConfigGroup>
#include <KSharedConfig>

using namespace KWin;

// Id of the window rule we keep in kwinrulesrc. KWin never applies window rules to
// layer-shell surfaces by itself, so the rule only takes effect where we opt a panel in.
static const QString s_ruleId = QStringLiteral("windows-can-cover-panel");

namespace
{
// Window::belongsToLayer() is protected and not exported; reach it through a member
// pointer (virtual dispatch, no symbol needed).
struct LayerPeek : Window {
    static Layer requestedLayer(const Window *window)
    {
        return (window->*(&LayerPeek::belongsToLayer))();
    }
};

bool isPlasmaPanel(const Window *window)
{
    return window->inherits("KWin::LayerShellV1Window") && window->windowType() == WindowType::Dock
        && window->resourceClass() == QLatin1String("plasmashell");
}
}

WindowsCanCover::WindowsCanCover()
{
    ensureRule();

    connect(workspace(), &Workspace::windowAdded, this, &WindowsCanCover::track);
    connect(workspace(), &Workspace::windowRemoved, this, [this](Window *window) {
        m_panels.remove(window);
    });
    // A layer change request from a layer-shell client schedules a rearrange.
    connect(workspace(), &Workspace::aboutToRearrange, this, &WindowsCanCover::evaluateAll);
    // Restack whenever focus moves: only the active window may cover the panel.
    connect(workspace(), &Workspace::windowActivated, this, [this](Window *) {
        evaluateAll();
    });

    const auto windows = workspace()->windows();
    for (Window *window : windows) {
        track(window);
    }
}

WindowsCanCover::~WindowsCanCover()
{
    for (auto it = m_panels.begin(); it != m_panels.end(); ++it) {
        if (it.value() && !it.key()->isDeleted()) {
            it.key()->finishWindowRules();
            it.key()->updateLayer();
        }
    }
}

void WindowsCanCover::ensureRule()
{
    // Only ever touch our own group. KWin discovers rule groups that are missing from
    // [General] Order by itself; writing the legacy "rules"/"count" keys would make its
    // migration code delete every other rule.
    auto config = KSharedConfig::openConfig(QStringLiteral("kwinrulesrc"), KConfig::NoGlobals);
    KConfigGroup rule(config, s_ruleId);

    if (rule.readEntry("layer") == QLatin1String("normal") && rule.readEntry("layerrule", 0) == int(Rules::Force)
        && rule.readEntry("wmclass") == QLatin1String("plasmashell")) {
        return;
    }

    rule.writeEntry("Description", QStringLiteral("Plasma panels in \"Windows can cover\" mode (plasma-windows-can-cover)"));
    rule.writeEntry("wmclass", QStringLiteral("plasmashell"));
    rule.writeEntry("wmclassmatch", int(Rules::ExactMatch));
    rule.writeEntry("wmclasscomplete", false);
    rule.writeEntry("types", int(DockMask));
    rule.writeEntry("layer", QStringLiteral("normal"));
    rule.writeEntry("layerrule", int(Rules::Force));
    config->sync();

    workspace()->rulebook()->load();
}

void WindowsCanCover::track(Window *window)
{
    if (!isPlasmaPanel(window) || m_panels.contains(window)) {
        return;
    }
    m_panels[window] = false;
    evaluate(window);
}

void WindowsCanCover::evaluateAll()
{
    for (auto it = m_panels.begin(); it != m_panels.end(); ++it) {
        if (!it.key()->isDeleted()) {
            evaluate(it.key());
        }
    }
}

void WindowsCanCover::restackAtRest(Window *panel)
{
    Window *active = workspace()->activeWindow();
    if (active && active != panel && !active->isDeleted() && active->layer() == NormalLayer
        && active->frameGeometry().intersects(panel->frameGeometry())) {
        // The focused window overlaps the panel: keep the panel directly beneath
        // it, above every other window.
        workspace()->stackBelow(panel, active);
    } else {
        // Nothing focused over the panel: stay above all normal windows.
        workspace()->raiseWindow(panel);
    }
}

void WindowsCanCover::evaluate(Window *window)
{
    const Layer requested = LayerPeek::requestedLayer(window);
    bool &managed = m_panels[window];

    if (requested == BelowLayer && !managed) {
        // Rest: live in the normal stack, avoiding only the active window.
        managed = true;
        window->setupWindowRules();
        window->updateLayer();
        restackAtRest(window);
    } else if (requested != BelowLayer && managed) {
        // Raised (or the mode was switched off): on top of everything again.
        managed = false;
        window->finishWindowRules();
        window->updateLayer();
    } else if (requested == BelowLayer && managed) {
        // Already at rest: focus may have moved, restack against the new active window.
        restackAtRest(window);
    }
}
