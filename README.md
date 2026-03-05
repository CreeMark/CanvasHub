# Canvas Refactoring Project

## 项目章程 (Project Charter)

### 1. 项目目标
重构整个 Canvas 工程，实现一个支持多用户实时协作的绘画与聊天应用。

### 2. 核心功能需求
- **基础功能**: 文件保存/打开 (自定义格式/标准格式)。
- **用户系统**: 注册、登录、权限管理。
- **绘画功能**: 自由画笔、橡皮擦、撤销/重做、画布清理。
- **协作功能**: 多用户实时同步绘画、实时聊天室。
- **项目管理**: 简单的项目列表管理。
- **数据存储**: 数据库设计 (用户信息、项目元数据)。

### 3. 技术栈
- **语言**: C (C11/C17)
- **GUI**: GTK 3 (C API)
- **图形库**: Cairo
- **网络**: libwebsockets (C)
- **数据交互**: cJSON
- **数据库**: MySQL 8.x C API
- **构建系统**: CMake
- **平台**: PC (Linux/Windows)

### 4. 验收标准
- 代码符合 **Linux Kernel Coding Style** 或 **MISRA-C 2012**。
- 单元测试覆盖率 > 90% (使用 Unity/CMocka)。
- 多用户绘画延迟 < 100ms。
- 内存泄漏为 0 (Valgrind 检测)。

## 项目进度 (Gantt Chart)

```mermaid
gantt
    title Canvas Refactoring Roadmap
    dateFormat  YYYY-MM-DD
    section Phase 1: Init & Design
    项目初始化 & 需求分析       :done, task0, 2026-03-03, 1d
    技术方案选型               :done, task1, after task0, 1d
    模块拆分 & 接口定义         :done, task2, after task1, 1d
    
    section Phase 2: Core Dev
    编码规范 & Repo配置         :active, task3, after task2, 1d
    核心模块TDD (Protocol/DB)  :task4, after task3, 4d
    GUI 框架搭建               :task5, after task4, 3d
    
    section Phase 3: Features
    多用户网络同步              :task6, after task5, 5d
    绘画工具实现                :task7, after task6, 4d
    撤销/重做 & 聊天            :task8, after task7, 3d
    
    section Phase 4: Delivery
    集成测试 & 优化             :task9, after task8, 3d
    交付 & 复盘                 :task10, after task9, 2d
```

## 当前状态
- **Task-1**: 需求澄清 & 技术方案选型
