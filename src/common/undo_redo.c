#include "canvas.h"
#include "undo_redo.h"
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "cJSON.h"

typedef struct action_node_s {
    draw_cmd_t cmd;
    struct action_node_s *prev;
    struct action_node_s *next;
} action_node_t;

typedef struct {
    action_node_t *head;
    action_node_t *tail;
    action_node_t *current;
    int count;
    int max_steps;
    draw_cmd_t *current_action;
    size_t current_point_count;
    size_t current_capacity;
} history_t;

static history_t g_history = {0};

void undo_redo_init(int max_steps) {
    // Clear existing
    if (g_history.head) {
        action_node_t *node = g_history.head;
        while (node) {
            action_node_t *next = node->next;
            if (node->cmd.points) free(node->cmd.points);
            free(node);
            node = next;
        }
    }
    memset(&g_history, 0, sizeof(g_history));
    g_history.max_steps = max_steps;
}

// 开始一个新的操作
void undo_redo_start_action(tool_type_t tool, uint32_t color, double line_width) {
    // 清理之前可能未完成的操作
    if (g_history.current_action) {
        free(g_history.current_action->points);
        free(g_history.current_action);
    }
    
    g_history.current_action = malloc(sizeof(draw_cmd_t));
    if (g_history.current_action) {
        g_history.current_action->tool = tool;
        g_history.current_action->color = color;
        g_history.current_action->line_width = line_width;
        g_history.current_action->point_count = 0;
        g_history.current_action->points = NULL;
        g_history.current_action->layer_id = 0;
        g_history.current_point_count = 0;
        g_history.current_capacity = 16; // 初始容量
        g_history.current_action->points = malloc(sizeof(point_t) * g_history.current_capacity);
    }
}

// 添加点到当前操作
void undo_redo_add_point(double x, double y) {
    if (!g_history.current_action) return;
    
    // 扩容
    if (g_history.current_point_count >= g_history.current_capacity) {
        g_history.current_capacity *= 2;
        g_history.current_action->points = realloc(g_history.current_action->points, sizeof(point_t) * g_history.current_capacity);
    }
    
    if (g_history.current_action->points) {
        g_history.current_action->points[g_history.current_point_count].x = x;
        g_history.current_action->points[g_history.current_point_count].y = y;
        g_history.current_point_count++;
        g_history.current_action->point_count = g_history.current_point_count;
    }
}

// 结束当前操作并添加到历史记录
void undo_redo_end_action() {
    if (!g_history.current_action || g_history.current_point_count < 2) {
        // 操作点太少，不添加到历史
        if (g_history.current_action) {
            free(g_history.current_action->points);
            free(g_history.current_action);
        }
        g_history.current_action = NULL;
        g_history.current_point_count = 0;
        g_history.current_capacity = 0;
        return;
    }
    
    // 如果当前不是在末尾，删除 current 之后的所有节点
    if (g_history.current != g_history.tail) {
        action_node_t *node = g_history.current ? g_history.current->next : g_history.head;
        
        while (node) {
            action_node_t *next = node->next;
            if (node->cmd.points) free(node->cmd.points);
            free(node);
            node = next;
            g_history.count--;
        }
        if (g_history.current) {
            g_history.current->next = NULL;
            g_history.tail = g_history.current;
        } else {
            g_history.head = NULL;
            g_history.tail = NULL;
        }
    }

    // 创建新节点
    action_node_t *new_node = malloc(sizeof(action_node_t));
    new_node->cmd = *g_history.current_action;
    // 深拷贝 points
    if (g_history.current_action->point_count > 0 && g_history.current_action->points) {
        new_node->cmd.points = malloc(sizeof(point_t) * g_history.current_action->point_count);
        if (new_node->cmd.points) {
            memcpy(new_node->cmd.points, g_history.current_action->points, sizeof(point_t) * g_history.current_action->point_count);
        }
    } else {
        new_node->cmd.points = NULL;
    }
    new_node->next = NULL;
    new_node->prev = g_history.tail;

    if (g_history.tail) {
        g_history.tail->next = new_node;
    } else {
        g_history.head = new_node;
    }
    g_history.tail = new_node;
    g_history.current = new_node;
    g_history.count++;

    // 限制步数
    if (g_history.count > g_history.max_steps) {
        action_node_t *old_head = g_history.head;
        if (old_head) {
            g_history.head = old_head->next;
            if (g_history.head) g_history.head->prev = NULL;
            
            // Critical fix: Update tail/current if they point to the removed head
            if (g_history.tail == old_head) g_history.tail = NULL;
            if (g_history.current == old_head) g_history.current = NULL;
            
            if (old_head->cmd.points) free(old_head->cmd.points);
            free(old_head);
            g_history.count--;
        }
    }
    
    // 清理当前操作
    free(g_history.current_action->points);
    free(g_history.current_action);
    g_history.current_action = NULL;
    g_history.current_point_count = 0;
    g_history.current_capacity = 0;
}

void undo_redo_push(const draw_cmd_t *cmd) {
    // 兼容旧接口，用于非绘制操作（如clear）
    // 如果当前不是在末尾，删除 current 之后的所有节点
    if (g_history.current != g_history.tail) {
        action_node_t *node = g_history.current ? g_history.current->next : g_history.head;
        
        while (node) {
            action_node_t *next = node->next;
            if (node->cmd.points) free(node->cmd.points);
            free(node);
            node = next;
            g_history.count--;
        }
        if (g_history.current) {
            g_history.current->next = NULL;
            g_history.tail = g_history.current;
        } else {
            g_history.head = NULL;
            g_history.tail = NULL;
        }
    }

    // 创建新节点
    action_node_t *new_node = malloc(sizeof(action_node_t));
    new_node->cmd = *cmd;
    // 深拷贝 points
    if (cmd->point_count > 0 && cmd->points) {
        new_node->cmd.points = malloc(sizeof(point_t) * cmd->point_count);
        if (new_node->cmd.points) {
            memcpy(new_node->cmd.points, cmd->points, sizeof(point_t) * cmd->point_count);
        }
    } else {
        new_node->cmd.points = NULL;
    }
    new_node->next = NULL;
    new_node->prev = g_history.tail;

    if (g_history.tail) {
        g_history.tail->next = new_node;
    } else {
        g_history.head = new_node;
    }
    g_history.tail = new_node;
    g_history.current = new_node;
    g_history.count++;

    // 限制步数
    if (g_history.count > g_history.max_steps) {
        action_node_t *old_head = g_history.head;
        if (old_head) {
            g_history.head = old_head->next;
            if (g_history.head) g_history.head->prev = NULL;
            
            // Critical fix: Update tail/current if they point to the removed head
            if (g_history.tail == old_head) g_history.tail = NULL;
            if (g_history.current == old_head) g_history.current = NULL;
            
            if (old_head->cmd.points) free(old_head->cmd.points);
            free(old_head);
            g_history.count--;
        }
    }
}

const draw_cmd_t *undo_redo_undo(void) {
    if (!g_history.current) return NULL;
    
    // Undo 意味着指针前移
    // current 指向的是"最后一次执行的操作"
    // undo 后，current 应该指向前一个操作
    // 界面应该重绘到 current（undo后）的状态
    // BUG FIX: undo returns the command that was just undone (to update external state if needed)
    // BUT for repaint_all, we need the state AFTER undo.
    // The caller usually calls undo_redo_undo(), then repaint_all().
    // repaint_all iterates from head to current.
    
    const draw_cmd_t *cmd = &g_history.current->cmd;
    g_history.current = g_history.current->prev;
    return cmd; 
}

const draw_cmd_t *undo_redo_redo(void) {
    if (g_history.current == g_history.tail) return NULL;
    
    if (g_history.current) {
        g_history.current = g_history.current->next;
    } else {
        g_history.current = g_history.head;
    }
    return &g_history.current->cmd;
}

void undo_redo_iterate(void (*cb)(const draw_cmd_t *, void *), void *user_data) {
    g_print("DEBUG: undo_redo_iterate - head=%p, current=%p, tail=%p\n",
           (void*)g_history.head, (void*)g_history.current, (void*)g_history.tail);
    if (!g_history.head) {
        g_print("WARNING: undo_redo_iterate - head is NULL\n");
        return;
    }
    
    // If current is NULL, it means we are at the state BEFORE the first action.
    // So we should not iterate anything (empty canvas).
    if (!g_history.current) {
        g_print("WARNING: undo_redo_iterate - current is NULL\n");
        return;
    }

    action_node_t *node = g_history.head;
    int count = 0;
    while (node) {
        count++;
        cb(&node->cmd, user_data);
        if (node == g_history.current) break; // 只遍历到当前状态
        node = node->next;
    }
    g_print("DEBUG: undo_redo_iterate - iterated %d commands\n", count);
}

// 分批迭代函数，从start_idx开始，最多遍历batch_size个命令
void undo_redo_iterate_range(int start_idx, int batch_size, void (*cb)(const draw_cmd_t *, void *), void *user_data) {
    g_print("DEBUG: undo_redo_iterate_range - start_idx=%d, batch_size=%d\n", start_idx, batch_size);
    if (!g_history.head) {
        g_print("WARNING: undo_redo_iterate_range - head is NULL\n");
        return;
    }
    
    // If current is NULL, it means we are at the state BEFORE the first action.
    // So we should not iterate anything (empty canvas).
    if (!g_history.current) {
        g_print("WARNING: undo_redo_iterate_range - current is NULL\n");
        return;
    }

    action_node_t *node = g_history.head;
    int count = 0;
    int current_idx = 0;
    
    // 找到起始位置
    while (node && current_idx < start_idx) {
        if (node == g_history.current) break;
        node = node->next;
        current_idx++;
    }
    
    // 遍历batch_size个命令
    while (node && count < batch_size) {
        count++;
        cb(&node->cmd, user_data);
        if (node == g_history.current) break; // 只遍历到当前状态
        node = node->next;
    }
    
    g_print("DEBUG: undo_redo_iterate_range - iterated %d commands\n", count);
}

char *undo_redo_serialize(void) {
    // 1. 增强的空历史检查
    if (!g_history.head) {
        // 历史为空，返回空数组
        return strdup("[]");
    }
    
    // 2. 如果current为NULL，表示在第一个操作之前的状态
    // 也应该返回空数组
    if (!g_history.current) {
        return strdup("[]");
    }
    
    // 3. 创建JSON数组
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        g_print("ERROR: Failed to create JSON array\n");
        return strdup("[]");
    }
    
    // 4. 遍历历史节点
    action_node_t *node = g_history.head;
    int command_count = 0;
    
    while (node) {
        // 安全检查每个命令
        if (node->cmd.point_count > 0 && !node->cmd.points) {
            g_print("WARNING: Command has point_count=%zu but points is NULL\n", 
                   node->cmd.point_count);
            // 跳过这个无效命令
            node = node->next;
            continue;
        }
        
        // 创建命令对象
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            g_print("ERROR: Failed to create command object\n");
            break;
        }
        
        // 添加基本属性
        cJSON_AddNumberToObject(item, "tool", node->cmd.tool);
        cJSON_AddNumberToObject(item, "color", node->cmd.color);
        cJSON_AddNumberToObject(item, "width", node->cmd.line_width);
        cJSON_AddNumberToObject(item, "count", (double)node->cmd.point_count);
        
        // 创建点数组
        cJSON *points = cJSON_CreateArray();
        if (!points) {
            g_print("ERROR: Failed to create points array\n");
            cJSON_Delete(item);
            break;
        }
        
        // 安全添加点坐标
        if (node->cmd.point_count > 0 && node->cmd.points) {
            for (size_t i = 0; i < node->cmd.point_count; i++) {
                cJSON *p = cJSON_CreateObject();
                if (!p) {
                    g_print("ERROR: Failed to create point object\n");
                    break;
                }
                cJSON_AddNumberToObject(p, "x", node->cmd.points[i].x);
                cJSON_AddNumberToObject(p, "y", node->cmd.points[i].y);
                cJSON_AddItemToArray(points, p);
            }
        }
        
        cJSON_AddItemToObject(item, "points", points);
        cJSON_AddItemToArray(root, item);
        
        command_count++;
        
        // 如果到达当前指针，停止遍历
        if (node == g_history.current) {
            break;
        }
        
        // 移动到下一个节点
        node = node->next;
    }
    
    // 5. 生成JSON字符串
    char *out = cJSON_PrintUnformatted(root);
    if (!out) {
        g_print("ERROR: Failed to print JSON (cJSON_PrintUnformatted returned NULL)\n");
        cJSON_Delete(root);
        return strdup("[]");
    }
    
    g_print("INFO: Serialized %d commands, JSON length: %zu\n", 
           command_count, strlen(out));
    
    cJSON_Delete(root);
    return out;
}

void undo_redo_deserialize(const char *json) {
    g_print("DEBUG: undo_redo_deserialize called with JSON length: %zu\n", strlen(json));
    
    // FIX: Ensure max_steps is at least 50 to prevent immediate deletion of pushed commands
    int steps = g_history.max_steps > 0 ? g_history.max_steps : 50;
    undo_redo_init(steps); 
    
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        g_print("ERROR: undo_redo_deserialize failed to parse JSON: %s\n", cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "unknown error");
        g_print("DEBUG: JSON content (first 100 chars): %.100s\n", json);
        return;
    }
    
    if (!cJSON_IsArray(root)) {
        g_print("ERROR: undo_redo_deserialize JSON root is not an array\n");
        cJSON_Delete(root);
        return;
    }

    int loaded_count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        draw_cmd_t cmd = {0}; // Initialize to zero
        cJSON *tool = cJSON_GetObjectItem(item, "tool");
        cJSON *color = cJSON_GetObjectItem(item, "color");
        cJSON *width = cJSON_GetObjectItem(item, "width");
        cJSON *count = cJSON_GetObjectItem(item, "count");
        cJSON *points = cJSON_GetObjectItem(item, "points");
        
        if (tool) cmd.tool = (tool_type_t)tool->valueint;
        if (color) cmd.color = (uint32_t)color->valuedouble;
        if (width) cmd.line_width = width->valuedouble;
        if (count && cJSON_IsNumber(count)) {
            cmd.point_count = (size_t)count->valuedouble;
        } else {
            g_print("WARNING: count field missing or not a number\n");
            cmd.point_count = 0;
        }
        
        g_print("DEBUG: Parsing command: tool=%d, color=%u, width=%.1f, count=%zu, points=%p\n", 
               cmd.tool, cmd.color, cmd.line_width, cmd.point_count, (void*)points);
        
        if (points && cJSON_IsArray(points)) {
            cmd.points = malloc(sizeof(point_t) * cmd.point_count);
            if (cmd.points) {
                int i = 0;
                cJSON *p = NULL;
                cJSON_ArrayForEach(p, points) {
                    if (i >= cmd.point_count) break;
                    cJSON *x = cJSON_GetObjectItem(p, "x");
                    cJSON *y = cJSON_GetObjectItem(p, "y");
                    if (x) cmd.points[i].x = x->valuedouble;
                    if (y) cmd.points[i].y = y->valuedouble;
                    i++;
                }
                cmd.layer_id = 0;
                g_print("DEBUG: Before push - head=%p, current=%p, tail=%p\n", 
                       (void*)g_history.head, (void*)g_history.current, (void*)g_history.tail);
                undo_redo_push(&cmd);
                g_print("DEBUG: After push - head=%p, current=%p, tail=%p, count=%d\n", 
                       (void*)g_history.head, (void*)g_history.current, (void*)g_history.tail, g_history.count);
                free(cmd.points);
                loaded_count++;
            } else {
                g_print("ERROR: Failed to allocate memory for points in deserialize\n");
            }
        } else {
            // Attempt to handle legacy flat format (single point per command)
            cJSON *x = cJSON_GetObjectItem(item, "x");
            cJSON *y = cJSON_GetObjectItem(item, "y");
            if (x && y) {
                // Legacy format detected
                if (!tool) {
                     cJSON *type_legacy = cJSON_GetObjectItem(item, "type");
                     if (type_legacy) cmd.tool = (tool_type_t)type_legacy->valueint;
                }
                if (!width) {
                     cJSON *size_legacy = cJSON_GetObjectItem(item, "size");
                     if (size_legacy) cmd.line_width = size_legacy->valuedouble;
                }
                
                // If color was not found by "color" key (unlikely as it matches), we keep default 0
                
                cmd.point_count = 1;
                cmd.points = malloc(sizeof(point_t));
                if (cmd.points) {
                    cmd.points[0].x = x->valuedouble;
                    cmd.points[0].y = y->valuedouble;
                    cmd.layer_id = 0;
                    undo_redo_push(&cmd);
                    free(cmd.points);
                    loaded_count++;
                }
            } else {
                g_print("WARNING: Skipping command without valid points array or legacy coordinates\n");
            }
        }
    }
    g_print("INFO: Successfully loaded %d commands from JSON\n", loaded_count);
    g_print("DEBUG: Final state - head=%p, current=%p, tail=%p, count=%d\n", 
           (void*)g_history.head, (void*)g_history.current, (void*)g_history.tail, g_history.count);
    // Set current to tail so canvas_get_command_count works correctly
    g_history.current = g_history.tail;
    g_print("DEBUG: After setting current - head=%p, current=%p, tail=%p\n", 
           (void*)g_history.head, (void*)g_history.current, (void*)g_history.tail);
    
    // Verify that history is correctly set up
    if (loaded_count > 0 && (!g_history.head || !g_history.current)) {
        g_print("ERROR: History is not correctly set up after deserialization!\n");
    }
    
    cJSON_Delete(root);
}

int canvas_get_command_count(void) {
    g_print("DEBUG: canvas_get_command_count - head=%p, current=%p, tail=%p, count=%d\n",
           (void*)g_history.head, (void*)g_history.current, (void*)g_history.tail, g_history.count);
    if (!g_history.head || !g_history.current) {
        g_print("WARNING: head or current is NULL\n");
        return 0;
    }
    
    int count = 0;
    action_node_t *node = g_history.head;
    while (node) {
        count++;
        if (node == g_history.current) {
            break;
        }
        node = node->next;
    }
    g_print("DEBUG: canvas_get_command_count returning %d\n", count);
    return count;
}

int canvas_has_history(void) {
    return (g_history.head != NULL && g_history.current != NULL);
}