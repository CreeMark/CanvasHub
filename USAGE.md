# Canvas 客户端使用说明

## 编译项目

```bash
make clean
make
```

## 运行方式

### 方式 1：默认本地连接（硬编码）

不传参数时，客户端默认连接 `127.0.0.1:8080`：

```bash
./build/bin/canvas_client
```

输出示例：
```
INFO: No arguments provided, using default server: 127.0.0.1:8080
INFO: Usage: ./build/bin/canvas_client [IP] [PORT]
INFO: Client initialized, will connect to 127.0.0.1:8080
INFO: Connecting to 127.0.0.1:8080...
INFO: Connected to server successfully
```

### 方式 2：指定服务器 IP

连接远程服务器（端口使用默认 8080）：

```bash
./build/bin/canvas_client 192.168.1.100
```

输出示例：
```
INFO: Server IP specified: 192.168.1.100
INFO: Client initialized, will connect to 192.168.1.100:8080
INFO: Connecting to 192.168.1.100:8080...
INFO: Connected to server successfully
```

### 方式 3：指定服务器 IP 和端口

完全自定义连接：

```bash
./build/bin/canvas_client 192.168.1.100 9000
```

输出示例：
```
INFO: Server IP specified: 192.168.1.100
INFO: Server port specified: 9000
INFO: Client initialized, will connect to 192.168.1.100:9000
INFO: Connecting to 192.168.1.100:9000...
INFO: Connected to server successfully
```

## 多机协作示例

### 1. 启动服务端（在服务器上）

```bash
# 在服务端机器上
./build/bin/canvas_server
```

输出：
```
Database initialized.
Server listening on port 8080
```

### 2. 启动客户端（在多台机器上）

**机器 A（本地）**：
```bash
./build/bin/canvas_client
# 或
./build/bin/canvas_client 127.0.0.1
```

**机器 B（远程）**：
```bash
# 假设服务器 IP 为 192.168.1.100
./build/bin/canvas_client 192.168.1.100
```

**机器 C（远程）**：
```bash
./build/bin/canvas_client 192.168.1.100
```

### 3. 协作流程

1. 所有客户端连接到同一台服务器
2. 用户 A 登录并创建房间
3. 用户 B 和用户 C 登录并加入同一房间
4. 所有用户可以在同一画布上实时协作绘画
5. 聊天消息实时同步

## 参数说明

| 参数位置 | 说明 | 默认值 | 示例 |
|---------|------|--------|------|
| argv[1] | 服务器 IP 地址 | 127.0.0.1 | 192.168.1.100 |
| argv[2] | 服务器端口 | 8080 | 9000 |

## 注意事项

1. **服务端绑定**：服务端默认绑定 `0.0.0.0:8080`，可以接受来自任何 IP 的连接
2. **防火墙**：确保服务器防火墙开放了 8080 端口
3. **网络可达**：确保客户端机器可以 ping 通服务器 IP
4. **端口范围**：端口号必须在 1-65535 之间，否则使用默认值 8080

## 错误处理

### 连接失败

```
INFO: Connecting to 192.168.1.100:8080...
ERROR: Connection failed: Connection timed out
```

**原因**：服务器未启动或防火墙阻止

**解决**：
- 检查服务端是否运行：`ps aux | grep canvas_server`
- 检查防火墙规则：`sudo ufw allow 8080/tcp`

### 无效 IP 地址

```
ERROR: Invalid server address: invalid.ip.format
```

**原因**：IP 地址格式不正确

**解决**：使用正确的 IPv4 地址格式，如 `192.168.1.100`

### 无效端口号

```
ERROR: Invalid port number: 99999, using default 8080
```

**原因**：端口号超出范围（>65535 或 <=0）

**解决**：使用有效的端口号，或省略该参数使用默认值 8080

## 代码修改说明

### 修改的文件

1. **src/client/gui_main.c**
   - 添加了命令行参数解析
   - 支持 `./canvas_client [IP] [PORT]` 格式

2. **src/client/net_client.c**
   - 在 `net_client_s` 结构体中添加 `server_ip` 和 `server_port` 字段
   - 修改 `net_client_new()` 函数接受 IP 和端口参数
   - 修改 `net_client_connect()` 使用配置的服务器地址

3. **include/network.h**
   - 更新 `net_client_new()` 函数声明

### 向后兼容

- 默认行为不变：不传参数时仍连接 `127.0.0.1:8080`
- 硬编码逻辑保留：默认值在代码中定义
