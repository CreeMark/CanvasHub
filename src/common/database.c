#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <glib.h>
#include "database.h"
#include "bcrypt_wrapper.h"

// MySQL 8.0+ removed my_bool, use bool instead
#if MYSQL_VERSION_ID >= 80000
typedef bool my_bool;
#endif

// 连接池结构体
typedef struct {
    MYSQL *conn;
    int in_use;
} db_connection_t;

// 连接池
typedef struct {
    db_connection_t *connections;
    int size;
    sem_t semaphore;
    pthread_mutex_t mutex;
} db_pool_t;

static db_pool_t *g_db_pool = NULL;
static db_config_t saved_config;

// 连接池管理函数
static MYSQL *db_pool_get_connection(void) {
    if (!g_db_pool) return NULL;
    
    sem_wait(&g_db_pool->semaphore);
    pthread_mutex_lock(&g_db_pool->mutex);
    
    MYSQL *conn = NULL;
    for (int i = 0; i < g_db_pool->size; i++) {
        if (!g_db_pool->connections[i].in_use) {
            g_db_pool->connections[i].in_use = 1;
            conn = g_db_pool->connections[i].conn;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_db_pool->mutex);
    return conn;
}

static void db_pool_release_connection(MYSQL *conn) {
    if (!g_db_pool || !conn) return;
    
    pthread_mutex_lock(&g_db_pool->mutex);
    
    for (int i = 0; i < g_db_pool->size; i++) {
        if (g_db_pool->connections[i].conn == conn) {
            g_db_pool->connections[i].in_use = 0;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_db_pool->mutex);
    sem_post(&g_db_pool->semaphore);
}

static int db_reconnect(MYSQL *conn) {
    if (conn) {
        mysql_close(conn);
    }
    
    conn = mysql_init(NULL);
    if (conn == NULL) {
        fprintf(stderr, "mysql_init() failed during reconnect\n");
        return -1;
    }
    
    if (mysql_real_connect(conn, saved_config.host, saved_config.user, saved_config.password, 
                          saved_config.db_name, saved_config.port, NULL, 0) == NULL) {
        fprintf(stderr, "mysql_real_connect() failed during reconnect: %s\n", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }
    
    g_print("INFO: Database reconnected successfully\n");
    return 0;
}

int db_init(const db_config_t *config) {
    memcpy(&saved_config, config, sizeof(db_config_t));
    
    // 创建连接池
    int pool_size = 5; // 默认连接池大小
    g_db_pool = malloc(sizeof(db_pool_t));
    if (!g_db_pool) {
        fprintf(stderr, "Failed to allocate memory for connection pool\n");
        return -1;
    }
    
    g_db_pool->size = pool_size;
    g_db_pool->connections = malloc(sizeof(db_connection_t) * pool_size);
    if (!g_db_pool->connections) {
        fprintf(stderr, "Failed to allocate memory for connections\n");
        free(g_db_pool);
        return -1;
    }
    
    sem_init(&g_db_pool->semaphore, 0, pool_size);
    pthread_mutex_init(&g_db_pool->mutex, NULL);
    
    // 初始化连接
    for (int i = 0; i < pool_size; i++) {
        g_db_pool->connections[i].conn = mysql_init(NULL);
        if (!g_db_pool->connections[i].conn) {
            fprintf(stderr, "mysql_init() failed for connection %d\n", i);
            // 清理已创建的连接
            for (int j = 0; j < i; j++) {
                if (g_db_pool->connections[j].conn) {
                    mysql_close(g_db_pool->connections[j].conn);
                }
            }
            free(g_db_pool->connections);
            free(g_db_pool);
            return -1;
        }
        
        if (mysql_real_connect(g_db_pool->connections[i].conn, config->host, config->user, config->password, 
                              config->db_name, config->port, NULL, 0) == NULL) {
            fprintf(stderr, "mysql_real_connect() failed for connection %d: %s\n", i, mysql_error(g_db_pool->connections[i].conn));
            // 清理已创建的连接
            for (int j = 0; j <= i; j++) {
                if (g_db_pool->connections[j].conn) {
                    mysql_close(g_db_pool->connections[j].conn);
                }
            }
            free(g_db_pool->connections);
            free(g_db_pool);
            return -1;
        }
        
        g_db_pool->connections[i].in_use = 0;
    }
    
    g_print("INFO: Database connection pool initialized with %d connections\n", pool_size);
    return 0;
}

void db_close(void) {
    if (g_db_pool) {
        for (int i = 0; i < g_db_pool->size; i++) {
            if (g_db_pool->connections[i].conn) {
                mysql_close(g_db_pool->connections[i].conn);
            }
        }
        free(g_db_pool->connections);
        sem_destroy(&g_db_pool->semaphore);
        pthread_mutex_destroy(&g_db_pool->mutex);
        free(g_db_pool);
        g_db_pool = NULL;
        g_print("INFO: Database connection pool closed\n");
    }
}

uint32_t db_register_user(const char *username, const char *password, const char *email) {
    MYSQL *conn = db_pool_get_connection();
    if (!conn) {
        fprintf(stderr, "Failed to get database connection\n");
        return 0;
    }
    
    char hash[BCRYPT_HASHSIZE];
    if (bcrypt_newhash(password, BCRYPT_COST, hash) != 0) {
        fprintf(stderr, "Password hashing failed\n");
        db_pool_release_connection(conn);
        return 0;
    }
    
    // 使用prepared statement防注入
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) {
        fprintf(stderr, "mysql_stmt_init failed\n");
        db_pool_release_connection(conn);
        return 0;
    }
    
    const char *query = "INSERT INTO users (username, password_hash, email) VALUES (?, ?, ?)";
    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        fprintf(stderr, "mysql_stmt_prepare failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return 0;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char *)username;
    bind[0].buffer_length = strlen(username);
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = hash;
    bind[1].buffer_length = strlen(hash);
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char *)email;
    bind[2].buffer_length = strlen(email);
    
    if (mysql_stmt_bind_param(stmt, bind)) {
        fprintf(stderr, "mysql_stmt_bind_param failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return 0;
    }
    
    if (mysql_stmt_execute(stmt)) {
        fprintf(stderr, "Register failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return 0;
    }
    
    uint32_t id = (uint32_t)mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    db_pool_release_connection(conn);
    return id;
}

uint32_t db_login_user(const char *username, const char *password) {
    MYSQL *conn = db_pool_get_connection();
    if (!conn) {
        fprintf(stderr, "Failed to get database connection\n");
        return 0;
    }
    
    // 使用prepared statement防注入
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) {
        fprintf(stderr, "mysql_stmt_init failed\n");
        db_pool_release_connection(conn);
        return 0;
    }
    
    const char *query = "SELECT id, password_hash FROM users WHERE username=?";
    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        fprintf(stderr, "mysql_stmt_prepare failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return 0;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char *)username;
    bind[0].buffer_length = strlen(username);
    
    if (mysql_stmt_bind_param(stmt, bind)) {
        fprintf(stderr, "mysql_stmt_bind_param failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return 0;
    }
    
    if (mysql_stmt_execute(stmt)) {
        fprintf(stderr, "Login query failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return 0;
    }
    
    MYSQL_RES *result = mysql_stmt_result_metadata(stmt);
    if (!result) {
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return 0;
    }
    
    MYSQL_BIND result_bind[2];
    memset(result_bind, 0, sizeof(result_bind));
    
    uint32_t id = 0;
    char password_hash[BCRYPT_HASHSIZE];
    
    result_bind[0].buffer_type = MYSQL_TYPE_LONG;
    result_bind[0].buffer = &id;
    
    result_bind[1].buffer_type = MYSQL_TYPE_STRING;
    result_bind[1].buffer = password_hash;
    result_bind[1].buffer_length = BCRYPT_HASHSIZE;
    
    if (mysql_stmt_bind_result(stmt, result_bind)) {
        fprintf(stderr, "mysql_stmt_bind_result failed: %s\n", mysql_stmt_error(stmt));
        mysql_free_result(result);
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return 0;
    }
    
    if (mysql_stmt_fetch(stmt) == 0) {
        if (bcrypt_checkpass(password, password_hash) != 0) {
            id = 0;
        }
    } else {
        id = 0;
    }
    
    mysql_free_result(result);
    mysql_stmt_close(stmt);
    db_pool_release_connection(conn);
    return id;
}

uint32_t db_create_project(uint32_t user_id, const char *name, const char *desc) {
    MYSQL *conn = db_pool_get_connection();
    if (!conn) {
        fprintf(stderr, "Failed to get database connection\n");
        return 0;
    }
    
    // 使用prepared statement防注入
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) {
        fprintf(stderr, "mysql_stmt_init failed\n");
        db_pool_release_connection(conn);
        return 0;
    }
    
    const char *query = "INSERT INTO projects (owner_id, name, description, canvas_data) VALUES (?, ?, ?, '[]')";
    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        fprintf(stderr, "mysql_stmt_prepare failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return 0;
    }
    
    MYSQL_BIND bind[3];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &user_id;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char *)name;
    bind[1].buffer_length = strlen(name);
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char *)desc;
    bind[2].buffer_length = strlen(desc);
    
    if (mysql_stmt_bind_param(stmt, bind)) {
        fprintf(stderr, "mysql_stmt_bind_param failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return 0;
    }
    
    if (mysql_stmt_execute(stmt)) {
        fprintf(stderr, "Create project failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return 0;
    }
    
    uint32_t id = (uint32_t)mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    db_pool_release_connection(conn);
    return id;
}

project_t *db_get_projects(uint32_t user_id, int *count) {
    MYSQL *conn = db_pool_get_connection();
    if (!conn) {
        fprintf(stderr, "Failed to get database connection\n");
        *count = 0;
        return NULL;
    }
    
    // 使用prepared statement防注入
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) {
        fprintf(stderr, "mysql_stmt_init failed\n");
        *count = 0;
        db_pool_release_connection(conn);
        return NULL;
    }
    
    const char *query = "SELECT id, owner_id, name, description FROM projects ORDER BY created_at DESC LIMIT 50";
    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        fprintf(stderr, "mysql_stmt_prepare failed: %s\n", mysql_stmt_error(stmt));
        *count = 0;
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    if (mysql_stmt_execute(stmt)) {
        fprintf(stderr, "Get projects failed: %s\n", mysql_stmt_error(stmt));
        *count = 0;
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    MYSQL_RES *result = mysql_stmt_result_metadata(stmt);
    if (!result) {
        *count = 0;
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    // 执行查询获取结果
    if (mysql_stmt_store_result(stmt)) {
        fprintf(stderr, "mysql_stmt_store_result failed: %s\n", mysql_stmt_error(stmt));
        *count = 0;
        mysql_free_result(result);
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    int num_rows = mysql_stmt_num_rows(stmt);
    project_t *projects = malloc(sizeof(project_t) * num_rows);
    if (!projects) {
        *count = 0;
        mysql_free_result(result);
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    MYSQL_BIND result_bind[4];
    memset(result_bind, 0, sizeof(result_bind));
    
    uint32_t id, owner_id;
    char name[128], description[256];
    
    result_bind[0].buffer_type = MYSQL_TYPE_LONG;
    result_bind[0].buffer = &id;
    
    result_bind[1].buffer_type = MYSQL_TYPE_LONG;
    result_bind[1].buffer = &owner_id;
    
    result_bind[2].buffer_type = MYSQL_TYPE_STRING;
    result_bind[2].buffer = name;
    result_bind[2].buffer_length = sizeof(name);
    
    result_bind[3].buffer_type = MYSQL_TYPE_STRING;
    result_bind[3].buffer = description;
    result_bind[3].buffer_length = sizeof(description);
    
    if (mysql_stmt_bind_result(stmt, result_bind)) {
        fprintf(stderr, "mysql_stmt_bind_result failed: %s\n", mysql_stmt_error(stmt));
        *count = 0;
        free(projects);
        mysql_free_result(result);
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    int i = 0;
    while (mysql_stmt_fetch(stmt) == 0 && i < num_rows) {
        projects[i].id = id;
        projects[i].owner_id = owner_id;
        strncpy(projects[i].name, name, sizeof(projects[i].name)-1);
        strncpy(projects[i].description, description, sizeof(projects[i].description)-1);
        i++;
    }
    
    mysql_free_result(result);
    mysql_stmt_close(stmt);
    *count = num_rows;
    db_pool_release_connection(conn);
    return projects;
}

void db_free_project_list(project_t *projects) {
    if (projects) free(projects);
}

int db_save_project_data(uint32_t project_id, const char *data) {
    MYSQL *conn = db_pool_get_connection();
    if (!conn) {
        g_print("ERROR: Failed to get database connection\n");
        return -1;
    }
    
    size_t data_len = strlen(data);
    const char *canvas_data = data_len == 0 ? "[]" : data;
    
    // 使用prepared statement防注入
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) {
        g_print("ERROR: mysql_stmt_init failed\n");
        db_pool_release_connection(conn);
        return -1;
    }
    
    const char *query = "UPDATE projects SET canvas_data=? WHERE id=?";
    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        g_print("ERROR: mysql_stmt_prepare failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return -1;
    }
    
    MYSQL_BIND bind[2];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (char *)canvas_data;
    bind[0].buffer_length = strlen(canvas_data);
    
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &project_id;
    
    if (mysql_stmt_bind_param(stmt, bind)) {
        g_print("ERROR: mysql_stmt_bind_param failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return -1;
    }
    
    g_print("INFO: Executing SQL: UPDATE projects SET canvas_data='[%zu chars]' WHERE id=%u\n", 
           strlen(canvas_data), project_id);
    
    int ret = 0;
    if (mysql_stmt_execute(stmt)) {
        g_print("ERROR: Save canvas failed: %s (errno: %u)\n", mysql_stmt_error(stmt), mysql_stmt_errno(stmt));
        
        if (mysql_stmt_errno(stmt) == CR_SERVER_GONE_ERROR) {
            g_print("WARNING: MySQL server gone, attempting reconnect...\n");
            if (db_reconnect(conn) == 0) {
                if (mysql_stmt_execute(stmt)) {
                    g_print("ERROR: Save canvas failed after reconnect: %s\n", mysql_stmt_error(stmt));
                    ret = -1;
                } else {
                    g_print("INFO: Database update successful after reconnect for project %u\n", project_id);
                }
            } else {
                ret = -1;
            }
        } else {
            ret = -1;
        }
    } else {
        g_print("INFO: Database update successful for project %u\n", project_id);
    }
    
    mysql_stmt_close(stmt);
    db_pool_release_connection(conn);
    return ret;
}

char *db_get_project_data(uint32_t project_id) {
    MYSQL *conn = db_pool_get_connection();
    if (!conn) {
        fprintf(stderr, "Failed to get database connection\n");
        return strdup("[]");
    }
    
    g_print("INFO: db_get_project_data - querying project_id=%u\n", project_id);
    
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) {
        fprintf(stderr, "mysql_stmt_init failed\n");
        db_pool_release_connection(conn);
        return strdup("[]");
    }
    
    const char *query = "SELECT canvas_data FROM projects WHERE id=?";
    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        fprintf(stderr, "mysql_stmt_prepare failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return strdup("[]");
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &project_id;
    
    if (mysql_stmt_bind_param(stmt, bind)) {
        fprintf(stderr, "mysql_stmt_bind_param failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return strdup("[]");
    }
    
    if (mysql_stmt_execute(stmt)) {
        fprintf(stderr, "Get project data failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return strdup("[]");
    }
    
    if (mysql_stmt_store_result(stmt)) {
        fprintf(stderr, "mysql_stmt_store_result failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return strdup("[]");
    }
    
    my_ulonglong num_rows = mysql_stmt_num_rows(stmt);
    g_print("INFO: db_get_project_data - found %lu rows for project_id=%u\n", (unsigned long)num_rows, project_id);
    
    if (num_rows == 0) {
        g_print("WARNING: db_get_project_data - no row found for project_id=%u\n", project_id);
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return strdup("[]");
    }
    
    MYSQL_BIND result_bind[1];
    memset(result_bind, 0, sizeof(result_bind));
    
    unsigned long length = 0;
    my_bool is_null = 0;
    my_bool error_flag = 0;
    
    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = NULL;
    result_bind[0].buffer_length = 0;
    result_bind[0].length = &length;
    result_bind[0].is_null = &is_null;
    result_bind[0].error = &error_flag;
    
    if (mysql_stmt_bind_result(stmt, result_bind)) {
        fprintf(stderr, "mysql_stmt_bind_result failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return strdup("[]");
    }
    
    int fetch_result = mysql_stmt_fetch(stmt);
    g_print("INFO: db_get_project_data - fetch result=%d, is_null=%d, length=%lu, error=%d\n", 
           fetch_result, is_null, length, error_flag);
    
    char *data = NULL;
    
    if (fetch_result == 0 || fetch_result == MYSQL_DATA_TRUNCATED) {
        if (is_null) {
            g_print("INFO: db_get_project_data - canvas_data is NULL\n");
            data = strdup("[]");
        } else if (length == 0) {
            g_print("INFO: db_get_project_data - canvas_data is empty\n");
            data = strdup("[]");
        } else {
            g_print("INFO: db_get_project_data - allocating %lu bytes for data\n", length + 1);
            data = (char *)malloc(length + 1);
            if (data) {
                memset(data, 0, length + 1);
                
                MYSQL_BIND fetch_bind;
                memset(&fetch_bind, 0, sizeof(fetch_bind));
                fetch_bind.buffer_type = MYSQL_TYPE_STRING;
                fetch_bind.buffer = data;
                fetch_bind.buffer_length = length + 1;
                fetch_bind.length = &length;
                
                int col_result = mysql_stmt_fetch_column(stmt, &fetch_bind, 0, 0);
                if (col_result == 0) {
                    data[length] = '\0';
                    g_print("INFO: db_get_project_data - retrieved data length: %zu, first 100 chars: %.100s\n", 
                           strlen(data), data);
                } else {
                    g_print("ERROR: mysql_stmt_fetch_column failed with result %d: %s\n", 
                           col_result, mysql_stmt_error(stmt));
                    free(data);
                    data = strdup("[]");
                }
            } else {
                g_print("ERROR: malloc failed for canvas data\n");
                data = strdup("[]");
            }
        }
    } else if (fetch_result == MYSQL_NO_DATA) {
        g_print("WARNING: db_get_project_data - no data (MYSQL_NO_DATA)\n");
        data = strdup("[]");
    } else {
        g_print("WARNING: db_get_project_data - mysql_stmt_fetch failed with error: %s\n", 
               mysql_stmt_error(stmt));
        data = strdup("[]");
    }
    
    mysql_stmt_close(stmt);
    db_pool_release_connection(conn);
    return data;
}

int db_save_chat_message(uint32_t project_id, const char *sender, const char *content, uint64_t timestamp) {
    MYSQL *conn = db_pool_get_connection();
    if (!conn) {
        fprintf(stderr, "Failed to get database connection\n");
        return -1;
    }
    
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) {
        fprintf(stderr, "mysql_stmt_init failed\n");
        db_pool_release_connection(conn);
        return -1;
    }
    
    const char *query = "INSERT INTO chat_messages (project_id, sender, content, timestamp) VALUES (?, ?, ?, ?)";
    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        fprintf(stderr, "mysql_stmt_prepare failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return -1;
    }
    
    MYSQL_BIND bind[4];
    memset(bind, 0, sizeof(bind));
    
    unsigned long sender_len = strlen(sender);
    unsigned long content_len = strlen(content);
    
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &project_id;
    
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char *)sender;
    bind[1].buffer_length = sender_len;
    bind[1].length = &sender_len;
    
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char *)content;
    bind[2].buffer_length = content_len;
    bind[2].length = &content_len;
    
    bind[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind[3].buffer = &timestamp;
    bind[3].is_unsigned = 1;
    
    if (mysql_stmt_bind_param(stmt, bind)) {
        fprintf(stderr, "mysql_stmt_bind_param failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return -1;
    }
    
    if (mysql_stmt_execute(stmt)) {
        fprintf(stderr, "Failed to save chat message: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return -1;
    }
    
    mysql_stmt_close(stmt);
    db_pool_release_connection(conn);
    return 0;
}

chat_msg_db_t *db_get_chat_messages(uint32_t project_id, int *count, int limit) {
    *count = 0;
    
    MYSQL *conn = db_pool_get_connection();
    if (!conn) {
        fprintf(stderr, "Failed to get database connection\n");
        return NULL;
    }
    
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) {
        fprintf(stderr, "mysql_stmt_init failed\n");
        db_pool_release_connection(conn);
        return NULL;
    }
    
    char query[256];
    snprintf(query, sizeof(query), 
             "SELECT sender, content, timestamp FROM chat_messages WHERE project_id=? ORDER BY timestamp ASC LIMIT %d", 
             limit > 0 ? limit : 100);
    
    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        fprintf(stderr, "mysql_stmt_prepare failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    MYSQL_BIND bind[1];
    memset(bind, 0, sizeof(bind));
    
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &project_id;
    
    if (mysql_stmt_bind_param(stmt, bind)) {
        fprintf(stderr, "mysql_stmt_bind_param failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    if (mysql_stmt_execute(stmt)) {
        fprintf(stderr, "Failed to get chat messages: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    if (mysql_stmt_store_result(stmt)) {
        fprintf(stderr, "mysql_stmt_store_result failed: %s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    my_ulonglong num_rows = mysql_stmt_num_rows(stmt);
    if (num_rows <= 0) {
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    chat_msg_db_t *messages = malloc(sizeof(chat_msg_db_t) * num_rows);
    if (!messages) {
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    memset(messages, 0, sizeof(chat_msg_db_t) * num_rows);
    
    MYSQL_BIND result_bind[3];
    memset(result_bind, 0, sizeof(result_bind));
    
    unsigned long sender_len = 0;
    unsigned long content_len = 0;
    uint64_t timestamp_val = 0;
    
    result_bind[0].buffer_type = MYSQL_TYPE_STRING;
    result_bind[0].buffer = messages[0].sender;
    result_bind[0].buffer_length = sizeof(messages[0].sender) - 1;
    result_bind[0].length = &sender_len;
    
    result_bind[1].buffer_type = MYSQL_TYPE_STRING;
    result_bind[1].buffer = messages[0].content;
    result_bind[1].buffer_length = sizeof(messages[0].content) - 1;
    result_bind[1].length = &content_len;
    
    result_bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[2].buffer = &timestamp_val;
    result_bind[2].is_unsigned = 1;
    
    if (mysql_stmt_bind_result(stmt, result_bind)) {
        fprintf(stderr, "mysql_stmt_bind_result failed: %s\n", mysql_stmt_error(stmt));
        free(messages);
        mysql_stmt_close(stmt);
        db_pool_release_connection(conn);
        return NULL;
    }
    
    int i = 0;
    while (mysql_stmt_fetch(stmt) == 0 && i < (int)num_rows) {
        messages[i].sender[sizeof(messages[i].sender) - 1] = '\0';
        messages[i].content[sizeof(messages[i].content) - 1] = '\0';
        messages[i].timestamp = timestamp_val;
        i++;
        
        if (i < (int)num_rows) {
            result_bind[0].buffer = messages[i].sender;
            result_bind[1].buffer = messages[i].content;
            mysql_stmt_bind_result(stmt, result_bind);
        }
    }
    
    *count = i;
    
    mysql_stmt_close(stmt);
    db_pool_release_connection(conn);
    return messages;
}
