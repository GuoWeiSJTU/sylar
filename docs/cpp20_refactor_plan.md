# Sylar C++20 重构计划

## 1. 目的与边界

本计划将 Sylar 从 C++11 演进为以 C++20 为最低语言标准的网络服务器框架，并建立基于 `co_await` 的显式异步 I/O 模型。

本计划分为两个独立目标，必须按顺序完成：

1. **兼容迁移**：公共协议 API 在 C++20 下稳定构建、链接并通过回归测试。
2. **架构重构**：使用 C++20 coroutine 运行时驱动 Socket、HTTP、WebSocket、Rock 调用链，淘汰栈式上下文和系统调用拦截路径。

不在本计划第一阶段的范围内：C++ Modules、全仓 ranges 改写、强制使用 `std::format`、替换 HTTP 解析器、替换 ORM/数据库协议。这些工作不直接解决异步模型和可维护性问题，待核心迁移稳定后再单独评估。

## 2. 当前基线与结论

### 2.1 已确认现状

- `CMakeLists.txt` 将 `-std=c++11` 写入全局编译参数，且最低 CMake 版本为 3.0。
- 当前 CMake 4.3 已不再直接支持最低版本为 3.0 的项目配置，因此构建系统现代化是阻塞项。
- 核心运行时已统一为 `Task/Executor/Reactor/TimerQueue`（epoll）。
- 定时器以整数毫秒和系统时间实现；线程和同步原语主要封装 POSIX API。
- 配置、HTTP Session 等仍使用 `boost::lexical_cast` 和 `boost::any`。
- 测试是独立可执行程序，尚未完整注册为 CTest。

### 2.2 已完成的兼容性审计

在当前工作树上，使用 GCC 16.1 并让最终生效标准为 C++20，已成功编译以下代表性对象：

- Thread、Timer、Task、Executor、Reactor；
- ByteArray、Socket、Stream、TcpServer；
- HTTP 请求解析、连接、会话和服务器；
- Config、日志、MySQL、OpenSSL 加密和散列工具。

这仅证明代表性源文件可在 C++20 下编译，**不等同于完整库链接成功或所有测试通过**。完整构建和测试是第 0 阶段的必经验收项。

## 3. 目标架构

### 3.1 当前执行记录（2026-08）

- 阶段 0：已完成 CTest 注册、单位/集成/慢测试标签划分和 C++20 全量库构建基线；Fiber 兼容回归和依赖硬编码路径的测试单独标记为 `legacy/environment` 并默认禁用，外部服务测试仍需相应服务和配置。
- 阶段 1：已完成目标级 CMake C++20 配置、导入依赖目标、C11 C 源文件隔离、模板/ORM 生成项目同步到 C++20 和 `-Werror` 构建。
- 阶段 2：已完成标准同步原语、`std::atomic_ref`、`std::any`、无 Boost 数值转换、`steady_clock` TimerManager、`span` 视图重载。
- 阶段 3：已加入 `Task/Result/Executor/Reactor/TimerQueue`，Reactor 使用 epoll one-shot、fd generation、超时状态和 exactly-once 恢复；`AsyncSocket` 支持 `steady_clock` deadline 与 `stop_token`；`tests/test_coroutine.cc` 覆盖 Task、pipe、超时、取消、socketpair 和 HTTP 解析。
- 阶段 4：已完成 `AsyncSocket`、`AsyncHttpSession`、WebSocket 帧会话和 Rock 会话，所有 I/O 均可显式传递 deadline 与取消令牌。
- 阶段 5：核心运行时迁移已完成。`TcpServer`、`HttpServer`、`WSServer`、`RockServer`、应用入口、echo 示例和协议测试均由 coroutine runtime 驱动；旧栈式运行时、旧异步流和全局 I/O 拦截代码已删除。Redis、模块/名称服务、服务发现等未纳入当前 CMake 构建的可选扩展仍保留原 API，待单独确定兼容策略后迁移或移除。

本记录与当前提交同步维护；核心构建目标不再保留旧运行时兼容分支。未纳入构建目标的可选扩展不计入核心服务运行时验收，不能在未完成迁移前宣称其已获得 C++20 coroutine 语义。

当前验收范围：C++20 全量库和示例构建成功，9 个 `unit` CTest 通过；`test_coroutine` 覆盖 Task、Reactor、TimerQueue、AsyncSocket、HTTP、WebSocket 和 Rock 帧/消息路径。网络监听集成测试保留为 `integration` 并在无网络权限的环境中禁用。ASan/UBSan 的 `test_coroutine` 回归通过，ABI 主版本说明已纳入本次发布记录。

迁移期间新旧路径并存：

```text
同步兼容 API ──> Socket/Stream（显式阻塞语义）

异步 API     ──> Task<T> + co_await ──> Executor ──> EpollReactor
                                                    ├── TimerQueue
                                                    └── Cancellation
```

新路径的原则：

- 不在 Reactor 锁内恢复协程；事件只把 `std::coroutine_handle<>` 投递给 `Executor`。
- I/O 成功、超时、取消和 fd 关闭必须通过原子状态机竞争，且只能有一个路径恢复协程。
- 异步 API 显式传递 `std::stop_token`、超时或 deadline；不得依赖 `LD_PRELOAD` 风格 hook 隐式挂起。
- 所有跨 `co_await` 保存的 Buffer 必须拥有内存；`std::span` 和 `std::string_view` 仅作短生命周期视图。
- 新公开 API 不使用 C++23 的 `std::expected`；使用项目内 `Result<T>`（值或 `std::error_code`）。
- 服务端配置统一使用 `async_runtime: coroutine`；旧值直接拒绝，避免运行时双轨。
- 当前 coroutine TCP 传输只接受明文监听；`ssl: 1` 会在绑定阶段明确失败，TLS 应通过独立的非阻塞 TLS adapter 接入，禁止静默降级为明文。

建议新增目录：

```text
sylar/coroutine/
  task.h             # Task<T>、promise_type、异常传播
  result.h           # Result<T> 与错误码
  executor.h/.cc     # 就绪队列、线程池、停止控制
  reactor.h/.cc      # epoll 注册、事件分发、fd generation
  timer_queue.h/.cc  # steady_clock 定时器
  async_fd.h/.cc     # read/write/connect 等 awaitable
```

## 4. 分阶段执行计划

### 阶段 0：冻结基线并建立可重复验证

**目标**：任何重构之前，先得到可比较的行为、性能和测试基线。

任务：

1. 在干净提交上记录编译器、依赖版本、构建参数和运行配置。
2. 执行全部可独立运行的 `bin/test_*`；依赖 MySQL、ZooKeeper、Redis、端口或配置的测试单独标记为集成测试。
3. 增加 CTest 注册，区分 `unit` 与 `integration` 标签。
4. 为 echo、HTTP keep-alive、WebSocket、Rock 建立压测脚本或固定压测记录，至少保存吞吐、P99 延迟、RSS 和 CPU。
5. 开启 AddressSanitizer/UndefinedBehaviorSanitizer 独立构建；TSan 作为单独任务执行。

验收：可从全新构建目录完成配置、构建、单位测试；基线日志和压测数据已归档。

### 阶段 1：构建系统与 C++20 兼容迁移

**目标**：删除全局 C++11 约束，完整库以 C++20 构建，同时不改变运行时行为。

任务：

1. 将 CMake 最低版本提升到 3.20，声明 `project(... LANGUAGES C CXX)`。
2. 移除全局 `CMAKE_CXX_FLAGS`、`include_directories`、`link_directories` 的核心依赖；改为目标级 API。
3. 使用 `target_compile_features(sylar PUBLIC cxx_std_20)` 和 `CXX_EXTENSIONS OFF`。
4. C 源文件 `sylar/ds/roaring.c` 保持 C11，不和 C++ 标准混用。
5. 使用 `Threads::Threads`、`OpenSSL::SSL`、`OpenSSL::Crypto`、`ZLIB::ZLIB` 等导入目标；公共头暴露的第三方依赖必须在 `PUBLIC` 接口中表达。
6. 保持 `-Werror`，但将 Ragel/Protobuf 生成文件和第三方 C 文件的例外限制到相应源文件。
7. 改造测试辅助函数：测试可执行文件自动 `add_test()`；外部服务相关测试增加标签并默认不在单位测试集合执行。
8. CI 同时运行 GCC 12+ 和 Clang 16+ 的 C++20 构建。

验收：

```sh
cmake -S . -B build/cpp20 -DBUILD_TESTING=ON
cmake --build build/cpp20 -j4
ctest --test-dir build/cpp20 --output-on-failure
```

完整共享库链接成功，所有无外部服务依赖的测试通过。

### 阶段 2：基础类型与 API 现代化

**目标**：降低 C++11 遗留 API 的维护成本，但不触碰 Fiber 调度语义。

按以下顺序拆分提交：

1. `typedef` 改为 `using`；将 `ptr(new T)` 改为 `std::make_shared<T>`。
2. 将简单 `std::bind` 改为 lambda，复杂绑定保持不变并附生命周期测试。
3. `boost::any` 改为 `std::any`；仅数值解析改用 `std::from_chars`，YAML 泛型转换保留现有实现。
4. 将 `volatile + __sync_*` 原子内建改为明确的 `std::atomic<T>` 和内存序。
5. 将 Timer 和超时参数从 `uint64_t` 毫秒逐步改为 `std::chrono::milliseconds`；定时器内部采用 `std::chrono::steady_clock`。
6. 为 `ByteArray`、Socket、HTTP 解析增加 `std::span<std::byte>` 与 `std::string_view` 重载，旧接口继续保留一个发布周期。
7. 线程、锁和信号量逐步采用 `std::jthread`、`std::mutex`、`std::shared_mutex`、`std::counting_semaphore`；Linux 线程命名通过 `native_handle()` 保留。
8. 对关键返回值增加 `[[nodiscard]]`，对不抛异常的析构、访问器补充 `noexcept`。

验收：旧 API 无源代码兼容性破坏；新 API 有单元测试；性能不低于阶段 0 基线的 95%。

### 阶段 3：建立 C++20 coroutine 运行时

**目标**：不修改现有 HTTP 服务路径，先实现一个可独立测试的 `Task + Executor + EpollReactor`。

核心接口草案：

```cpp
template<class T>
class Task;

template<class T>
class Result;

class Executor {
public:
    void schedule(std::coroutine_handle<> handle);
    void requestStop();
};

class AsyncSocket {
public:
    Task<Result<std::size_t>> read(
        std::span<std::byte> buffer,
        std::chrono::steady_clock::time_point deadline,
        std::stop_token stop = {});
};
```

任务：

1. 实现 move-only `Task<T>`、异常传播、析构时 frame 所有权和 detach 策略。
2. 实现 Executor 的 MPSC 就绪队列、线程启动和有序停止。
3. 实现 EpollReactor 的 fd 注册/删除/事件唤醒；每次注册分配 generation，防止 fd 复用导致 ABA 问题。
4. 实现 TimerQueue；到期、取消、I/O 成功均通过同一等待操作状态机完成。
5. 先实现 `async_wait_readable`、`async_wait_writable`，再实现 `connect/read/write/accept`。
6. 为每一种竞争场景编写测试：事件对超时、事件对取消、close 对事件、停止时仍有挂起操作。

验收：在不启用 `hook.cc` 的情况下，协程 echo 服务可稳定处理并发连接；ASan/UBSan 无报错；每个等待操作只恢复一次。

### 阶段 4：迁移网络与 HTTP 调用链

**目标**：将新服务流量迁移到显式异步 API，保留旧路径作为回退。

迁移顺序：

1. `AsyncSocket` 与 `SocketStream`；
2. `HttpSession`、`HttpConnection`；
3. `TcpServer`、`HttpServer`；
4. WebSocket；
5. Rock 协议、服务发现与数据库异步接口。

目标形式：

```cpp
Task<void> HttpSession::serve(std::stop_token stop) {
    while(!stop.stop_requested()) {
        auto request = co_await readRequest(stop);
        if(!request) {
            co_return;
        }
        auto response = co_await dispatch(*request, stop);
        co_await sendResponse(response, stop);
    }
}
```

迁移期间配置保留 `async_runtime` 字段以便识别旧配置；运行时只接受 `async_runtime: coroutine`，旧值会被拒绝，不再提供双运行时切换。

验收：HTTP keep-alive、半关闭、断连、超时、取消和限流场景均通过；新旧路径协议行为一致；新路径压测满足阶段 0 设定的性能目标。

### 阶段 5：淘汰遗留运行时（核心范围已完成）

**前提**：所有生产协议和示例均已迁移；本地构建、单元回归和 Sanitizer 已通过。

任务：

1. 删除栈式上下文运行时及其公开头文件和调度路径。
2. 删除对 `sleep/read/write/connect` 等函数的全局拦截。
3. 删除仅服务旧运行时的 fd 状态、异步流、测试和配置依赖。
4. 将项目版本和服务器标识提升到 2.0，并在本节记录迁移说明。

验收：核心源码和构建目标中不再有旧运行时或全局 I/O hook；所有核心服务由 coroutine runtime 驱动。未纳入构建的 Redis、模块/名称服务、服务发现扩展必须在单独迁移或移除后，才能扩大本阶段的验收范围。

本次验证命令：

```sh
CC=/usr/bin/gcc CXX=/usr/bin/g++ cmake -S . -B /tmp/sylar-cpp20-safe -DBUILD_TEST=ON
cmake --build /tmp/sylar-cpp20-safe -j4
ctest --test-dir /tmp/sylar-cpp20-safe -L unit --output-on-failure --timeout 20
cmake --build /tmp/sylar-cpp20-asan --target test_coroutine -j4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/sylar-cpp20-asan/bin/test_coroutine
```

## 5. 关键文件映射

| 现有模块 | 迁移目标 | 处理原则 |
| --- | --- | --- |
| `sylar/coroutine/task.*` | Task/Result | move-only frame、异常和错误码传播 |
| `sylar/coroutine/executor.*` | Executor | coroutine handle 就绪队列和有序停止 |
| `sylar/coroutine/reactor.*` | EpollReactor | one-shot、fd generation 和等待状态机 |
| `sylar/coroutine/timer_queue.*` | TimerQueue | `steady_clock` + deadline |
| `sylar/net/async_socket.*` | AsyncSocket | 显式 read/write/connect/accept 超时和取消 |
| `sylar/mutex.*`、`thread.*` | 标准同步原语 | 分批替换，保留线程名支持 |
| `sylar/bytearray.*` | span Buffer API | 先增加视图接口，后弃用旧接口 |
| `sylar/socket.*`、`stream.*` | `AsyncSocket` | 显式超时、取消与错误结果 |
| `sylar/http/*` | `Task<Result<T>>` API | 从连接层向服务器层自底向上迁移 |
| `sylar/config.*`、`session_data.*` | 标准库类型 | `std::any`、`from_chars`、chrono |

## 6. 风险与控制措施

| 风险 | 控制措施 |
| --- | --- |
| C++20 coroutine 与旧同步 API 语义不同 | 协议层统一使用 AsyncSession，并保留同步 Socket 作为独立 API |
| 超时、取消和 I/O 同时触发 | 等待操作状态机 + exactly-once resume 测试 |
| fd 关闭后被系统复用 | fd generation 校验 |
| `span/string_view` 跨挂起悬垂 | 跨 `co_await` 使用拥有型 Buffer 或 shared ownership |
| `jthread` 在自身线程析构导致自 join | 由外部生命周期控制器统一停止和 join |
| Sanitizer 对异步生命周期敏感 | 新路径在 ASan/UBSan 配置下运行 coroutine 单元和协议测试 |
| 外部依赖升级引入 API 变化 | CMake 中使用导入目标；依赖升级独立提交与 CI 矩阵 |
| 新旧协议行为不一致 | 协议测试向两条路径复用，使用特性开关灰度 |

## 7. 提交与发布策略

### 7.1 代码托管与协作仓库

后续 C++20 重构的代码以以下 Fork 为唯一远程协作仓库：

```text
https://github.com/GuoWeiSJTU/sylar.git
```

所有重构提交、分支和 Pull Request 均推送至该仓库；不得将重构提交推送至上游原始仓库。建议将其配置为 `origin`，并以 `main`（或该仓库当前默认分支）作为集成分支。每个阶段或独立可回滚任务使用单独主题分支，例如：

```text
refactor/cmake-cpp20
refactor/coroutine-runtime
refactor/http-coroutine
```

提交必须小而可回滚，建议顺序：

1. `build: modernize cmake and enable c++20`
2. `test: register unit tests with ctest`
3. `refactor: adopt chrono and standard synchronization`
4. `refactor: replace boost any and numeric conversions`
5. `add: coroutine task and executor`
6. `add: epoll coroutine reactor`
7. `add: async socket and stream`
8. `migrate: http server to coroutine runtime`
9. `remove: legacy runtime and syscall hooks`

每个提交必须：

- 通过对应阶段的构建和测试；
- 不混入无关格式化或生成文件；
- 在改变公开 API 时更新示例与迁移说明；
- 在改变可观察服务行为时提供日志、测试或压测证据。

## 8. 完成定义

### C++20 兼容迁移完成

- 不再存在强制 `-std=c++11`；
- GCC 与 Clang 的 C++20 构建、链接成功；
- 单位测试全部通过，集成测试具备明确前置条件；
- CMake、安装和依赖接口均为目标级配置。

### 完整重构完成

- HTTP、WebSocket、Rock 等生产协议默认使用 coroutine runtime；
- 所有 I/O 操作支持 deadline 与取消，且无双重恢复；
- 新旧运行时切换开关已移除；
- 核心构建目标中的旧栈式运行时和系统调用 hook 已删除；
- Sanitizer、协议回归和性能门禁命令已记录，可接入 CI；
- ABI 主版本升级为 2.0，并保留本迁移文档作为升级说明。
