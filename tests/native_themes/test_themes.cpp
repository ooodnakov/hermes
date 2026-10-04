#include "ui/themes.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

struct Rgb {
  double r;
  double g;
  double b;
};

Rgb unpack(std::uint16_t color) {
  const unsigned r5 = (color >> 11) & 0x1F;
  const unsigned g6 = (color >> 5) & 0x3F;
  const unsigned b5 = color & 0x1F;
  return {r5 * 255.0 / 31.0, g6 * 255.0 / 63.0, b5 * 255.0 / 31.0};
}

double linearize(double channel) {
  const double s = channel / 255.0;
  return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

double luminance(std::uint16_t color) {
  const Rgb rgb = unpack(color);
  return 0.2126 * linearize(rgb.r) + 0.7152 * linearize(rgb.g) +
         0.0722 * linearize(rgb.b);
}

double contrast(std::uint16_t first, std::uint16_t second) {
  const double a = luminance(first);
  const double b = luminance(second);
  const double lighter = a > b ? a : b;
  const double darker = a > b ? b : a;
  return (lighter + 0.05) / (darker + 0.05);
}

void assert_warning_yellowish(std::uint16_t color) {
  const Rgb rgb = unpack(color);
  assert(rgb.r > 100.0);
  assert(rgb.g > rgb.r * 0.5);
  assert(rgb.b < rgb.r / 3.0);
}

void assert_danger_reddish(std::uint16_t color) {
  const Rgb rgb = unpack(color);
  assert(rgb.r > rgb.g * 1.5);
  assert(rgb.r > rgb.b * 1.5);
}

}  // namespace

int main() {
  using ui::themes::Id;
  using ui::themes::Palette;

  static_assert(ui::themes::count() == static_cast<std::size_t>(Id::Count));
  const Palette& fallback = ui::themes::get(static_cast<std::uint8_t>(Id::Phosphor));
  assert(std::strcmp(fallback.name, "Phosphor") == 0);
  assert(ui::themes::get(static_cast<std::uint8_t>(Id::Count)).name == fallback.name);
  assert(ui::themes::get(255).name == fallback.name);
  assert(ui::themes::get(Id::Count).name == fallback.name);

  constexpr std::uint16_t avatarSource[] = {0x0000, 0x00C0, 0x03E0, 0x57EA};
  for (std::uint16_t source : avatarSource)
    assert(ui::themes::avatarColor(source, static_cast<std::uint8_t>(Id::Phosphor)) == source);
  assert(ui::themes::avatarColor(0x1234, static_cast<std::uint8_t>(Id::Ocean)) == 0x1234);
  // Invalid theme IDs resolve to Phosphor and preserve the asset colors.
  for (std::uint16_t source : avatarSource)
    assert(ui::themes::avatarColor(source, static_cast<std::uint8_t>(Id::Count)) == source);
  assert(ui::themes::avatarColor(0x57EA, 255) == 0x57EA);

  const char* expectedNames[] = {"Phosphor", "Amber", "Ocean", "Paper", "Gruvbox"};
  const Palette& gruvbox = ui::themes::get(Id::Gruvbox);
  assert(gruvbox.background == 0x2945 && gruvbox.panel == 0x39C6);
  assert(gruvbox.secondary == 0xBD72 && gruvbox.accent == 0xBDC4);
  assert(gruvbox.text == 0xEED6 && gruvbox.warning == 0xFDE5);
  assert(gruvbox.danger == 0xFC0D && gruvbox.code == 0x8E0F);
  for (std::size_t i = 0; i < ui::themes::count(); ++i) {
    const Palette& palette = ui::themes::get(static_cast<std::uint8_t>(i));
    assert(std::strcmp(palette.name, expectedNames[i]) == 0);

    if (i != static_cast<std::size_t>(Id::Phosphor)) {
      assert(ui::themes::avatarColor(avatarSource[0], static_cast<std::uint8_t>(i)) == palette.background);
      assert(ui::themes::avatarColor(avatarSource[1], static_cast<std::uint8_t>(i)) == palette.panel);
      assert(ui::themes::avatarColor(avatarSource[2], static_cast<std::uint8_t>(i)) == palette.accent);
      assert(ui::themes::avatarColor(avatarSource[3], static_cast<std::uint8_t>(i)) == palette.text);
    }

    // WCAG's 4.5:1 nominal contrast criterion is used for normal text, dim
    // text, and code. This checks the RGB565 colors themselves; it cannot
    // account for display calibration, ambient light, or viewing angle.
    assert(contrast(palette.text, palette.background) >= 4.5);
    assert(contrast(palette.secondary, palette.background) >= 4.5);
    assert(contrast(palette.code, palette.background) >= 4.5);
    assert(contrast(palette.code, palette.panel) >= 4.5);
    assert(contrast(palette.accent, palette.background) >= 4.5);
    assert(contrast(palette.accent, palette.panel) >= 4.5);
    assert(contrast(palette.warning, palette.background) >= 4.5);
    assert(contrast(palette.warning, palette.panel) >= 4.5);
    assert(contrast(palette.danger, palette.background) >= 4.5);
    assert(contrast(palette.danger, palette.panel) >= 4.5);

    // Status colors retain recognizable yellow and red hues in all themes.
    assert_warning_yellowish(palette.warning);
    assert_danger_reddish(palette.danger);
  }

  return 0;
}
