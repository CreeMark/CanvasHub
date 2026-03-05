#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "database.h"

static MYSQL *conn;

int db_init(const db_config_t *config) {
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
    if (!conn) return 0;
    char query[512];
    snprintf(query, sizeof(query), 
             "INSERT INTO users (username, password_hash, email) VALUES ('%s', '%s', '%s')",
             username, password, email);
    
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Register failed: %s\n", mysql_error(conn));
        return 0;
    }
    
    return (uint32_t)mysql_insert_id(conn);
}

uint32_t db_login_user(const char *username, const char *password) {
    if (!conn) return 0;
    char query[512];
    snprintf(query, sizeof(query), 
             "SELECT id FROM users WHERE username='%s' AND password_hash='%s'",
             username, password);
             
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Login query failed: %s\n", mysql_error(conn));
        return 0;
    }
    
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result) return 0;
    
    uint32_t id = 0;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row) {
        id = atoi(row[0]);
    }
    
    mysql_free_result(result);
    return id;
}

uint32_t db_create_project(uint32_t user_id, const char *name, const char *desc) {
    if (!conn) return 0;
    char query[1024];
    snprintf(query, sizeof(query),
             "INSERT INTO projects (owner_id, name, description, canvas_data) VALUES (%u, '%s', '%s', '')",
             user_id, name, desc);
             
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Create project failed: %s\n", mysql_error(conn));
        return 0;
    }
    
    return (uint32_t)mysql_insert_id(conn);
}

project_t *db_get_projects(uint32_t user_id, int *count) {
    if (!conn) { *count = 0; return NULL; }
    char query[512];
    // List all projects (public) or owned by user?
    // For simplicity, let's list all projects for now to allow collaboration.
    // Or projects where user is owner OR member.
    // Let's just list ALL projects to enable "Lobby" feel.
    snprintf(query, sizeof(query), "SELECT id, owner_id, name, description FROM projects ORDER BY created_at DESC LIMIT 50");
    
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Get projects failed: %s\n", mysql_error(conn));
        *count = 0;
        return NULL;
    }
    
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result) {
        *count = 0;
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
    return projects;
}

void db_free_project_list(project_t *projects) {
    if (projects) free(projects);
}

int db_save_project_data(uint32_t project_id, const char *data) {
    if (!conn) return -1;
    // Need to escape data string as it's JSON and might contain quotes
    char *escaped = malloc(strlen(data) * 2 + 1);
    mysql_real_escape_string(conn, escaped, data, strlen(data));
    
    char *query = malloc(strlen(escaped) + 256);
    sprintf(query, "UPDATE projects SET canvas_data='%s' WHERE id=%u", escaped, project_id);
    
    int ret = 0;
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Save canvas failed: %s\n", mysql_error(conn));
        ret = -1;
    }
    
    free(escaped);
    free(query);
    return ret;
}

char *db_get_project_data(uint32_t project_id) {
    if (!conn) return NULL;
    char query[128];
    snprintf(query, sizeof(query), "SELECT canvas_data FROM projects WHERE id=%u", project_id);
    
    if (mysql_query(conn, query)) {
        return NULL;
    }
    
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result) return NULL;
    
    char *data = NULL;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row && row[0]) {
        data = strdup(row[0]);
    }
    
    mysql_free_result(result);
    return data;
}
