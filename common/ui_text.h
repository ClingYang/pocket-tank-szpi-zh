#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t codepoint;
    uint8_t small[8];
    uint16_t large[12];
} ui_zh_glyph_t;

uint32_t ui_utf8_next(const char **cursor);
int ui_text_advance(uint32_t codepoint, int scale);
int ui_text_width(const char *text, int scale);
const ui_zh_glyph_t *ui_zh_find(uint32_t codepoint);
const char *ui_zh_label(const char *label);
size_t ui_text_fit_bytes(const char *text, int width, int scale, size_t max_bytes);
void ui_text_copy(char *destination, size_t capacity, const char *source);
void ui_zh_tank_name(char *out, size_t capacity, const char *name);
