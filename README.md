# Hyprspace

Hyprland overview / workspace-expo plugin. Renders a top or bottom workspace strip with live thumbnails; switch or move windows between workspaces.

This fork targets Hyprland **0.56.0+** with **Lua-based configuration** (not the old `.conf` / Hyprlang plugin config flow).

## Features

- Live workspace overview with window thumbnails
- Multi-monitor support
- Mouse drag/drop between workspaces
- Optional gesture support
- Lua-native configuration via [Hyprspace.lua](./Hyprspace.lua)
- Manual build or `hyprpm` install

## Requirements

- Hyprland **0.56.0+** (Animation / State API refactor)
- Hyprland development headers / pkg-config metadata (manual builds)
- C++ compiler with C++23 support

## Setup

Pick **one** load method. Do not enable `hyprpm` and also `hyprctl plugin load` the same plugin twice.

### 1. Local build (recommended for this repo)

Clone or keep the repo next to your Hyprland config, then build:

```bash
git clone https://github.com/0xl30/Hyprspace.git ~/.config/hypr/Hyprspace
cd ~/.config/hypr/Hyprspace 
make all
```

Produces `Hyprspace.so` in that directory (same folder as `Hyprspace.lua`).

**`hyprland.lua`** (or your overlay entrypoint) — add the module search path:

```lua
package.path = package.path .. ";" .. HOME .. "/.config/hypr/Hyprspace/?.lua"
```

Use `?.lua`, not the full `Hyprspace.lua` filename. Lua replaces `?` with the module name for `require("Hyprspace")`.

**`plugins.lua`:**

```lua
local hyprspace = require("Hyprspace")
hyprspace.setup()
```

Optional explicit path (manual mode — only this path is tried):

```lua
hyprspace.setup({
    plugin_path = HOME .. "/.config/hypr/Hyprspace/Hyprspace.so",
})
```

**Keybinds** (e.g. in `keybinds.lua`):

```lua
local hyprspace = require("Hyprspace")

hl.unbind("SUPER + A")
hl.bind("SUPER + A", function()
    hyprspace.toggle()
end)
```

Reload:

```bash
hyprctl reload
```

### 2. `hyprpm`

```bash
hyprpm add https://github.com/0xl30/Hyprspace.git
hyprpm enable Hyprspace
hyprpm reload
```

Still add `package.path` (same as above) so `require("Hyprspace")` finds the Lua helper.

**`plugins.lua`:**

```lua
local hyprspace = require("Hyprspace")

hyprspace.setup()
```

`setup()` does **not** scan hyprpm’s install tree for a `.so` path. It:

1. Uses `plugin_path` if you passed one (manual mode)
2. Else `HYPRSPACE_PLUGIN_PATH` if set
3. Else `Hyprspace.so` next to `Hyprspace.lua`
4. Else runs `hyprpm reload` and checks whether `hl.plugin.Hyprspace` is available

After Hyprland or plugin updates:

```bash
hyprpm update
hyprpm reload
hyprctl reload
```

### Environment variable

Override the `.so` location without editing Lua:

```bash
export HYPRSPACE_PLUGIN_PATH="$HOME/.config/hypr/Hyprspace/Hyprspace.so"
```

## Usage

### Lua API

```lua
local hyprspace = require("Hyprspace")

hyprspace.setup()
hyprspace.apply_config()
hyprspace.toggle()
hyprspace.overview("open")
hyprspace.overview("close")
hyprspace.reload()
```

### Plugin API (after load)

```lua
hl.plugin.Hyprspace.overview("toggle")
hl.plugin.Hyprspace.overview("open")
hl.plugin.Hyprspace.overview("close")
```

## Configuration

Defaults and Matugen colors live in [Hyprspace.lua](./Hyprspace.lua). The helper reads:

```text
~/.config/matugen/generated/hyprland-colors.lua
```

If missing, built-in fallback colors are used. Matugen `rgba(rrggbbaa)` values are converted to the integer format the plugin expects.

Applied via `hl.config({ plugin = { hyprspace = { ... } } })`. Example:

```lua
plugin = {
    hyprspace = {
        panel_height = 220,
        panel_border_width = 2,
        workspace_margin = 10,
        reserved_area = 35,
        workspace_border_size = 1,

        center_aligned = true,
        on_bottom = false,
        draw_active_workspace = true,
        hide_real_layers = false,
        affect_strut = false,

        auto_drag = true,
        auto_scroll = true,
        exit_on_click = true,
        exit_on_switch = false,

        disable_gestures = false,
        swipe_fingers = 3,
        swipe_distance = 300,
        swipe_force_speed = 30,
        swipe_cancel_ratio = 0.5,
        click_release_threshold_ms = 200,
    },
}
```

### Main options

#### Colors

- `panel_color`, `panel_border_color`
- `workspace_active_background`, `workspace_inactive_background`
- `workspace_active_border`, `workspace_inactive_border`
- `drag_alpha`, `disable_blur`

#### Layout

- `panel_height`, `panel_border_width`, `workspace_margin`, `reserved_area`, `workspace_border_size`
- `adaptive_height`, `center_aligned`, `on_bottom`
- `hide_background_layers`, `hide_top_layers`, `hide_overlay_layers`
- `draw_active_workspace`, `hide_real_layers`, `affect_strut`

#### Behavior

- `auto_drag`, `auto_scroll`, `exit_on_click`, `switch_on_drop`, `exit_on_switch`
- `show_new_workspace`, `show_empty_workspace`, `show_special_workspace`, `exit_key`

#### Gestures and input

- `disable_gestures`, `reverse_swipe`, `swipe_fingers`, `swipe_distance`, `swipe_force_speed`
- `swipe_cancel_ratio`, `swipe_threshold`, `swipe_closed_padding`, `workspace_scroll_speed`
- `click_release_threshold_ms`

#### Animation

- `override_anim_speed`

## Troubleshooting

### Keybind does nothing

```bash
hyprctl plugin list
```

Expect `Plugin Hyprspace`. If empty:

- Local: `make all` in the Hyprspace repo, then `hyprctl reload`
- hyprpm: `hyprpm reload`, then `hyprctl reload`
- Ensure `plugins.lua` calls `hyprspace.setup()` (not `hyprspace.setup(auto)` — `auto` is not a valid argument)

### Lua colors / settings not applied

`plugins.lua` must call `hyprspace.setup()` or `hyprspace.setup({ plugin_path = "..." })` so hooks run and `hl.config(...)` is applied after the plugin loads.

### `require("Hyprspace")` fails

Add to your Hyprland Lua entrypoint:

```lua
package.path = package.path .. ";" .. HOME .. "/.config/hypr/Hyprspace/?.lua"
```

Do **not** append `Hyprspace/Hyprspace.lua` directly — use `?.lua`.

### `hyprpm enable` fails

- Correct repo URL
- `hyprpm.toml` has a commit pin for your Hyprland version (for 0.56.0, pin after the State/Animation API port is committed)
- Plugin builds cleanly on that commit (`hyprpm update` / rebuild)

### Build fails: missing `AnimationManager.hpp`

On Hyprland 0.56+, animation headers moved from `managers/animation/` to `animation/`, and compositor helpers moved into `State::` / `Desktop::` / `Fullscreen::` trackers. Rebuild this fork against matching headers (`pkg-config --modversion hyprland`).

### Plugin crashes on load

Check `~/.cache/hyprland/` crash reports. Rebuild the plugin against the same Hyprland version you are running (`hyprctl version`).
