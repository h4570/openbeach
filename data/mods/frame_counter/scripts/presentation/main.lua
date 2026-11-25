local margin = 8
local padding = 4
local font = "guifont8b"
local enabled
local corner

function on_start()
  enabled = obl.settings.get("enabled")
  corner = obl.settings.get("corner")
end

function on_setting(id, value)
  if id == "enabled" then
    enabled = value
  elseif id == "corner" then
    corner = value
  end
end

function on_frame()
  if not enabled or not obl.game.in_world() then
    return
  end

  local text = string.format("FPS %.0f   %.1f ms", obl.stats.fps(), obl.stats.frame_ms())
  local text_width, text_height = obl.overlay.measure(text, font)
  local width = text_width + 2 * padding
  local height = text_height + 2 * padding
  local screen_width, screen_height = obl.screen.size()
  local safe = obl.screen.safe_area()
  local x = safe.left + margin
  local y = safe.top + margin

  if corner == "top_right" or corner == "bottom_right" then
    x = screen_width - safe.right - margin - width
  end
  if corner == "bottom_left" or corner == "bottom_right" then
    y = screen_height - safe.bottom - margin - height
  end

  obl.overlay.rect(x, y, width, height, 0xFF000000)
  obl.overlay.text(x + padding, y + padding, text, font, 0xFFFFFFFF)
end
