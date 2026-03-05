#include "canvas.h"
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

// 简单的双向链表实现 Undo/Redo
typedef struct action_node_s {
    draw_cmd_t cmd;
    struct action_node_s *prev;
    struct action_node_s *next;
} action_node_t;

typedef struct {
    action_node_t *head; // 最早的操作
    action_node_t *tail; // 最新的操作
    action_node_t *current; // 当前指针
    int count;
    int max_steps;
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

void undo_redo_push(const draw_cmd_t *cmd) {
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
    if (!g_history.head) return;
    
    // If current is NULL, it means we are at the state BEFORE the first action.
    // So we should not iterate anything (empty canvas).
    if (!g_history.current) return;

    action_node_t *node = g_history.head;
    while (node) {
        cb(&node->cmd, user_data);
        if (node == g_history.current) break; // 只遍历到当前状态
        node = node->next;
    }
}

char *undo_redo_serialize(void) {
    if (!g_history.head || !g_history.current) return strdup("[]");
    
    cJSON *root = cJSON_CreateArray();
    action_node_t *node = g_history.head;
    
    while (node) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "tool", node->cmd.tool);
        cJSON_AddNumberToObject(item, "color", node->cmd.color);
        cJSON_AddNumberToObject(item, "width", node->cmd.line_width);
        cJSON_AddNumberToObject(item, "count", node->cmd.point_count);
        
        cJSON *points = cJSON_CreateArray();
        if (node->cmd.point_count > 0 && node->cmd.points) {
            for (size_t i = 0; i < node->cmd.point_count; i++) {
                cJSON *p = cJSON_CreateObject();
                cJSON_AddNumberToObject(p, "x", node->cmd.points[i].x);
                cJSON_AddNumberToObject(p, "y", node->cmd.points[i].y);
                cJSON_AddItemToArray(points, p);
            }
        }
        cJSON_AddItemToObject(item, "points", points);
        cJSON_AddItemToArray(root, item);
        
        if (node == g_history.current) break;
        node = node->next;
    }
    
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

void undo_redo_deserialize(const char *json) {
    undo_redo_init(g_history.max_steps); // Clear existing
    
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        draw_cmd_t cmd;
        cJSON *tool = cJSON_GetObjectItem(item, "tool");
        cJSON *color = cJSON_GetObjectItem(item, "color");
        cJSON *width = cJSON_GetObjectItem(item, "width");
        cJSON *count = cJSON_GetObjectItem(item, "count");
        cJSON *points = cJSON_GetObjectItem(item, "points");
        
        if (tool) cmd.tool = (tool_type_t)tool->valueint;
        if (color) cmd.color = (uint32_t)color->valuedouble;
        if (width) cmd.line_width = width->valuedouble;
        if (count) cmd.point_count = (size_t)count->valueint;
        
        if (points && cJSON_IsArray(points)) {
            cmd.points = malloc(sizeof(point_t) * cmd.point_count);
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
            undo_redo_push(&cmd);
            free(cmd.points);
        }
    }
    cJSON_Delete(root);
}
