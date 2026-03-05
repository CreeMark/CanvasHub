#include "protocol.h"
#include "cJSON.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *protocol_serialize_draw(const draw_msg_t *msg) {
    if (!msg) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "type", "draw");
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);

    cJSON_AddNumberToObject(data, "tool", msg->tool_type);
    cJSON_AddNumberToObject(data, "color", msg->color);
    cJSON_AddNumberToObject(data, "width", msg->width);

    cJSON *points_array = cJSON_CreateArray();
    cJSON_AddItemToObject(data, "points", points_array);

    for (size_t i = 0; i < msg->point_count; i++) {
        cJSON *point = cJSON_CreateObject();
        cJSON_AddNumberToObject(point, "x", msg->points[i].x);
        cJSON_AddNumberToObject(point, "y", msg->points[i].y);
        cJSON_AddItemToArray(points_array, point);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_str;
}

int protocol_deserialize_draw(const char *json, draw_msg_t *out_msg) {
    if (!json || !out_msg) return -1;

    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || (strcmp(type->valuestring, "draw") != 0)) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *tool = cJSON_GetObjectItemCaseSensitive(data, "tool");
    cJSON *color = cJSON_GetObjectItemCaseSensitive(data, "color");
    cJSON *width = cJSON_GetObjectItemCaseSensitive(data, "width");
    cJSON *points = cJSON_GetObjectItemCaseSensitive(data, "points");

    if (!cJSON_IsNumber(tool) || !cJSON_IsNumber(color) || 
        !cJSON_IsNumber(width) || !cJSON_IsArray(points)) {
        cJSON_Delete(root);
        return -1;
    }

    out_msg->tool_type = (int)tool->valuedouble;
    out_msg->color = (uint32_t)color->valuedouble;
    out_msg->width = width->valuedouble;
    out_msg->point_count = cJSON_GetArraySize(points);
    
    if (out_msg->point_count > 0) {
        out_msg->points = malloc(out_msg->point_count * sizeof(*out_msg->points)); 
        
        if (!out_msg->points) {
            cJSON_Delete(root);
            return -1;
        }

        int i = 0;
        cJSON *pt = NULL;
        cJSON_ArrayForEach(pt, points) {
            cJSON *x = cJSON_GetObjectItemCaseSensitive(pt, "x");
            cJSON *y = cJSON_GetObjectItemCaseSensitive(pt, "y");
            if (cJSON_IsNumber(x) && cJSON_IsNumber(y)) {
                out_msg->points[i].x = x->valuedouble;
                out_msg->points[i].y = y->valuedouble;
            } else {
                out_msg->points[i].x = 0;
                out_msg->points[i].y = 0;
            }
            i++;
        }
    } else {
        out_msg->points = NULL;
    }

    cJSON_Delete(root);
    return 0;
}

char *protocol_serialize_chat(const chat_msg_t *msg) {
    if (!msg) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "type", "chat");
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);

    cJSON_AddStringToObject(data, "sender", msg->sender);
    cJSON_AddStringToObject(data, "content", msg->content);
    cJSON_AddNumberToObject(data, "timestamp", (double)msg->timestamp);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_str;
}

int protocol_deserialize_chat(const char *json, chat_msg_t *out_msg) {
    if (!json || !out_msg) return -1;

    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type) || (strcmp(type->valuestring, "chat") != 0)) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *sender = cJSON_GetObjectItemCaseSensitive(data, "sender");
    cJSON *content = cJSON_GetObjectItemCaseSensitive(data, "content");
    cJSON *ts = cJSON_GetObjectItemCaseSensitive(data, "timestamp");

    if (cJSON_IsString(sender)) strncpy(out_msg->sender, sender->valuestring, sizeof(out_msg->sender)-1);
    if (cJSON_IsString(content)) strncpy(out_msg->content, content->valuestring, sizeof(out_msg->content)-1);
    if (cJSON_IsNumber(ts)) out_msg->timestamp = (uint64_t)ts->valuedouble;

    cJSON_Delete(root);
    return 0;
}

char *protocol_serialize_cmd(const char *type) {
    if (!type) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "type", type);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_str;
}

void protocol_free_draw_msg(draw_msg_t *msg) {
    if (msg) {
        if (msg->points) {
            free(msg->points);
            msg->points = NULL;
        }
        msg->point_count = 0;
    }
}
