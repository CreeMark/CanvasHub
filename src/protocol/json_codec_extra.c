#include "protocol.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

char *protocol_serialize_auth(const char *type, const auth_msg_t *msg) {
    if (!type || !msg) return NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddStringToObject(data, "username", msg->username);
    cJSON_AddStringToObject(data, "password", msg->password);
    if (strlen(msg->email) > 0) cJSON_AddStringToObject(data, "email", msg->email);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

int protocol_deserialize_auth(const char *json, auth_msg_t *out_msg) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) { cJSON_Delete(root); return -1; }
    cJSON *u = cJSON_GetObjectItem(data, "username");
    cJSON *p = cJSON_GetObjectItem(data, "password");
    cJSON *e = cJSON_GetObjectItem(data, "email");
    if (u && u->valuestring) strncpy(out_msg->username, u->valuestring, sizeof(out_msg->username)-1);
    if (p && p->valuestring) strncpy(out_msg->password, p->valuestring, sizeof(out_msg->password)-1);
    if (e && e->valuestring) strncpy(out_msg->email, e->valuestring, sizeof(out_msg->email)-1);
    cJSON_Delete(root);
    return 0;
}

char *protocol_serialize_room(const char *type, const room_msg_t *msg) {
    if (!type || !msg) return NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type);
    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddNumberToObject(data, "room_id", msg->room_id);
    cJSON_AddStringToObject(data, "name", msg->name);
    cJSON_AddStringToObject(data, "description", msg->description);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

int protocol_deserialize_room(const char *json, room_msg_t *out_msg) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) { cJSON_Delete(root); return -1; }
    cJSON *id = cJSON_GetObjectItem(data, "room_id");
    cJSON *name = cJSON_GetObjectItem(data, "name");
    cJSON *desc = cJSON_GetObjectItem(data, "description");
    if (id) out_msg->room_id = (uint32_t)id->valuedouble;
    if (name && name->valuestring) strncpy(out_msg->name, name->valuestring, sizeof(out_msg->name)-1);
    if (desc && desc->valuestring) strncpy(out_msg->description, desc->valuestring, sizeof(out_msg->description)-1);
    cJSON_Delete(root);
    return 0;
}

char *protocol_serialize_room_list(const room_list_msg_t *msg) {
    if (!msg) return NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "room_list");
    cJSON *data = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "data", data);
    for (uint32_t i = 0; i < msg->count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", msg->rooms[i].room_id);
        cJSON_AddStringToObject(item, "name", msg->rooms[i].name);
        cJSON_AddStringToObject(item, "desc", msg->rooms[i].description);
        cJSON_AddNumberToObject(item, "owner", msg->rooms[i].owner_id);
        cJSON_AddItemToArray(data, item);
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
