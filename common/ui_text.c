#include "ui_text.h"
#include <string.h>
#include <stdio.h>

uint32_t ui_utf8_next(const char **cursor) {
    const unsigned char *p = (const unsigned char *)*cursor;
    uint32_t value = *p;
    if (value < 0x80) { if (value) *cursor += 1; return value; }
    int bytes = value >= 0xf0 && value <= 0xf4 ? 4 : value >= 0xe0 && value <= 0xef ? 3 : value >= 0xc2 && value <= 0xdf ? 2 : 0;
    if (!bytes) { *cursor += 1; return '?'; }
    value &= (1u << (7 - bytes)) - 1;
    for (int i = 1; i < bytes; i++) {
        if (!p[i] || (p[i] & 0xc0) != 0x80) { *cursor += 1; return '?'; }
        value = (value << 6) | (p[i] & 0x3f);
    }
    if ((bytes == 2 && value < 0x80) || (bytes == 3 && value < 0x800) ||
        (bytes == 4 && value < 0x10000) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        *cursor += 1;
        return '?';
    }
    *cursor += bytes;
    return value;
}

int ui_text_advance(uint32_t codepoint, int scale) {
    return codepoint < 0x80 ? 6 * scale : (scale == 1 ? 12 : 9 * scale) + scale;
}

int ui_text_width(const char *text, int scale) {
    int width = 0;
    while (*text) width += ui_text_advance(ui_utf8_next(&text), scale);
    return width ? width - scale : 0;
}

size_t ui_text_fit_bytes(const char *text, int width, int scale, size_t max_bytes) {
    const char *cursor = text;
    size_t bytes = 0;
    int used = 0;
    while (*cursor) {
        uint32_t codepoint = ui_utf8_next(&cursor);
        int advance = ui_text_advance(codepoint, scale);
        if (used + advance - scale > width || (size_t)(cursor - text) > max_bytes) break;
        used += advance;
        bytes = (size_t)(cursor - text);
    }
    return bytes;
}

void ui_text_copy(char *destination, size_t capacity, const char *source) {
    if (!capacity) return;
    size_t length = ui_text_fit_bytes(source, 0x3fffffff, 1, capacity - 1);
    memcpy(destination, source, length);
    destination[length] = 0;
}

const char *ui_zh_label(const char *label) {
    static const struct { const char *english; const char *chinese; } labels[] = {
        {"alpha", "测试版"},
        {"A LEAF", "叶片"}, {"A BLADE", "草叶"}, {"THE CASTLE", "城堡"},
        {"THE CORAL", "珊瑚"}, {"THE VOLCANO", "火山"}, {"THE REEF", "礁石"}, {"THE SAND", "沙地"},
        {"fry", "鱼苗"}, {"juv", "幼鱼"}, {"adult", "成鱼"}, {"elder", "老鱼"},
        {"CLASSIC", "经典"}, {"SLENDER", "纤长"}, {"BULBOUS", "圆润"},
        {"VEIL", "飘逸长鳍"}, {"SAIL", "高帆鳍"}, {"BARS", "条纹"},
        {"MASK", "面罩"}, {"EYE BAND", "眼带"}, {"SOLID", "纯色"},
        {"SHAPE", "体型"}, {"FINS", "鱼鳍"}, {"PATTERN", "图案"},
        {"BODY", "身体"}, {"MARKINGS", "花纹"},
        {"seek_food", "寻找食物"}, {"flee_shadow", "躲避阴影"},
        {"visit_bubbles", "靠近气泡"}, {"follow_friend", "跟随伙伴"},
        {"explore", "探索"}, {"rest", "休息"}, {"dart_play", "嬉戏"}, {"inspect_reef", "探索礁石"},
        {"REEF", "珊瑚礁"}, {"LAGOON", "潟湖"}, {"DUSK", "暮色"},
        {"arrived", "初来乍到"}, {"first meal from you", "第一次亲手喂食"},
        {"first hold-approach", "第一次靠近手指"}, {"first dart", "第一次嬉戏"},
        {"first bubbles", "第一次探索气泡"}, {"first follow", "第一次跟随伙伴"},
        {"reached juv", "长成幼鱼"}, {"reached adult", "长成成鱼"}, {"reached elder", "步入老年"},
        {"first rest in the seagrass", "第一次在水草间休息"},
        {"a pair", "两鱼相伴"}, {"a trio", "三鱼同游"}, {"a quartet", "四鱼同行"}, {"a quintet", "五鱼成群"},
        {"first full night's sleep", "第一次整夜安眠"}, {"first play session", "第一次陪伴嬉戏"},
        {"the tank changed someone", "性格因鱼缸而改变"}, {"first feeding", "第一次投食"},
        {"first trimming", "第一次修剪水草"}, {"first glass cleaning", "第一次擦净玻璃"},
        {"a school of ten shrimp", "十只虾的大家庭"}, {"first visit to the reef", "第一次探索礁石"},
        {"first mutation", "第一次出现新特征"}, {"first fish sold", "第一次出售鱼儿"},
        {"first seahorse feeding", "第一次喂海马"}, {"first sand dollar dug up", "第一次挖到沙币"},
    };
    for (size_t i = 0; i < sizeof labels / sizeof labels[0]; i++) {
        if (!strcmp(label, labels[i].english)) return labels[i].chinese;
    }
    return label;
}

void ui_zh_tank_name(char *out, size_t capacity, const char *name) {
    if (!name) name = "";
    if (!strcmp(name, "MY TANK")) { ui_text_copy(out, capacity, "我的鱼缸"); return; }
    unsigned number; char suffix = 0, extra = 0;
    int fields = sscanf(name, "TANK %u%c%c", &number, &suffix, &extra);
    if (!strncmp(name, "TANK ", 5) && (fields == 1 || (fields == 2 && suffix >= 'A' && suffix <= 'Z'))) {
        snprintf(out, capacity, "鱼缸%u%s", number, fields == 2 ? (char[2]){suffix, 0} : "");
        return;
    }
    ui_text_copy(out, capacity, name);
}
