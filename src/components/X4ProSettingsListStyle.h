#pragma once

#include <BoardConfig.h>
#include <FreeInkUI.h>

namespace X4ProSettingsListStyle {

constexpr int VISIBLE_ROWS = 10;
constexpr int ICON_SIZE = 24;
constexpr int VALUE_INSET = 8;
constexpr int TOGGLE_WIDTH = 32;
constexpr int TOGGLE_HEIGHT = 18;

template <typename ScreenT>
inline void apply(ScreenT& screen, freeink::ui::ListProps& props) {
  props.valueInset = VALUE_INSET;
  props.iconSize = ICON_SIZE;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  if (!BoardConfig::isX4Pro()) return;

  props.rowHeight = static_cast<int16_t>(screen.body().height / VISIBLE_ROWS);
  props.rowGap = 0;
  props.toggleWidth = TOGGLE_WIDTH;
  props.toggleHeight = TOGGLE_HEIGHT;
  props.toggleRadius = TOGGLE_HEIGHT / 2;
  props.toggleKnobRadius = (TOGGLE_HEIGHT - 6) / 2;
}

inline void setToggle(freeink::ui::ListItem& item, const bool checked) {
  item.value = nullptr;
  item.toggle = true;
  item.toggleChecked = checked;
}

inline void clearToggle(freeink::ui::ListItem& item) {
  item.toggle = false;
  item.toggleChecked = false;
}

}  // namespace X4ProSettingsListStyle
