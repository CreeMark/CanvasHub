# 技术方案选型 (Technical Solution Selection) - 纯 C 实现

## 1. 核心库选型对比

| 模块 | 候选方案 A (推荐) | 候选方案 B | 候选方案 C | 推荐理由 |
| :--- | :--- | :--- | :--- | :--- |
| **语言标准** | **C17** | C11 | C99 | C17 修复了 C11 的缺陷，是目前最稳定的 C 标准。 |
| **GUI** | **GTK 3** | GTK 4 | Nuklear | GTK 3 成熟稳定，文档丰富，原生支持 C 语言。 |
| **图形渲染** | **Cairo** | Skia | OpenGL | Cairo 与 GTK 完美集成，适合 2D 矢量绘图。 |
| **WebSocket**| **libwebsockets** | mongoose | libsoup | `libwebsockets` 是轻量级、高性能的纯 C WebSocket 库。 |
| **JSON 解析**| **cJSON** | Jansson | jsmn | `cJSON` 极其轻量，单文件集成，适合纯 C 项目。 |
| **数据库** | **MySQL C API** | SQLite3 | libpq | 明确要求使用 MySQL 8.x，直接使用官方 C Connector。 |
| **单元测试** | **Unity** | CMocka | Check | Unity 极简，适合嵌入式思维的 C 开发测试。 |
| **构建系统** | **CMake** | Makefile | Meson | 跨平台构建的标准选择。 |

## 2. 架构设计 (模块化 C)
采用面向对象思想的 C 语言设计 (OOC) 和回调机制。

- **Core (核心层)**:
  - `canvas_t`: 画布数据结构 (图层链表、尺寸)。
  - `drawing_t`: 绘图操作抽象 (工具类型、坐标点集)。
  - `history_t`: 双向链表实现的撤销/重做栈。
  
- **Network (网络层)**:
  - `ws_client_t`: 封装 libwebsockets，处理连接状态。
  - `protocol_t`: 负责 JSON 序列化/反序列化。
  
- **Database (持久层)**:
  - `db_manager_t`: 封装 MySQL 连接池和 CRUD 操作。
  
- **GUI (表现层)**:
  - `main_window`: GTK 窗口布局。
  - `drawing_area`: 处理 Expose 事件，调用 Cairo 绘图。

## 3. 关键机制
### 3.1 内存管理
- 统一使用 `canvas_malloc` / `canvas_free` 宏，方便追踪泄漏。
- 关键结构体采用 `_new()` 和 `_free()` 配对函数。

### 3.2 渲染循环
- 使用 `g_timeout_add` 或 `g_idle_add` 驱动网络事件轮询。
- 收到网络绘图指令 -> 更新 `canvas_t` 数据 -> `gtk_widget_queue_draw()`。

### 3.3 协议格式 (JSON)
```json
{
  "type": "draw",
  "data": {
    "tool": 1, 
    "color": 0xFF0000,
    "width": 2.5,
    "points": [{"x": 10, "y": 20}, {"x": 12, "y": 22}]
  }
}
```
