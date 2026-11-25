#ifndef GUI_H
#define GUI_H

#include <stdbool.h>
#include <stddef.h>
#include "raylib.h"
#include "simulator.h"

#define GUI_WINDOW_WIDTH   1280
#define GUI_WINDOW_HEIGHT  720
#define GUI_WINDOW_TITLE   "Cache Memory Simulator"
#define GUI_FIELD_TEXT_MAX 1200

typedef struct {
    char   text[GUI_FIELD_TEXT_MAX];
    size_t length;
    size_t limit;          /* maximum characters the user may type */
    bool   numericOnly;    /* digits only; otherwise digits , ; and space */
} TextField;

typedef struct {
    Simulator sim;

    Font font;
    bool fontLoadedFromFile;

    /* configuration form */
    TextField   fieldCacheSize, fieldBlockSize, fieldMemorySize, fieldWays;
    MappingType mapping;
    ReplacementPolicy policy;

    /* access form */
    TextField fieldAddress, fieldSequence;
    bool      sequenceDirty;     /* sequence text edited but not loaded yet */

    const TextField *focus;      /* field receiving keyboard input, or NULL */

    /* last access (drives the result panel and the highlighted table row) */
    bool         hasResult;
    AccessResult last;
    Explanation  explanation;
    double       lastAccessTime;

    char message[192];
    bool messageIsError;

    float tableScroll;
    float historyScroll;

    bool   autoRun;
    double nextAutoStep;
    int    cursor;               /* mouse cursor requested this frame */
} Gui;

bool gui_init(Gui *gui);
void gui_frame(Gui *gui);        /* handles input, updates state and draws one frame */
void gui_shutdown(Gui *gui);

#endif
