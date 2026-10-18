# Audit raw findings: UI framework, gfx, MainWindow, Shell, Signal, I18n (reviewer A3)

1. **must** — Alt+Left with the Sort menu open destroys the page. Clicking a sort item afterwards calls SetSort on the freed page. The Browser Back key and a language change do the same.
   - Where: Shell.cpp:551, :470; Controls.cpp:1315-1320; Views.cpp:717, :826.
   - Fix: CloseAllPopups() before ClearChildren() in ShowRoute and in the language rebuild. (checked)
2. **should** — The TextBox caret blink repaints the whole window every 530 ms while the window is in the background, which breaks the idle=0 rule.
   - Where: Controls.cpp:665-668, :879; Host.cpp:1524-1529.
   - Fix: stop the blink when inactive and restart it on WM_ACTIVATE. (checked)
3. **should** — After device loss, only the window that saw it repaints. RecreateDevice runs on the UI thread and can fall back to WARP for the whole session.
   - Where: Gpu.cpp:353-360, :379-382; Host.cpp:723-728.
   - Fix: RequestLayout on every Host after a successful recreate. (checked)
4. **should** — Settings and onboarding are sized with the system DPI. On a monitor with a different DPI the window is too small or too big, because no WM_DPICHANGED arrives at creation.
   - Where: Host.cpp:304, :317-323, :341-347.
   - Fix: MulDiv w/h by actual/old DPI. (checked; the "no message at creation" part is likely)
5. **should** — After a wheel scroll, the hover and tooltip belong to the old item, because there is no re-hit-test.
   - Where: Host.cpp:1430-1441, :697-698; ItemGrid.cpp:902-915.
   - Fix: UpdateHot after layout when the mouse is inside. (checked)
6. **should** — The "System" theme doesn't follow Windows at runtime; nothing handles WM_SETTINGCHANGE "ImmersiveColorSet".
   - Where: App.cpp:166-181, MainWindow.cpp.
   - Fix: handle it and call ApplyTheme. (checked)
7. **nice** — The title-bar tooltips, the drop hint and the collapsed-rail tooltips keep the old language after a switch.
   - Where: Shell.cpp:136-186, :294; MainWindow.cpp:168.
   - Fix: connect to languageChanged. (checked)
8. **nice** — A blank click in a ScrollView flashes the bar and stops a smooth scroll.
   - Where: Controls.cpp:1049-1053.
   - Fix: OnMouseUp returns early unless dragging_. (checked)
9. **nice** — A passthrough popup (toast or tooltip) that appears during a press eats the click.
   - Where: Host.cpp:485-488.
   - Fix: skip this for passthrough popups. (checked)
10. **nice** — Any animation re-lays out every drawing window each frame.
    - Where: Host.cpp:697-698, :733; Anim.cpp:120-129.
    - Fix: lay out only when dirty; ScrollView and the chrome invalidate themselves. Measure first. (likely)
11. **nice** — A scroll view can stay 1-4 DIP off the top: when it's not Scrollable, the offset is clamped to MaxOffset, not to 0.
    - Where: Controls.cpp:943, :948. (checked)
12. **nice (lean)** — Dead code, about 12 lines:
    - ShellRoot::OnMouseMove;
    - ShellBody::window_;
    - Host::onActivate;
    - Host::RenderNow;
    - Text::Size made private.

Left out (7):
- Slider::OnMouseUp ignores cancelled (Controls.cpp:540).
- Dropdown/Segmented with no choices hit clamp(0,-1).
- The toast map keeps stale Host* entries.
- An orphan tooltip bubble stays up.
- kDumpTreeMessage is honoured outside --automation (Host.cpp:1598).
- TextBox paste has no length cap.
- Text::Layout allocates its key on every call.
