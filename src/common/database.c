#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "database.h"
#include "bcrypt_wrapper.h"

static MYSQL *conn;
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
static db_config_t saved_config;

static int db_reconnect(void) {
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
        conn = NULL;
        return -1;
    }
    
    g_print("INFO: Database reconnected successfully\n");
    return 0;
}

int db_init(const db_config_t *config) {
    memcpy(&saved_config, config, sizeof(db_config_t));
    
    conn = mysql_init(NULL);
    if (conn == NULL) {
        fprintf(stderr, "mysql_init() failed\n");
        return -1;
    }
    
    if (mysql_real_connect(conn, config->host, config->user, config->password, 
                          config->db_name, config->port, NULL, 0) == NULL) {
        fprintf(stderr, "mysql_real_connect() failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }
    
    return 0;
}

void db_close(void) {
    if (conn) {
        mysql_close(conn);
        conn = NULL;
    }
}

uint32_t db_register_user(const char *username, const char *password, const char *email) {
    pthread_mutex_lock(&db_mutex);
    
    if (!conn) {
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    
    char hash[BCRYPT_HASHSIZE];
    if (bcrypt_newhash(password, BCRYPT_COST, hash) != 0) {
        fprintf(stderr, "Password hashing failed\n");
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    
    char *escaped_user = malloc(strlen(username) * 2 + 1);
    char *escaped_hash = malloc(strlen(hash) * 2 + 1);
    char *escaped_email = malloc(strlen(email) * 2 + 1);
    
    if (!escaped_user || !escaped_hash || !escaped_email) {
        free(escaped_user);
        free(escaped_hash);
        free(escaped_email);
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    
    mysql_real_escape_string(conn, escaped_user, username, strlen(username));
    mysql_real_escape_string(conn, escaped_hash, hash, strlen(hash));
    mysql_real_escape_string(conn, escaped_email, email, strlen(email));
    
    char query[1024];
    snprintf(query, sizeof(query), 
             "INSERT INTO users (username, password_hash, email) VALUES ('%s', '%s', '%s')",
             escaped_user, escaped_hash, escaped_email);
    
    free(escaped_user);
    free(escaped_hash);
    free(escaped_email);
    
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Register failed: %s\n", mysql_error(conn));
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    
    uint32_t id = (uint32_t)mysql_insert_id(conn);
    pthread_mutex_unlock(&db_mutex);
    return id;
}

uint32_t db_login_user(const char *username, const char *password) {
    pthread_mutex_lock(&db_mutex);
    
    if (!conn) {
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    
    char *escaped_user = malloc(strlen(username) * 2 + 1);
    if (!escaped_user) {
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    
    mysql_real_escape_string(conn, escaped_user, username, strlen(username));
    
    char query[512];
    snprintf(query, sizeof(query), 
             "SELECT id, password_hash FROM users WHERE username='%s'",
             escaped_user);
    
    free(escaped_user);
             
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Login query failed: %s\n", mysql_error(conn));
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result) {
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    
    uint32_t id = 0;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row && row[0] && row[1]) {
        if (bcrypt_checkpass(password, row[1]) == 0) {
            id = atoi(row[0]);
        }
    }
    
    mysql_free_result(result);
    pthread_mutex_unlock(&db_mutex);
    return id;
}

uint32_t db_create_project(uint32_t user_id, const char *name, const char *desc) {
    pthread_mutex_lock(&db_mutex);
    
    if (!conn) {
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    
    char query[1024];
    snprintf(query, sizeof(query),
             "INSERT INTO projects (owner_id, name, description, canvas_data) VALUES (%u, '%s', '%s', '')",
             user_id, name, desc);
             
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Create project failed: %s\n", mysql_error(conn));
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }
    
    uint32_t id = (uint32_t)mysql_insert_id(conn);
    pthread_mutex_unlock(&db_mutex);
    return id;
}

project_t *db_get_projects(uint32_t user_id, int *count) {
    pthread_mutex_lock(&db_mutex);
    
    if (!conn) { 
        *count = 0; 
        pthread_mutex_unlock(&db_mutex);
        return NULL; 
    }
    
    char query[512];
    snprintf(query, sizeof(query), "SELECT id, owner_id, name, description FROM projects ORDER BY created_at DESC LIMIT 50");
    
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Get projects failed: %s\n", mysql_error(conn));
        *count = 0;
        pthread_mutex_unlock(&db_mutex);
        return NULL;
    }
    
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result) {
        *count = 0;
        pthread_mutex_unlock(&db_mutex);
        return NULL;
    }
    
    int num_rows = mysql_num_rows(result);
    project_t *projects = malloc(sizeof(project_t) * num_rows);
    
    int i = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        projects[i].id = atoi(row[0]);
        projects[i].owner_id = atoi(row[1]);
        strncpy(projects[i].name, row[2], sizeof(projects[i].name)-1);
        if(row[3]) strncpy(projects[i].description, row[3], sizeof(projects[i].description)-1);
        else projects[i].description[0] = '\0';
        i++;
    }
    
    mysql_free_result(result);
    *count = num_rows;
    pthread_mutex_unlock(&db_mutex);
    return projects;
}

void db_free_project_list(project_t *projects) {
    if (projects) free(projects);
}

int db_save_project_data(uint32_t project_id, const char *data) {
    pthread_mutex_lock(&db_mutex);
    
    if (!conn) {
        if (db_reconnect() != 0) {
            g_print("ERROR: No database connection and reconnect failed\n");
            pthread_mutex_unlock(&db_mutex);
            return -1;
        }
    }
    
    size_t data_len = strlen(data);
    if (data_len == 0) {
        char query[128];
        snprintf(query, sizeof(query), "UPDATE projects SET canvas_data='[]' WHERE id=%u", project_id);
        int ret = mysql_query(conn, query) ? -1 : 0;
        if (ret != 0 && mysql_errno(conn) == CR_SERVER_GONE_ERROR) {
            g_print("WARNING: MySQL server gone, attempting reconnect...\n");
            if (db_reconnect() == 0) {
                ret = mysql_query(conn, query) ? -1 : 0;
            }
        }
        pthread_mutex_unlock(&db_mutex);
        return ret;
    }
    
    char *escaped = malloc(data_len * 2 + 1);
    if (!escaped) {
        g_print("ERROR: Out of memory for escaping\n");
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }
    
    unsigned long escaped_len = mysql_real_escape_string(conn, escaped, data, data_len);
    if (escaped_len == (unsigned long)-1) {
        g_print("ERROR: mysql_real_escape_string failed: %s\n", mysql_error(conn));
        free(escaped);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }
    
    char query[escaped_len + 256];
    snprintf(query, escaped_len + 256, 
             "UPDATE projects SET canvas_data='%s' WHERE id=%u", 
             escaped, project_id);
    
    g_print("INFO: Executing SQL: UPDATE projects SET canvas_data='[%zu chars]' WHERE id=%u\n", 
           escaped_len, project_id);
    
    int ret = 0;
    if (mysql_query(conn, query)) {
        g_print("ERROR: Save canvas failed: %s (errno: %u)\n", mysql_error(conn), mysql_errno(conn));
        
        if (mysql_errno(conn) == CR_SERVER_GONE_ERROR) {
            g_print("WARNING: MySQL server gone, attempting reconnect...\n");
            if (db_reconnect() == 0) {
                if (mysql_query(conn, query)) {
                    g_print("ERROR: Save canvas failed after reconnect: %s\n", mysql_error(conn));
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
    
    free(escaped);
    pthread_mutex_unlock(&db_mutex);
    return ret;
}

char *db_get_project_data(uint32_t project_id) {
    pthread_mutex_lock(&db_mutex);
    
    if (!conn) {
        pthread_mutex_unlock(&db_mutex);
        return strdup("[]");
    }
    
    char query[128];
    snprintf(query, sizeof(query), "SELECT canvas_data FROM projects WHERE id=%u", project_id);
    
    if (mysql_query(conn, query)) {
        pthread_mutex_unlock(&db_mutex);
        return strdup("[]");
    }
    
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result) {
        pthread_mutex_unlock(&db_mutex);
        return strdup("[]");
    }
    
    char *data = NULL;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row && row[0]) {
        data = strdup(row[0]);
    } else {
        data = strdup("[]");
    }
    
    mysql_free_result(result);
    pthread_mutex_unlock(&db_mutex);
    return data;
}
