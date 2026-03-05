#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 网络事件回调函数类型
typedef void (*net_callback_t)(const void *data, size_t len, void *user_data);

// 网络客户端上下文 (Opaque struct)
typedef struct net_client_s net_client_t;

// 初始化网络客户端
net_client_t *net_client_new(const char *url);

// 设置回调函数
void net_client_set_callbacks(net_client_t *client, 
                              net_callback_t on_connect,
                              net_callback_t on_disconnect,
                              net_callback_t on_message,
                              void *user_data);

// 连接服务器
int net_client_connect(net_client_t *client);

// 发送数据
int net_client_send(net_client_t *client, const void *data, size_t len);

// 处理网络事件循环（需在主循环中调用）
void net_client_service(net_client_t *client);

// 断开连接并销毁客户端
void net_client_free(net_client_t *client);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_H
