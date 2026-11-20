/*
    SPDX-FileCopyrightText: 2026 Benji Weber
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <plugin.h>

#include <QHash>

namespace KWin
{
class Window;
}

/**
 * "Windows can cover" panels, active-window variant: the panel lives in the normal
 * window layer and only ever avoids the currently focused window. When the active
 * window overlaps the panel, the panel sits directly beneath it; otherwise it stays
 * above all other windows.
 *
 * Protocol with the plasmashell side (org.windowscancover PanelCover), using only the
 * layer-shell layer the panel requests:
 *  - "bottom": at rest, stack relative to the active window (see above).
 *  - "top": raised (screen edge, hover, popup...). Above everything.
 * Without this plugin the plasmashell side still works, with plain top/bottom layers.
 */
class WindowsCanCover : public KWin::Plugin
{
    Q_OBJECT

public:
    WindowsCanCover();
    ~WindowsCanCover() override;

private:
    void ensureRule();
    void track(KWin::Window *window);
    void evaluateAll();
    void evaluate(KWin::Window *window);
    void restackAtRest(KWin::Window *panel);

    QHash<KWin::Window *, bool> m_panels; // window -> window rules applied (managed)
};
