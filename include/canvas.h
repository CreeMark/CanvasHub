#ifndef CANVAS_H
#define CANVAS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 图层ID类型
typedef int32_t layer_id_t;

// 画布结构体 (Opaque)
typedef struct canvas_s canvas_t;

// 绘图工具枚举
typedef enum {
    TOOL_PEN = 0,
    TOOL_ERASER,
    TOOL_LINE,
    TOOL_RECT,
    TOOL_CIRCLE
} tool_type_t;

// 点结构体
typedef struct point_s {
    double x;
    double y;
} point_t;

// 绘图命令结构体
typedef struct draw_cmd_s {
    tool_type_t tool;
    uint32_t color; // 0xAARRGGBB
    double line_width;
    point_t *points;
    size_t point_count;
    layer_id_t layer_id;
} draw_cmd_t;

// 创建画布
canvas_t *canvas_new(int width, int height);

// 销毁画布
void canvas_free(canvas_t *canvas);

// 执行绘图命令
int canvas_draw(canvas_t *canvas, const draw_cmd_t *cmd);

// 清空画布
void canvas_clear(canvas_t *canvas);

#ifdef __cplusplus
}
#endif

#endif // CANVAS_H
