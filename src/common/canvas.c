#include "canvas.h"
#include <stdlib.h>
#include <string.h>

struct canvas_s {
    int width;
    int height;
    // For now, we don't store layers or pixels in core if we rely on GUI surface
    // But to support full undo/redo repaint, we need to store the history of commands
    // The history is already managed by undo_redo.c
    // This core canvas struct might be used to maintain state if we separate rendering from data
    void *user_data;
};

canvas_t *canvas_new(int width, int height) {
    canvas_t *c = malloc(sizeof(canvas_t));
    if (c) {
        c->width = width;
        c->height = height;
        c->user_data = NULL;
    }
    return c;
}

void canvas_free(canvas_t *canvas) {
    if (canvas) {
        free(canvas);
    }
}

// In a real implementation, this would update the internal pixel buffer
// For this GTK client, we draw directly to Cairo surface in GUI code
int canvas_draw(canvas_t *canvas, const draw_cmd_t *cmd) {
    (void)canvas;
    (void)cmd;
    return 0;
}

void canvas_clear(canvas_t *canvas) {
    (void)canvas;
}
