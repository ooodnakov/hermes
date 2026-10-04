#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::themes {

enum class Id : std::uint8_t { Phosphor, Amber, Ocean, Paper, Gruvbox, Count };

struct Palette {
  std::uint16_t background;
  std::uint16_t panel;
  std::uint16_t secondary;
  std::uint16_t accent;
  std::uint16_t text;
  std::uint16_t warning;
  std::uint16_t danger;
  std::uint16_t code;
  const char* name;
};

// RGB565 values are kept here so native tests and the firmware use one source
// of truth. Phosphor preserves the existing dark green/cyan visual language.
inline constexpr Palette kPalettes[] = {
    {0x0000, 0x0841, 0x7BEF, 0xAFE5, 0xDFFF, 0xFEE0, 0xF965, 0x07F5,
     "Phosphor"},
    {0x1000, 0x2000, 0xBDF7, 0xF6A0, 0xFFDE, 0xFFE0, 0xF986, 0x05D7,
     "Amber"},
    {0x0010, 0x0821, 0x9D7F, 0x47FF, 0xEFFF, 0xFFE0, 0xFA8A, 0x7DFF,
     "Ocean"},
    {0xFFFF, 0xEF5D, 0x39E7, 0x0317, 0x1082, 0x7A00, 0xA104, 0x2017,
     "Paper"},
    // Gruvbox dark medium colors from morhetz/gruvbox. code uses bright aqua
    // (#8ec07c) for contrast; danger brightens bright red (#fb4934) to #ff806f
    // so the RGB565 value meets the existing contrast threshold.
    {0x2945, 0x39C6, 0xBD72, 0xBDC4, 0xEED6, 0xFDE5, 0xFC0D, 0x8E0F,
     "Gruvbox"},
};

inline constexpr std::size_t count() { return sizeof(kPalettes) / sizeof(kPalettes[0]); }

inline constexpr const Palette& get(std::uint8_t id) {
  return kPalettes[id < count() ? id : static_cast<std::uint8_t>(Id::Phosphor)];
}

inline constexpr const Palette& get(Id id) {
  return get(static_cast<std::uint8_t>(id));
}

// The bundled character artwork is RGB565 with four fixed source colors.
// Keep Phosphor byte-for-byte compatible with those assets; other themes use
// the same semantic roles with colors from their active palette.
inline constexpr std::uint16_t avatarColor(std::uint16_t sourceColor,
                                            std::uint8_t id) {
  if (id == static_cast<std::uint8_t>(Id::Phosphor) || id >= count())
    return sourceColor;
  const Palette& palette = get(id);
  switch (sourceColor) {
    case 0x0000: return palette.background;
    case 0x00C0: return palette.panel;
    case 0x03E0: return palette.accent;
    case 0x57EA: return palette.text;
    default: return sourceColor;
  }
}

}  // namespace ui::themes
