#ifndef UNDO_REDO_H
#define UNDO_REDO_H

#include "canvas.h"

#ifdef __cplusplus
extern "C" {
#endif

void undo_redo_init(int max_steps);
void undo_redo_start_action(tool_type_t tool, uint32_t color, double line_width);
void undo_redo_add_point(double x, double y);
void undo_redo_end_action(void);
void undo_redo_push(const draw_cmd_t *cmd);
const draw_cmd_t *undo_redo_undo(void);
const draw_cmd_t *undo_redo_redo(void);
void undo_redo_iterate(void (*cb)(const draw_cmd_t *, void *), void *user_data);
void undo_redo_iterate_range(int start_idx, int batch_size, void (*cb)(const draw_cmd_t *, void *), void *user_data);
char *undo_redo_serialize(void);
void undo_redo_deserialize(const char *json);
int canvas_get_command_count(void);

#ifdef __cplusplus
}
#endif

#endif
