/* Bounded OLED text, using the built-in GFX font plus small Cyrillic glyphs.
 * UTF-8 is decoded only for drawing; stored names keep their original bytes.
 * No heap allocation, scrolling timer or additional framebuffer.
 */
#pragma once
#include "display_glyphs.h"

static uint16_t ICACHE_FLASH_ATTR nextDisplayGlyph(const char*& text) {
  uint8_t first = uint8_t(*text++);
  if (first < 0x80) return first >= 32 ? first : '?';
  uint8_t count = first >= 0xC2 && first <= 0xDF ? 1 :
                  first >= 0xE0 && first <= 0xEF ? 2 :
                  first >= 0xF0 && first <= 0xF4 ? 3 : 0;
  if (!count) return '?';
  uint32_t code = first & (0x7F >> count);
  for (uint8_t i = 0; i < count; ++i) {
    uint8_t next = uint8_t(*text);
    if ((next & 0xC0) != 0x80) return '?';
    code = (code << 6) | (next & 0x3F);
    ++text;
  }
  if (code < (count == 1 ? 0x80U : count == 2 ? 0x800U : 0x10000U) ||
      code > 0xFFFF || (code >= 0xD800 && code <= 0xDFFF)) return '?';
  return uint16_t(code);
}

static void ICACHE_FLASH_ATTR drawDisplayGlyph(uint16_t code, int x, int y, uint8_t size) {
  int custom = -1;
  if (code >= 0x410 && code <= 0x44F) custom = code - 0x410;
  else if (code == 0x401) custom = 64;
  else if (code == 0x451) custom = 65;
  else if (code == 0xC3) custom = 66;
  else if (code == 0xE3) custom = 67;
  else if (code == 0xD5) custom = 68;
  else if (code == 0xF5) custom = 69;
  if (custom >= 0) {
    for (uint8_t col = 0; col < 5; ++col) {
      uint8_t bits = pgm_read_byte(&displayExtraGlyphs[custom][col]);
      for (uint8_t row = 0; row < 8; ++row) {
        if (bits & (1U << row)) display.fillRect(x + col * size, y + row * size, size, size, SSD1306_WHITE);
      }
    }
    return;
  }
  if (code >= 0xC0 && code <= 0xFF) code = pgm_read_byte(&displayLatin1[code - 0xC0]);
  else if (code == 0xB0) code = 0xF8; // Correct CP437 degree sign, not the legacy off-by-one glyph.
  else if (code == 0xA0) code = ' ';
  else if (code >= 128) code = '?';
  display.setCursor(x, y);
  display.write(uint8_t(code));
}

// Center a UTF-8 block inside an explicit rectangle. Prefer a single line;
// then wrap on spaces, with hard breaks only for words wider than the screen.
// Ellipsis marks an exhausted rectangle instead of silently losing the ends.
static void ICACHE_FLASH_ATTR displayText(const char* text, int top, int height, uint8_t maxSize = 1) {
  uint16_t glyphs[64];
  uint8_t count = 0;
  while (*text && count < 63) glyphs[count++] = nextDisplayGlyph(text);
  if (!count) return;
  if (*text) { glyphs[60] = glyphs[61] = glyphs[62] = '.'; }
  uint8_t size = maxSize;
  while (size > 1 && (count * 6 * size > display.width() || 8 * size > height)) --size;
  const uint8_t columns = display.width() / (6 * size);
  const uint8_t rows = height / (8 * size);
  if (!columns || !rows) return;
  uint8_t starts[16], lengths[16], lines = 0, pos = 0;
  while (pos < count && lines < rows && lines < 16) {
    uint8_t end = pos + columns < count ? pos + columns : count;
    if (end < count && lines + 1 < rows && glyphs[end] != ' ') {
      for (uint8_t i = end; i > pos; --i) {
        // Do not waste the first line on a short word when the remaining
        // word would otherwise force an unnecessary ellipsis on the last line.
        if (glyphs[i - 1] == ' ' && i - 1 > pos &&
            count - i <= (rows - lines - 1) * columns) { end = i - 1; break; }
      }
    }
    starts[lines] = pos;
    lengths[lines++] = end - pos;
    pos = end;
    while (pos < count && glyphs[pos] == ' ') ++pos;
  }
  if (pos < count && lengths[lines - 1] >= 3) {
    uint8_t end = starts[lines - 1] + lengths[lines - 1];
    glyphs[end - 1] = glyphs[end - 2] = glyphs[end - 3] = '.';
  }
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.cp437(true);
  int y = top + (height - lines * 8 * size) / 2;
  for (uint8_t line = 0; line < lines; ++line, y += 8 * size) {
    int x = (display.width() - lengths[line] * 6 * size + size) / 2;
    for (uint8_t i = 0; i < lengths[line]; ++i, x += 6 * size)
      drawDisplayGlyph(glyphs[starts[line] + i], x, y, size);
  }
}

static int ICACHE_FLASH_ATTR footerTop() { return display.height() - (display.width() < 100 ? 32 : 16); }
static void ICACHE_FLASH_ATTR displayFooter(const char* text, uint8_t size = 1) {
  displayText(text, footerTop(), display.height() - footerTop(), size);
}
