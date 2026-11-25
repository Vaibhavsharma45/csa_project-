/*
 * gui.c - everything the user sees and touches (raylib immediate-mode UI).
 * No cache arithmetic happens here: the GUI forwards user actions to the
 * simulator and draws the structured results it returns.
 */
#include "gui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cache.h"
#include "mapping.h"
#include "replacement.h"

/* ----------------------------------------------------------------- palette */
static const Color COL_BG      = { 0x12, 0x16, 0x20, 0xFF };
static const Color COL_PANEL   = { 0x1E, 0x26, 0x35, 0xFF };
static const Color COL_ROW_ALT = { 0x23, 0x2D, 0x40, 0xFF };
static const Color COL_FIELD   = { 0x15, 0x1B, 0x28, 0xFF };
static const Color COL_BORDER  = { 0x33, 0x40, 0x58, 0xFF };
static const Color COL_BUTTON  = { 0x2F, 0x3C, 0x56, 0xFF };
static const Color COL_PRIMARY = { 0x4F, 0x8C, 0xFF, 0xFF };
static const Color COL_HIT     = { 0x22, 0xC5, 0x5E, 0xFF };
static const Color COL_MISS    = { 0xEF, 0x44, 0x44, 0xFF };
static const Color COL_REPLACE = { 0xF5, 0x9E, 0x0B, 0xFF };
static const Color COL_DIM     = { 0xA0, 0xAE, 0xC0, 0xFF };
static const Color COL_TEXT    = { 0xF8, 0xFA, 0xFC, 0xFF };

/* ------------------------------------------------------------------ layout */
static const Rectangle R_CONFIG  = {  12.0f, 138.0f, 296.0f, 570.0f };
static const Rectangle R_TABLE   = { 320.0f, 138.0f, 536.0f, 330.0f };
static const Rectangle R_HISTORY = { 320.0f, 480.0f, 536.0f, 228.0f };
static const Rectangle R_ACCESS  = { 868.0f, 138.0f, 400.0f, 246.0f };
static const Rectangle R_RESULT  = { 868.0f, 396.0f, 400.0f, 312.0f };

#define TABLE_ROW_H    24.0f
#define TABLE_BODY_Y   64.0f
#define TABLE_BODY_H   258.0f
#define HISTORY_ROW_H  22.0f
#define HISTORY_BODY_Y 60.0f
#define HISTORY_BODY_H 162.0f
#define AUTO_STEP_SECONDS 0.9

/* ------------------------------------------------------------ small helpers */
static Rectangle rect_in(Rectangle panel, float dx, float dy, float w, float h)
{
    return (Rectangle){ panel.x + dx, panel.y + dy, w, h };
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int log2_exact(uint32_t v)
{
    if (v == 0u || (v & (v - 1u)) != 0u) {
        return -1;
    }
    int n = 0;
    while (v > 1u) {
        v >>= 1;
        n++;
    }
    return n;
}

#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
static void set_message(Gui *g, bool isError, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g->message, sizeof g->message, fmt, args);
    va_end(args);
    g->messageIsError = isError;
}

/* -------------------------------------------------------------------- text */
static float spacing_for(const Gui *g, float size)
{
    return g->fontLoadedFromFile ? 0.0f : size / 10.0f;
}

static void ui_text(const Gui *g, const char *text, float x, float y, float size, Color color)
{
    DrawTextEx(g->font, text, (Vector2){ x, y }, size, spacing_for(g, size), color);
}

static float ui_text_width(const Gui *g, const char *text, float size)
{
    return MeasureTextEx(g->font, text, size, spacing_for(g, size)).x;
}

static void ui_text_right(const Gui *g, const char *text, float xRight, float y, float size, Color color)
{
    ui_text(g, text, xRight - ui_text_width(g, text, size), y, size, color);
}

/* Word-wrapped text; returns the height used. */
static float ui_text_wrapped(const Gui *g, const char *text, Rectangle area, float size,
                             float lineHeight, Color color)
{
    char line[384] = "";
    float y = area.y;
    const char *p = text;

    while (*p != '\0') {
        const char *start = p;
        while (*p != '\0' && *p != ' ') {
            p++;
        }
        char word[128];
        size_t wordLen = (size_t)(p - start);
        if (wordLen >= sizeof word) {
            wordLen = sizeof word - 1u;
        }
        memcpy(word, start, wordLen);
        word[wordLen] = '\0';
        while (*p == ' ') {
            p++;
        }

        char candidate[512];
        snprintf(candidate, sizeof candidate, "%s%s%s", line, line[0] != '\0' ? " " : "", word);
        if (line[0] != '\0' && ui_text_width(g, candidate, size) > area.width) {
            ui_text(g, line, area.x, y, size, color);
            y += lineHeight;
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%.*s", (int)sizeof line - 1, candidate);
        }
    }
    if (line[0] != '\0') {
        ui_text(g, line, area.x, y, size, color);
        y += lineHeight;
    }
    return y - area.y;
}

/* ----------------------------------------------------------------- widgets */
typedef enum { BTN_SECONDARY = 0, BTN_PRIMARY, BTN_DANGER } ButtonStyle;
enum { FIELD_CHANGED = 1, FIELD_SUBMITTED = 2 };

static bool ui_button(Gui *g, Rectangle r, const char *label, ButtonStyle style,
                      bool enabled, bool active)
{
    const bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    const bool down  = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);

    Color fill = COL_BUTTON;
    if (style == BTN_PRIMARY)      { fill = COL_PRIMARY; }
    else if (style == BTN_DANGER)  { fill = ColorBrightness(COL_MISS, -0.35f); }
    if (active)                    { fill = COL_PRIMARY; }
    if (down)                      { fill = ColorBrightness(fill, -0.2f); }
    else if (hover)                { fill = ColorBrightness(fill, 0.15f); }
    if (!enabled)                  { fill = ColorAlpha(fill, 0.35f); }

    DrawRectangleRounded(r, 0.25f, 8, fill);
    const float size = 15.0f;
    const float tw   = ui_text_width(g, label, size);
    ui_text(g, label, r.x + (r.width - tw) / 2.0f, r.y + (r.height - size) / 2.0f - 1.0f, size,
            enabled ? COL_TEXT : ColorAlpha(COL_TEXT, 0.45f));

    if (hover) {
        g->cursor = MOUSE_CURSOR_POINTING_HAND;
    }
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static void field_init(TextField *f, size_t limit, bool numericOnly, const char *initial)
{
    memset(f, 0, sizeof *f);
    f->limit       = limit;
    f->numericOnly = numericOnly;
    snprintf(f->text, sizeof f->text, "%s", initial);
    f->length = strlen(f->text);
}

static bool field_accepts(const TextField *f, int ch)
{
    if (ch >= '0' && ch <= '9') {
        return true;
    }
    return !f->numericOnly && (ch == ',' || ch == ';' || ch == ' ');
}

static bool field_parse_u32(const TextField *f, uint32_t *out)
{
    if (f->length == 0u) {
        return false;
    }
    uint64_t value = 0u;
    for (size_t i = 0u; i < f->length; i++) {
        if (f->text[i] < '0' || f->text[i] > '9') {
            return false;
        }
        value = value * 10u + (uint64_t)(f->text[i] - '0');
        if (value > UINT32_MAX) {
            return false;
        }
    }
    *out = (uint32_t)value;
    return true;
}

static int ui_field(Gui *g, Rectangle r, TextField *f, bool enabled)
{
    int events = 0;
    const bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    if (hover) {
        g->cursor = MOUSE_CURSOR_IBEAM;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            g->focus = f;
        }
    }
    const bool focused = enabled && g->focus == f;

    if (focused) {
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            if (f->length + 1u < sizeof f->text && f->length < f->limit && field_accepts(f, ch)) {
                f->text[f->length++] = (char)ch;
                f->text[f->length]   = '\0';
                events |= FIELD_CHANGED;
            }
        }
        /* The press queue (unlike IsKeyPressed) never loses two quick taps in one frame. */
        int key;
        while ((key = GetKeyPressed()) != 0) {
            if (key == KEY_BACKSPACE && f->length > 0u) {
                f->text[--f->length] = '\0';
                events |= FIELD_CHANGED;
            } else if (key == KEY_ENTER || key == KEY_KP_ENTER) {
                events |= FIELD_SUBMITTED;
            } else if (key == KEY_ESCAPE) {
                g->focus = NULL;
            }
        }
        if (IsKeyPressedRepeat(KEY_BACKSPACE) && f->length > 0u) {   /* key held down */
            f->text[--f->length] = '\0';
            events |= FIELD_CHANGED;
        }
    }

    DrawRectangleRounded(r, 0.15f, 6, enabled ? COL_FIELD : ColorAlpha(COL_FIELD, 0.5f));
    DrawRectangleRoundedLinesEx(r, 0.15f, 6, focused ? 2.0f : 1.0f, focused ? COL_PRIMARY : COL_BORDER);

    const float size  = 17.0f;
    const float pad   = 8.0f;
    const float tw    = ui_text_width(g, f->text, size);
    const float tx    = (tw > r.width - 2.0f * pad) ? r.x + r.width - pad - tw : r.x + pad;
    const float ty    = r.y + (r.height - size) / 2.0f - 1.0f;
    BeginScissorMode((int)r.x + 2, (int)r.y, (int)r.width - 4, (int)r.height);
    ui_text(g, f->text, tx, ty, size, enabled ? COL_TEXT : COL_DIM);
    if (focused && ((int)(GetTime() * 2.0) % 2) == 0) {
        DrawRectangle((int)(tx + tw + 1.0f), (int)ty, 2, (int)size, COL_TEXT);
    }
    EndScissorMode();
    return events;
}

static void draw_panel(const Gui *g, Rectangle r, const char *title)
{
    DrawRectangleRounded(r, 0.03f, 8, COL_PANEL);
    ui_text(g, title, r.x + 12.0f, r.y + 9.0f, 20.0f, COL_TEXT);
}

/* ------------------------------------------------------- form <-> simulator */
static bool form_read_config(const Gui *g, CacheConfig *cfg, char *err, size_t errSize)
{
    cfg->mapping = g->mapping;
    cfg->policy  = g->policy;
    cfg->ways    = 1u;

    if (!field_parse_u32(&g->fieldCacheSize, &cfg->cacheSize)) {
        snprintf(err, errSize, "Cache size must be a whole number of bytes.");
        return false;
    }
    if (!field_parse_u32(&g->fieldBlockSize, &cfg->blockSize)) {
        snprintf(err, errSize, "Block size must be a whole number of bytes.");
        return false;
    }
    if (!field_parse_u32(&g->fieldMemorySize, &cfg->memorySize)) {
        snprintf(err, errSize, "Memory size must be a whole number of bytes.");
        return false;
    }
    if (g->mapping == MAPPING_SET_ASSOCIATIVE && !field_parse_u32(&g->fieldWays, &cfg->ways)) {
        snprintf(err, errSize, "Ways per set must be a whole number.");
        return false;
    }
    const ConfigStatus status = cache_config_validate(cfg);
    if (status != CONFIG_OK) {
        snprintf(err, errSize, "%s", cache_config_status_message(status));
        return false;
    }
    err[0] = '\0';
    return true;
}

static void form_fill(Gui *g, const CacheConfig *cfg)
{
    snprintf(g->fieldCacheSize.text,  sizeof g->fieldCacheSize.text,  "%u", (unsigned)cfg->cacheSize);
    snprintf(g->fieldBlockSize.text,  sizeof g->fieldBlockSize.text,  "%u", (unsigned)cfg->blockSize);
    snprintf(g->fieldMemorySize.text, sizeof g->fieldMemorySize.text, "%u", (unsigned)cfg->memorySize);
    snprintf(g->fieldWays.text,       sizeof g->fieldWays.text,       "%u", (unsigned)cfg->ways);
    g->fieldCacheSize.length  = strlen(g->fieldCacheSize.text);
    g->fieldBlockSize.length  = strlen(g->fieldBlockSize.text);
    g->fieldMemorySize.length = strlen(g->fieldMemorySize.text);
    g->fieldWays.length       = strlen(g->fieldWays.text);
    g->mapping = cfg->mapping;
    g->policy  = cfg->policy;
}

static void sequence_to_field(Gui *g)
{
    simulator_format_sequence(&g->sim, g->fieldSequence.text, sizeof g->fieldSequence.text);
    g->fieldSequence.length = strlen(g->fieldSequence.text);
    g->sequenceDirty = false;
}

/* ----------------------------------------------------------------- actions */
static void scroll_table_to(Gui *g, uint32_t line)
{
    const float top = (float)line * TABLE_ROW_H;
    if (top < g->tableScroll) {
        g->tableScroll = top;
    } else if (top + TABLE_ROW_H > g->tableScroll + TABLE_BODY_H) {
        g->tableScroll = top + TABLE_ROW_H - TABLE_BODY_H;
    }
}

static void show_result(Gui *g, const AccessResult *r)
{
    g->last           = *r;
    g->hasResult      = true;
    g->lastAccessTime = GetTime();
    g->historyScroll  = 0.0f;
    simulator_explain(&g->sim, r, &g->explanation);
    scroll_table_to(g, r->selectedLine);
    set_message(g, false, "%s", r->message);
}

static void action_apply(Gui *g)
{
    CacheConfig cfg;
    char err[160];
    if (!form_read_config(g, &cfg, err, sizeof err)) {
        set_message(g, true, "%s", err);
        return;
    }
    Simulator fresh;
    const ConfigStatus status = simulator_init(&fresh, &cfg);
    if (status != CONFIG_OK) {
        set_message(g, true, "%s", cache_config_status_message(status));
        return;
    }

    simulator_destroy(&g->sim);
    g->sim = fresh;
    g->hasResult = false;
    g->autoRun = false;
    g->tableScroll = 0.0f;

    /* keep the typed sequence if it is still valid for the new memory size */
    const SequenceStatus seq = simulator_load_sequence(&g->sim, g->fieldSequence.text);
    if (seq != SEQ_OK) {
        simulator_generate_sequence(&g->sim);
        sequence_to_field(g);
        set_message(g, false, "Configuration applied. The old sequence no longer fits, so a new one was generated.");
    } else {
        g->sequenceDirty = false;
        set_message(g, false, "Configuration applied. Cache is empty.");
    }
}

static void action_defaults(Gui *g)
{
    const CacheConfig cfg = { DEFAULT_CACHE_SIZE, DEFAULT_BLOCK_SIZE, DEFAULT_MEMORY_SIZE,
                              DEFAULT_WAYS, MAPPING_DIRECT, POLICY_FIFO };
    form_fill(g, &cfg);
    action_apply(g);
}

static bool ensure_sequence_loaded(Gui *g)
{
    if (!g->sequenceDirty) {
        return true;
    }
    const SequenceStatus status = simulator_load_sequence(&g->sim, g->fieldSequence.text);
    if (status != SEQ_OK) {
        set_message(g, true, "%s", simulator_sequence_status_message(status));
        g->autoRun = false;
        return false;
    }
    g->sequenceDirty = false;
    g->hasResult = false;
    set_message(g, false, "Sequence loaded (%u addresses). Simulation restarted with an empty cache.",
                (unsigned)g->sim.sequenceLength);
    return true;
}

static void action_access(Gui *g)
{
    uint32_t address;
    if (!field_parse_u32(&g->fieldAddress, &address)) {
        set_message(g, true, "Enter the memory address as a whole number (bytes).");
        return;
    }
    AccessResult r;
    const AccessStatus status = simulator_access(&g->sim, address, &r);
    if (status != ACCESS_OK) {
        set_message(g, true, "%s Valid range: 0 to %u.", cache_access_status_message(status),
                    (unsigned)(g->sim.cache.config.memorySize - 1u));
        return;
    }
    show_result(g, &r);
}

static void action_next(Gui *g)
{
    if (!ensure_sequence_loaded(g)) {
        return;
    }
    AccessResult r;
    const AccessStatus status = simulator_step_sequence(&g->sim, &r);
    if (status != ACCESS_OK) {
        set_message(g, status != ACCESS_ERR_END_OF_SEQUENCE, "%s", cache_access_status_message(status));
        g->autoRun = false;
        return;
    }
    show_result(g, &r);
}

static void action_previous(Gui *g)
{
    g->autoRun = false;
    if (!simulator_undo(&g->sim)) {
        set_message(g, true, "Nothing to undo.");
        return;
    }
    if (g->sim.historyCount > 0u) {
        show_result(g, &g->sim.history[g->sim.historyCount - 1u].result);
        set_message(g, false, "Went back one access. Showing access #%u.", (unsigned)g->sim.historyCount);
    } else {
        g->hasResult = false;
        set_message(g, false, "Back to the start: cache is empty.");
    }
}

static void action_reset(Gui *g)
{
    simulator_reset(&g->sim);
    g->hasResult = false;
    g->autoRun = false;
    g->tableScroll = 0.0f;
    set_message(g, false, "Simulation reset: cache emptied and statistics cleared.");
}

static void action_generate(Gui *g)
{
    simulator_generate_sequence(&g->sim);
    sequence_to_field(g);
    g->hasResult = false;
    g->autoRun = false;
    set_message(g, false, "Generated %u addresses. Simulation restarted with an empty cache.",
                (unsigned)g->sim.sequenceLength);
}

static void action_toggle_auto(Gui *g)
{
    if (g->autoRun) {
        g->autoRun = false;
        return;
    }
    if (!ensure_sequence_loaded(g)) {
        return;
    }
    if (!simulator_has_next(&g->sim)) {
        set_message(g, true, "%s", cache_access_status_message(ACCESS_ERR_END_OF_SEQUENCE));
        return;
    }
    g->autoRun = true;
    g->nextAutoStep = GetTime();
}

/* ------------------------------------------------------------ header/stats */
static void draw_header(Gui *g)
{
    const CacheConfig *c = &g->sim.cache.config;
    ui_text(g, "Cache Memory Simulator", 14.0f, 7.0f, 30.0f, COL_TEXT);
    ui_text(g, "Computer System Architecture  |  interactive cache simulation", 14.0f, 40.0f, 15.0f, COL_DIM);

    char summary[160];
    snprintf(summary, sizeof summary, "Cache %u B  |  Block %u B  |  %u lines  |  %s  |  %s",
             (unsigned)c->cacheSize, (unsigned)c->blockSize, (unsigned)g->sim.cache.geometry.totalLines,
             mapping_name(c->mapping), replacement_name(c->policy));
    ui_text_right(g, summary, 1268.0f, 22.0f, 17.0f, COL_DIM);
}

static void draw_stat_card(const Gui *g, int index, const char *label, const char *value, Color accent)
{
    const float w = 241.0f;
    const Rectangle r = { 12.0f + (float)index * (w + 12.0f), 66.0f, w, 60.0f };
    DrawRectangleRounded(r, 0.12f, 8, COL_PANEL);
    DrawRectangle((int)r.x, (int)r.y + 10, 4, 40, accent);
    ui_text(g, label, r.x + 16.0f, r.y + 7.0f, 14.0f, COL_DIM);
    ui_text(g, value, r.x + 16.0f, r.y + 25.0f, 28.0f, COL_TEXT);
}

static void draw_stats(const Gui *g)
{
    const SimStats *s = &g->sim.stats;
    char buf[48];

    snprintf(buf, sizeof buf, "%llu", (unsigned long long)s->accesses);
    draw_stat_card(g, 0, "Total accesses", buf, COL_PRIMARY);
    snprintf(buf, sizeof buf, "%llu", (unsigned long long)s->hits);
    draw_stat_card(g, 1, "Cache hits", buf, COL_HIT);
    snprintf(buf, sizeof buf, "%llu", (unsigned long long)s->misses);
    draw_stat_card(g, 2, "Cache misses", buf, COL_MISS);
    snprintf(buf, sizeof buf, "%.2f %%", simulator_hit_ratio(s));
    draw_stat_card(g, 3, "Hit ratio = hits / accesses", buf, COL_HIT);
    snprintf(buf, sizeof buf, "%.2f %%", simulator_miss_ratio(s));
    draw_stat_card(g, 4, "Miss ratio = misses / accesses", buf, COL_MISS);
}

/* ------------------------------------------------------------ config panel */
static void draw_config_panel(Gui *g)
{
    draw_panel(g, R_CONFIG, "Configuration");

    CacheConfig preview;
    char err[160];
    const bool valid = form_read_config(g, &preview, err, sizeof err);
    const bool setAssoc = (g->mapping == MAPPING_SET_ASSOCIATIVE);
    const bool policyMatters = (g->mapping != MAPPING_DIRECT);

    ui_text(g, "Cache size (bytes)", R_CONFIG.x + 12.0f, R_CONFIG.y + 42.0f, 13.0f, COL_DIM);
    ui_field(g, rect_in(R_CONFIG, 12.0f, 60.0f, 272.0f, 32.0f), &g->fieldCacheSize, true);
    ui_text(g, "Block size (bytes)", R_CONFIG.x + 12.0f, R_CONFIG.y + 100.0f, 13.0f, COL_DIM);
    ui_field(g, rect_in(R_CONFIG, 12.0f, 118.0f, 272.0f, 32.0f), &g->fieldBlockSize, true);
    ui_text(g, "Main memory size (bytes)", R_CONFIG.x + 12.0f, R_CONFIG.y + 158.0f, 13.0f, COL_DIM);
    ui_field(g, rect_in(R_CONFIG, 12.0f, 176.0f, 272.0f, 32.0f), &g->fieldMemorySize, true);

    ui_text(g, "Mapping technique", R_CONFIG.x + 12.0f, R_CONFIG.y + 216.0f, 13.0f, COL_DIM);
    static const char *const mappingLabels[MAPPING_COUNT] = { "Direct", "Fully Assoc.", "Set Assoc." };
    for (int i = 0; i < MAPPING_COUNT; i++) {
        if (ui_button(g, rect_in(R_CONFIG, 12.0f + (float)i * 93.0f, 234.0f, 86.0f, 32.0f),
                      mappingLabels[i], BTN_SECONDARY, true, g->mapping == (MappingType)i)) {
            g->mapping = (MappingType)i;
        }
    }

    ui_text(g, "Ways per set (set associative only)", R_CONFIG.x + 12.0f, R_CONFIG.y + 274.0f, 13.0f, COL_DIM);
    ui_field(g, rect_in(R_CONFIG, 12.0f, 292.0f, 272.0f, 32.0f), &g->fieldWays, setAssoc);

    ui_text(g, policyMatters ? "Replacement policy" : "Replacement policy (unused for Direct)",
            R_CONFIG.x + 12.0f, R_CONFIG.y + 332.0f, 13.0f, COL_DIM);
    static const char *const policyLabels[POLICY_COUNT] = { "FIFO", "LRU" };
    for (int i = 0; i < POLICY_COUNT; i++) {
        if (ui_button(g, rect_in(R_CONFIG, 12.0f + (float)i * 140.0f, 350.0f, 132.0f, 32.0f),
                      policyLabels[i], BTN_SECONDARY, policyMatters, g->policy == (ReplacementPolicy)i)) {
            g->policy = (ReplacementPolicy)i;
        }
    }

    if (ui_button(g, rect_in(R_CONFIG, 12.0f, 394.0f, 190.0f, 36.0f), "Apply Configuration",
                  BTN_PRIMARY, valid, false)) {
        action_apply(g);
    }
    if (ui_button(g, rect_in(R_CONFIG, 208.0f, 394.0f, 76.0f, 36.0f), "Defaults",
                  BTN_SECONDARY, true, false)) {
        action_defaults(g);
    }

    /* live "calculated parameters" for the values currently typed */
    ui_text(g, "Calculated parameters", R_CONFIG.x + 12.0f, R_CONFIG.y + 442.0f, 14.0f, COL_TEXT);
    if (valid) {
        const CacheGeometry geo = mapping_geometry(&preview);
        char line[128];
        snprintf(line, sizeof line, "Lines %u  |  Sets %u  |  Ways %u",
                 (unsigned)geo.totalLines, (unsigned)geo.numSets, (unsigned)geo.ways);
        ui_text(g, line, R_CONFIG.x + 12.0f, R_CONFIG.y + 462.0f, 14.0f, COL_DIM);
        snprintf(line, sizeof line, "Memory blocks %u  (%u / %u)",
                 (unsigned)geo.memoryBlocks, (unsigned)preview.memorySize, (unsigned)preview.blockSize);
        ui_text(g, line, R_CONFIG.x + 12.0f, R_CONFIG.y + 480.0f, 14.0f, COL_DIM);

        const int offsetBits = log2_exact(preview.blockSize);
        const int indexBits  = log2_exact(geo.numSets);
        const int memBits    = log2_exact(preview.memorySize);
        if (offsetBits >= 0 && indexBits >= 0 && memBits >= 0) {
            snprintf(line, sizeof line, "Bits: tag %d | index %d | offset %d",
                     memBits - indexBits - offsetBits, indexBits, offsetBits);
        } else {
            snprintf(line, sizeof line, "Bits: n/a (not powers of 2)");
        }
        ui_text(g, line, R_CONFIG.x + 12.0f, R_CONFIG.y + 498.0f, 14.0f, COL_DIM);
    } else {
        ui_text_wrapped(g, err, rect_in(R_CONFIG, 12.0f, 464.0f, 272.0f, 40.0f), 14.0f, 17.0f, COL_MISS);
    }

    /* status line of the last action */
    if (g->message[0] != '\0') {
        BeginScissorMode((int)R_CONFIG.x, (int)(R_CONFIG.y + 516.0f), (int)R_CONFIG.width, 52);
        ui_text_wrapped(g, g->message, rect_in(R_CONFIG, 12.0f, 520.0f, 272.0f, 44.0f), 13.0f, 16.0f,
                        g->messageIsError ? COL_MISS : COL_DIM);
        EndScissorMode();
    }
}

/* ------------------------------------------------------------- cache table */
static void draw_legend_item(const Gui *g, float *xRight, float y, const char *label, Color color)
{
    const float w = ui_text_width(g, label, 13.0f);
    ui_text_right(g, label, *xRight, y, 13.0f, COL_DIM);
    DrawRectangle((int)(*xRight - w - 14.0f), (int)y + 3, 9, 9, color);
    *xRight -= w + 28.0f;
}

static void draw_cache_table(Gui *g)
{
    draw_panel(g, R_TABLE, "Cache lines");

    float legendX = R_TABLE.x + R_TABLE.width - 14.0f;
    draw_legend_item(g, &legendX, R_TABLE.y + 14.0f, "REPLACED", COL_REPLACE);
    draw_legend_item(g, &legendX, R_TABLE.y + 14.0f, "MISS", COL_MISS);
    draw_legend_item(g, &legendX, R_TABLE.y + 14.0f, "HIT", COL_HIT);

    const Cache *cache      = &g->sim.cache;
    const uint32_t total    = cache->geometry.totalLines;
    const bool showSet      = (cache->config.mapping == MAPPING_SET_ASSOCIATIVE);
    const float contentH    = (float)total * TABLE_ROW_H;
    const float maxScroll   = contentH > TABLE_BODY_H ? contentH - TABLE_BODY_H : 0.0f;

    const Rectangle body = rect_in(R_TABLE, 6.0f, TABLE_BODY_Y, R_TABLE.width - 12.0f, TABLE_BODY_H);
    if (CheckCollisionPointRec(GetMousePosition(), body)) {
        g->tableScroll -= GetMouseWheelMove() * TABLE_ROW_H * 2.0f;
    }
    g->tableScroll = clampf(g->tableScroll, 0.0f, maxScroll);

    /* column layout */
    const float colW[8] = { 52.0f, showSet ? 48.0f : 0.0f, 56.0f, 64.0f, 72.0f, 68.0f, 60.0f, 0.0f };
    const char *const colTitle[8] = { "Line", "Set", "Valid", "Tag", "Block", "Loaded", "Used", "Status" };
    float colX[8];
    float x = R_TABLE.x + 18.0f;
    for (int i = 0; i < 8; i++) {
        colX[i] = x;
        x += colW[i];
    }
    for (int i = 0; i < 8; i++) {
        if (i == 1 && !showSet) {
            continue;
        }
        ui_text(g, colTitle[i], colX[i], R_TABLE.y + 42.0f, 13.0f, COL_DIM);
    }
    DrawLine((int)R_TABLE.x + 10, (int)(R_TABLE.y + 60.0f), (int)(R_TABLE.x + R_TABLE.width - 10.0f),
             (int)(R_TABLE.y + 60.0f), COL_BORDER);

    const bool haveResult = g->hasResult && g->last.status == ACCESS_OK;
    const float age  = (float)(GetTime() - g->lastAccessTime);
    const float fade = age < 1.0f ? 1.0f - age : 0.0f;

    BeginScissorMode((int)body.x, (int)body.y, (int)body.width, (int)body.height);
    for (uint32_t i = 0u; i < total; i++) {
        const float y = body.y + (float)i * TABLE_ROW_H - g->tableScroll;
        if (y + TABLE_ROW_H < body.y || y > body.y + body.height) {
            continue;
        }
        const CacheLine *line = &cache->lines[i];
        const bool selected = haveResult && g->last.selectedLine == i;
        const bool inSet = haveResult && showSet && (i / cache->geometry.ways) == g->last.setIndex;
        const bool recent = line->valid && !selected && line->lastUsed + 2u >= cache->clock;

        DrawRectangle((int)body.x, (int)y, (int)body.width, (int)TABLE_ROW_H - 1,
                      (i % 2u) ? COL_ROW_ALT : COL_PANEL);
        if (inSet && !selected) {
            DrawRectangle((int)body.x, (int)y, (int)body.width, (int)TABLE_ROW_H - 1, ColorAlpha(COL_PRIMARY, 0.10f));
        }
        if (recent) {
            DrawRectangle((int)body.x, (int)y, (int)body.width, (int)TABLE_ROW_H - 1, ColorAlpha(COL_PRIMARY, 0.14f));
        }

        Color statusColor = COL_DIM;
        const char *status = line->valid ? "cached" : "EMPTY";
        if (selected) {
            const Color accent = g->last.outcome == ACCESS_HIT ? COL_HIT
                               : (g->last.replaced ? COL_REPLACE : COL_MISS);
            DrawRectangle((int)body.x, (int)y, (int)body.width, (int)TABLE_ROW_H - 1,
                          ColorAlpha(accent, 0.18f + 0.30f * fade));
            DrawRectangle((int)body.x, (int)y, 4, (int)TABLE_ROW_H - 1, accent);
            statusColor = accent;
            status = g->last.outcome == ACCESS_HIT ? "HIT" : (g->last.replaced ? "REPLACED" : "MISS");
        } else if (recent) {
            statusColor = COL_PRIMARY;
            status = "recent";
        }

        const float ty = y + 4.0f;
        char buf[32];
        snprintf(buf, sizeof buf, "%u", (unsigned)i);
        ui_text(g, buf, colX[0], ty, 15.0f, COL_TEXT);
        if (showSet) {
            snprintf(buf, sizeof buf, "%u", (unsigned)(i / cache->geometry.ways));
            ui_text(g, buf, colX[1], ty, 15.0f, COL_DIM);
        }
        ui_text(g, line->valid ? "1" : "0", colX[2], ty, 15.0f, line->valid ? COL_HIT : COL_DIM);
        if (line->valid) {
            snprintf(buf, sizeof buf, "%u", (unsigned)line->tag);
            ui_text(g, buf, colX[3], ty, 15.0f, COL_TEXT);
            snprintf(buf, sizeof buf, "%u", (unsigned)line->blockNumber);
            ui_text(g, buf, colX[4], ty, 15.0f, COL_TEXT);
            snprintf(buf, sizeof buf, "%llu", (unsigned long long)line->loadedAt);
            ui_text(g, buf, colX[5], ty, 15.0f, COL_DIM);
            snprintf(buf, sizeof buf, "%llu", (unsigned long long)line->lastUsed);
            ui_text(g, buf, colX[6], ty, 15.0f, COL_DIM);
        } else {
            ui_text(g, "-", colX[3], ty, 15.0f, COL_DIM);
            ui_text(g, "-", colX[4], ty, 15.0f, COL_DIM);
        }
        ui_text(g, status, colX[7], ty, 15.0f, statusColor);
    }
    EndScissorMode();

    if (maxScroll > 0.0f) {
        const float barH = TABLE_BODY_H * TABLE_BODY_H / contentH;
        const float barY = body.y + (g->tableScroll / maxScroll) * (TABLE_BODY_H - barH);
        DrawRectangleRounded((Rectangle){ R_TABLE.x + R_TABLE.width - 9.0f, barY, 4.0f, barH }, 1.0f, 4, COL_BORDER);
    }
}

/* ----------------------------------------------------------------- history */
static void draw_history(Gui *g)
{
    draw_panel(g, R_HISTORY, "Access history");
    const SimStats *s = &g->sim.stats;
    char count[48];
    snprintf(count, sizeof count, "%llu accesses (newest first)", (unsigned long long)s->accesses);
    ui_text_right(g, count, R_HISTORY.x + R_HISTORY.width - 14.0f, R_HISTORY.y + 14.0f, 13.0f, COL_DIM);

    const float colX[6] = { 18.0f, 62.0f, 140.0f, 204.0f, 268.0f, 350.0f };
    const char *const colTitle[6] = { "#", "Address", "Block", "Line", "Result", "Replaced" };
    for (int i = 0; i < 6; i++) {
        ui_text(g, colTitle[i], R_HISTORY.x + colX[i], R_HISTORY.y + 38.0f, 13.0f, COL_DIM);
    }
    DrawLine((int)R_HISTORY.x + 10, (int)(R_HISTORY.y + 57.0f), (int)(R_HISTORY.x + R_HISTORY.width - 10.0f),
             (int)(R_HISTORY.y + 57.0f), COL_BORDER);

    const uint32_t n = g->sim.historyCount;
    const Rectangle body = rect_in(R_HISTORY, 6.0f, HISTORY_BODY_Y, R_HISTORY.width - 12.0f, HISTORY_BODY_H);
    const float contentH = (float)n * HISTORY_ROW_H;
    const float maxScroll = contentH > HISTORY_BODY_H ? contentH - HISTORY_BODY_H : 0.0f;
    if (CheckCollisionPointRec(GetMousePosition(), body)) {
        g->historyScroll -= GetMouseWheelMove() * HISTORY_ROW_H * 2.0f;
    }
    g->historyScroll = clampf(g->historyScroll, 0.0f, maxScroll);

    if (n == 0u) {
        ui_text(g, "No accesses yet.", R_HISTORY.x + 18.0f, R_HISTORY.y + 70.0f, 15.0f, COL_DIM);
        return;
    }

    BeginScissorMode((int)body.x, (int)body.y, (int)body.width, (int)body.height);
    for (uint32_t row = 0u; row < n; row++) {
        const float y = body.y + (float)row * HISTORY_ROW_H - g->historyScroll;
        if (y + HISTORY_ROW_H < body.y || y > body.y + body.height) {
            continue;
        }
        const uint32_t idx = n - 1u - row;
        const AccessResult *r = &g->sim.history[idx].result;
        if (row % 2u) {
            DrawRectangle((int)body.x, (int)y, (int)body.width, (int)HISTORY_ROW_H - 1, COL_ROW_ALT);
        }
        char buf[32];
        const float ty = y + 3.0f;
        snprintf(buf, sizeof buf, "%u", (unsigned)(idx + 1u));
        ui_text(g, buf, R_HISTORY.x + colX[0], ty, 14.0f, COL_DIM);
        snprintf(buf, sizeof buf, "%u", (unsigned)r->address);
        ui_text(g, buf, R_HISTORY.x + colX[1], ty, 14.0f, COL_TEXT);
        snprintf(buf, sizeof buf, "%u", (unsigned)r->blockNumber);
        ui_text(g, buf, R_HISTORY.x + colX[2], ty, 14.0f, COL_TEXT);
        snprintf(buf, sizeof buf, "%u", (unsigned)r->selectedLine);
        ui_text(g, buf, R_HISTORY.x + colX[3], ty, 14.0f, COL_TEXT);
        ui_text(g, r->outcome == ACCESS_HIT ? "HIT" : "MISS", R_HISTORY.x + colX[4], ty, 14.0f,
                r->outcome == ACCESS_HIT ? COL_HIT : COL_MISS);
        if (r->replaced) {
            snprintf(buf, sizeof buf, "block %u", (unsigned)r->replacedBlock);
            ui_text(g, buf, R_HISTORY.x + colX[5], ty, 14.0f, COL_REPLACE);
        } else {
            ui_text(g, "-", R_HISTORY.x + colX[5], ty, 14.0f, COL_DIM);
        }
    }
    EndScissorMode();
}

/* ------------------------------------------------------------ access panel */
static void draw_access_panel(Gui *g)
{
    draw_panel(g, R_ACCESS, "Memory access");

    ui_text(g, "Byte address (decimal)", R_ACCESS.x + 12.0f, R_ACCESS.y + 36.0f, 13.0f, COL_DIM);
    const int addrEvents = ui_field(g, rect_in(R_ACCESS, 12.0f, 52.0f, 160.0f, 32.0f), &g->fieldAddress, true);
    if ((addrEvents & FIELD_SUBMITTED) ||
        ui_button(g, rect_in(R_ACCESS, 180.0f, 52.0f, 208.0f, 32.0f), "Access Memory", BTN_PRIMARY, true, false)) {
        action_access(g);
    }

    ui_text(g, "Sequence (comma-separated addresses)", R_ACCESS.x + 12.0f, R_ACCESS.y + 92.0f, 13.0f, COL_DIM);
    const int seqEvents = ui_field(g, rect_in(R_ACCESS, 12.0f, 108.0f, 376.0f, 32.0f), &g->fieldSequence, true);
    if (seqEvents & FIELD_CHANGED) {
        g->sequenceDirty = true;
        g->autoRun = false;
    }
    if (seqEvents & FIELD_SUBMITTED) {
        ensure_sequence_loaded(g);
    }

    char info[128];
    if (g->sequenceDirty) {
        snprintf(info, sizeof info, "Sequence edited - press Enter or Next Access to load it.");
    } else if (simulator_has_next(&g->sim)) {
        snprintf(info, sizeof info, "Next: #%u of %u (address %u)", (unsigned)g->sim.sequenceNext + 1u,
                 (unsigned)g->sim.sequenceLength, (unsigned)g->sim.sequence[g->sim.sequenceNext]);
    } else {
        snprintf(info, sizeof info, "Sequence finished (%u of %u). Reset to replay.",
                 (unsigned)g->sim.sequenceNext, (unsigned)g->sim.sequenceLength);
    }
    ui_text(g, info, R_ACCESS.x + 12.0f, R_ACCESS.y + 146.0f, 13.0f, COL_DIM);

    if (ui_button(g, rect_in(R_ACCESS, 12.0f, 166.0f, 88.0f, 32.0f), "Previous", BTN_SECONDARY,
                  g->sim.historyCount > 0u, false)) {
        action_previous(g);
    }
    if (ui_button(g, rect_in(R_ACCESS, 107.0f, 166.0f, 130.0f, 32.0f), "Next Access", BTN_PRIMARY, true, false)) {
        action_next(g);
    }
    if (ui_button(g, rect_in(R_ACCESS, 244.0f, 166.0f, 144.0f, 32.0f),
                  g->autoRun ? "Auto Run: ON" : "Auto Run: OFF", BTN_SECONDARY, true, g->autoRun)) {
        action_toggle_auto(g);
    }
    if (ui_button(g, rect_in(R_ACCESS, 12.0f, 204.0f, 184.0f, 32.0f), "Generate Sequence", BTN_SECONDARY, true, false)) {
        action_generate(g);
    }
    if (ui_button(g, rect_in(R_ACCESS, 204.0f, 204.0f, 184.0f, 32.0f), "Reset Simulation", BTN_DANGER, true, false)) {
        action_reset(g);
    }
}

/* ------------------------------------------------------------ result panel */
static void draw_result_panel(Gui *g)
{
    draw_panel(g, R_RESULT, "Result and step-by-step");
    const float x = R_RESULT.x + 12.0f;
    const float w = R_RESULT.width - 24.0f;
    float y = R_RESULT.y + 42.0f;

    if (!g->hasResult) {
        ui_text_wrapped(g, "No access yet. Type an address and press Access Memory, or press Next Access to step "
                           "through the sequence one address at a time.",
                        (Rectangle){ x, y, w, 80.0f }, 15.0f, 20.0f, COL_DIM);
        return;
    }

    const AccessResult *r = &g->last;
    const bool hit = (r->outcome == ACCESS_HIT);
    const Rectangle badge = { x, y, 80.0f, 32.0f };
    DrawRectangleRounded(badge, 0.3f, 8, hit ? COL_HIT : COL_MISS);
    const char *badgeText = hit ? "HIT" : "MISS";
    ui_text(g, badgeText, badge.x + (badge.width - ui_text_width(g, badgeText, 20.0f)) / 2.0f, badge.y + 5.0f,
            20.0f, COL_TEXT);
    ui_text_wrapped(g, r->message, (Rectangle){ x + 92.0f, y, w - 92.0f, 36.0f }, 14.0f, 16.0f, COL_TEXT);
    y += 42.0f;

    char line[160];
    snprintf(line, sizeof line, "Address %u  |  Block %u  |  Offset %u",
             (unsigned)r->address, (unsigned)r->blockNumber, (unsigned)r->offset);
    ui_text(g, line, x, y, 15.0f, COL_TEXT);

    char where[64];
    if (g->sim.cache.config.mapping == MAPPING_SET_ASSOCIATIVE) {
        snprintf(where, sizeof where, "Set %u  |  Line %u  |  Tag %u",
                 (unsigned)r->setIndex, (unsigned)r->selectedLine, (unsigned)r->tag);
    } else {
        snprintf(where, sizeof where, "Line %u  |  Tag %u", (unsigned)r->selectedLine, (unsigned)r->tag);
    }
    ui_text(g, where, x, y + 20.0f, 15.0f, COL_TEXT);
    if (r->replaced) {
        snprintf(line, sizeof line, "Replaced: block %u", (unsigned)r->replacedBlock);
        ui_text_right(g, line, x + w, y + 20.0f, 15.0f, COL_REPLACE);
    } else {
        ui_text_right(g, "Replaced: none", x + w, y + 20.0f, 15.0f, COL_DIM);
    }
    y += 46.0f;

    DrawLine((int)x, (int)y, (int)(x + w), (int)y, COL_BORDER);
    y += 6.0f;
    BeginScissorMode((int)R_RESULT.x, (int)y, (int)R_RESULT.width, (int)(R_RESULT.y + R_RESULT.height - y - 4.0f));
    for (size_t i = 0u; i < g->explanation.count; i++) {
        y += ui_text_wrapped(g, g->explanation.text[i], (Rectangle){ x, y, w, 40.0f }, 13.0f, 16.0f, COL_DIM) + 1.0f;
    }
    EndScissorMode();
}

/* ------------------------------------------------------------------- public */
bool gui_init(Gui *g)
{
    memset(g, 0, sizeof *g);

    g->font = GetFontDefault();
    static const char *const fontPaths[] = {
        "assets/font.ttf",                  /* drop a TTF here to override */
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf"
    };
    for (size_t i = 0u; i < sizeof fontPaths / sizeof fontPaths[0]; i++) {
        if (FileExists(fontPaths[i])) {
            const Font font = LoadFontEx(fontPaths[i], 24, NULL, 0);
            if (font.texture.id != 0u && font.glyphCount > 0) {
                SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
                g->font = font;
                g->fontLoadedFromFile = true;
                break;
            }
        }
    }

    field_init(&g->fieldCacheSize,  7u, true, "");
    field_init(&g->fieldBlockSize,  7u, true, "");
    field_init(&g->fieldMemorySize, 8u, true, "");
    field_init(&g->fieldWays,       3u, true, "");
    field_init(&g->fieldAddress,    8u, true, "10");
    field_init(&g->fieldSequence,   GUI_FIELD_TEXT_MAX - 1u, false, DEFAULT_SEQUENCE_TEXT);

    const CacheConfig cfg = { DEFAULT_CACHE_SIZE, DEFAULT_BLOCK_SIZE, DEFAULT_MEMORY_SIZE,
                              DEFAULT_WAYS, MAPPING_DIRECT, POLICY_FIFO };
    form_fill(g, &cfg);
    if (simulator_init(&g->sim, &cfg) != CONFIG_OK) {
        return false;
    }
    simulator_load_sequence(&g->sim, g->fieldSequence.text);
    set_message(g, false, "Ready. Press Next Access to step through the sequence.");
    return true;
}

void gui_shutdown(Gui *g)
{
    simulator_destroy(&g->sim);
    if (g->fontLoadedFromFile) {
        UnloadFont(g->font);
    }
}

void gui_frame(Gui *g)
{
    TextField *const tabOrder[] = { &g->fieldCacheSize, &g->fieldBlockSize, &g->fieldMemorySize,
                                    &g->fieldWays, &g->fieldAddress, &g->fieldSequence };
    const size_t tabCount = sizeof tabOrder / sizeof tabOrder[0];

    g->cursor = MOUSE_CURSOR_DEFAULT;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        g->focus = NULL;                 /* a click on a field re-focuses it below */
    }
    if (IsKeyPressed(KEY_TAB)) {
        size_t next = 0u;
        for (size_t i = 0u; i < tabCount; i++) {
            if (g->focus == tabOrder[i]) {
                next = (i + 1u) % tabCount;
            }
        }
        g->focus = tabOrder[next];
    }
    if (g->focus == NULL) {
        while (GetCharPressed() != 0) { /* drop stray keystrokes */ }
        while (GetKeyPressed() != 0)  { }
    }

    if (g->autoRun && GetTime() >= g->nextAutoStep) {
        action_next(g);
        g->nextAutoStep = GetTime() + AUTO_STEP_SECONDS;
    }

    BeginDrawing();
    ClearBackground(COL_BG);
    draw_header(g);
    draw_stats(g);
    draw_config_panel(g);
    draw_cache_table(g);
    draw_history(g);
    draw_access_panel(g);
    draw_result_panel(g);
    EndDrawing();

    SetMouseCursor(g->cursor);
}
