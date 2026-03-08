# Canvas - 多用户实时协作绘画应用

一个基于 C 语言开发的支持多用户实时协作的绘画与聊天应用。

## 功能特性

- **用户系统**: 注册、登录认证
- **绘画功能**: 自由画笔、橡皮擦、撤销/重做、画布清理
- **协作功能**: 多用户实时同步绘画、实时聊天室
- **项目管理**: 创建/加入房间、项目列表管理
- **数据持久化**: MySQL 数据库存储用户信息和画布状态
- **本地存储**: 支持画布数据本地保存/加载

## 技术栈

| 组件 | 技术 |
|------|------|
| 语言 | C (C17) |
| GUI | GTK 3 |
| 图形库 | Cairo |
| 网络 | WebSocket (自定义实现) |
| 数据交互 | cJSON |
| 数据库 | MySQL 8.x |
| 构建系统 | Make |

## 项目结构

```
Canvas/
├── deps/              # 第三方依赖
│   └── cJSON/         # JSON 解析库
├── include/           # 头文件
├── src/
│   ├── client/        # 客户端代码 (GUI + 网络客户端)
│   ├── server/        # 服务端代码
│   ├── common/        # 公共代码 (画布、数据库、撤销重做)
│   └── protocol/      # 协议编解码
├── tests/             # 单元测试
├── Makefile           # 构建脚本
└── schema.sql         # 数据库架构
```

## 快速开始

### 环境要求

- GCC (支持 C17)
- GTK 3 开发库
- MySQL 8.x 开发库
- Make

### Linux (Ubuntu) 安装依赖

```bash
sudo apt update
sudo apt install libgtk-3-dev libmysqlclient-dev build-essential
```

### 编译

```bash
make clean && make
```

### 配置数据库

```bash
# 登录 MySQL
mysql -u root -p

# 执行数据库脚本
source schema.sql
```

### 运行

```bash
# 启动服务端
./build/bin/canvas_server

# 启动客户端 (另一个终端)
# 方式 1: 默认连接本地服务器 (127.0.0.1:8080)
./build/bin/canvas_client

# 方式 2: 连接远程服务器
./build/bin/canvas_client 192.168.1.100

# 方式 3: 指定服务器 IP 和端口
./build/bin/canvas_client 192.168.1.100 9000
```

## 使用说明

1. **注册/登录**: 首次使用需要注册账号，然后登录
2. **选择房间**: 在大厅中选择或创建一个房间
3. **绘画**: 使用工具栏选择画笔/橡皮擦，在画布上绘制
4. **协作**: 其他用户加入同一房间后可实时看到绘画内容
5. **保存**: 点击 Save 按钮将画布保存到服务器

## 多机协作

客户端支持连接远程服务器，实现多机实时协作：

### 启动服务端

在服务端机器上启动：

```bash
./build/bin/canvas_server
```

服务端默认绑定 `0.0.0.0:8080`，可接受来自任何 IP 的连接。

### 启动客户端

在多台客户端机器上分别运行：

```bash
# 本地机器
./build/bin/canvas_client

# 远程机器（假设服务器 IP 为 192.168.1.100）
./build/bin/canvas_client 192.168.1.100
```

所有客户端连接到同一服务器后，可以：
- 加入同一房间进行实时协作绘画
- 实时聊天交流
- 共享画布状态

## 开发指南

### 代码风格

项目遵循 Linux Kernel Coding Style，使用 `.clang-format` 配置文件。

### 命令行参数

客户端支持命令行参数指定服务器地址：

```bash
# 格式
./build/bin/canvas_client [IP] [PORT]

# 示例
./build/bin/canvas_client                    # 默认 127.0.0.1:8080
./build/bin/canvas_client 192.168.1.100      # 指定 IP
./build/bin/canvas_client 192.168.1.100 9000 # 指定 IP 和端口
```

### 运行测试

```bash
make test
```

### 调试模式

编译时已包含 `-g` 标志，可使用 GDB 调试：

```bash
gdb ./build/bin/canvas_server
gdb ./build/bin/canvas_client
```

## 协议说明

客户端与服务端通过 WebSocket 通信，消息格式为 JSON：

```json
{
  "type": "message_type",
  "data": { ... }
}
```

主要消息类型：
- `login` / `login_resp`: 登录
- `register` / `register_resp`: 注册
- `list_rooms` / `room_list`: 房间列表
- `join_room` / `join_room_resp`: 加入房间
- `load_canvas`: 加载画布数据
- `draw`: 绘画同步
- `chat`: 聊天消息
- `save_canvas` / `save_canvas_resp`: 保存画布

## 许可证

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE) 文件。
