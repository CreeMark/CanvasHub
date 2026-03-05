#ifndef DATABASE_H
#define DATABASE_H

#include <mysql/mysql.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// 用户信息结构
typedef struct {
    uint32_t id;
    char username[64];
    char password_hash[128];
    char email[128];
    time_t created_at;
} user_t;

// 项目结构
typedef struct {
    uint32_t id;
    uint32_t owner_id;
    char name[128];
    char description[512];
    time_t updated_at;
} project_t;

// 数据库连接配置
typedef struct {
    char host[64];
    uint16_t port;
    char user[64];
    char password[64];
    char db_name[64];
} db_config_t;

// 初始化数据库连接
// 成功返回 0，失败返回 -1
int db_init(const db_config_t *config);

// 注册新用户
// 成功返回用户ID，失败返回 0
uint32_t db_register_user(const char *username, const char *password, const char *email);

// 用户登录
// 成功返回用户ID，失败返回 0
uint32_t db_login_user(const char *username, const char *password);

// 创建新项目
// 成功返回项目ID，失败返回 0
uint32_t db_create_project(uint32_t user_id, const char *name, const char *desc);

// 获取项目列表
// 返回项目数组，count 为项目数量
// 需调用 db_free_project_list 释放
project_t *db_get_projects(uint32_t user_id, int *count);

// 释放项目列表
void db_free_project_list(project_t *projects);

// 保存项目画布数据 (JSON string)
int db_save_project_data(uint32_t project_id, const char *data);

// 获取项目画布数据
// 返回动态分配的字符串，调用者需 free
char *db_get_project_data(uint32_t project_id);

// 关闭数据库连接
void db_close(void);

#ifdef __cplusplus
}
#endif

#endif // DATABASE_H
