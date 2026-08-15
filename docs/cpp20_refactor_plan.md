# Sylar C++20 重构计划

## 1. 目的与边界

本计划将 Sylar 从 C++11 演进为以 C++20 为最低语言标准的网络服务器框架，并在保持现有服务可用的前提下，逐步建立基于 `co_await` 的显式异步 I/O 模型。

本计划分为两个独立目标，必须按顺序完成：

1. **兼容迁移**：现有代码、行为和 Fiber 调度模型保持不变，但能稳定以 C++20 构建、链接并通过回归测试。
2. **架构重构**：新增 C++20 coroutine 运行时，将 Socket、HTTP、WebSocket、Rock 等调用链逐步迁移到显式异步 API，最终淘汰 `ucontext` 和系统调用 hook 路径。

不在本计划第一阶段的范围内：C++ Modules、全仓 ranges 改写、强制使用 `std::format`、替换 HTTP 解析器、替换 ORM/数据库协议。这些工作不直接解决异步模型和可维护性问题，待核心迁移稳定后再单独评估。

## 2. 当前基线与结论

### 2.1 已确认现状

- `CMakeLists.txt` 将 `-std=c++11` 写入全局编译参数，且最低 CMake 版本为 3.0。
- 当前 CMake 4.3 已不再直接支持最低版本为 3.0 的项目配置，因此构建系统现代化是阻塞项。
- 核心运行时由 `Fiber`（`ucontext`）、`Scheduler`、`IOManager`（epoll）和 `hook` 组成。
- 定时器以整数毫秒和系统时间实现；线程和同步原语主要封装 POSIX API。
- 配置、HTTP Session 等仍使用 `boost::lexical_cast` 和 `boost::any`。
- 测试是独立可执行程序，尚未完整注册为 CTest。

### 2.2 已完成的兼容性审计

在当前工作树上，使用 GCC 16.1 并让最终生效标准为 C++20，已成功编译以下代表性对象：

- Fiber、Thread、Scheduler、IOManager、hook、Timer；
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
- 阶段 4：已加入不依赖系统调用 hook 的 `AsyncSocket`、`AsyncHttpSession` 和 WebSocket 帧会话，HTTP/WebSocket I/O 均可显式传递 deadline 与取消令牌；现有 Fiber HTTP 服务路径仍保留，TcpServer/HttpServer/Rock 的生产流量切换和配置特性开关待后续迁移。
- 阶段 5：尚未完成；`fiber.*`、`scheduler.*`、`iomanager.*` 和 `hook.*` 仍作为兼容路径保留，必须在生产协议迁移和灰度验证后删除。

本记录只描述已提交代码，不把“可在 C++20 下编译”误记为 Fiber/hook 已删除。每次后续阶段提交都应同步更新此处和第 8 节完成定义。

当前验收范围：C++20 全量库构建和 14 个 `unit` CTest 通过；legacy、environment、integration 和 slow 测试按标签隔离，依赖外部服务或历史 Fiber 行为的测试不计入本地单位测试门禁。Sanitizer、跨编译器 CI、压测及阶段 5 删除遗留运行时仍是未完成项。

迁移期间新旧路径并存：

```text
遗留同步 API ──> Fiber + hook + ucontext ──> epoll
                                      │
                                      │ 适配层（仅迁移期保留）
                                      ▼
新异步 API   ──> Task<T> + co_await ──> Executor ──> EpollReactor
                                                    ├── TimerQueue
                                                    └── Cancellation
```

新路径的原则：

- 不在 Reactor 锁内恢复协程；事件只把 `std::coroutine_handle<>` 投递给 `Executor`。
- I/O 成功、超时、取消和 fd 关闭必须通过原子状态机竞争，且只能有一个路径恢复协程。
- 异步 API 显式传递 `std::stop_token`、超时或 deadline；不得依赖 `LD_PRELOAD` 风格 hook 隐式挂起。
- 所有跨 `co_await` 保存的 Buffer 必须拥有内存；`std::span` 和 `std::string_view` 仅作短生命周期视图。
- 新公开 API 不使用 C++23 的 `std::expected`；使用项目内 `Result<T>`（值或 `std::error_code`）。

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

每迁移一个协议，必须提供特性开关，例如 `server.async_runtime = legacy|coroutine`，以支持灰度发布和快速回退。

验收：HTTP keep-alive、半关闭、断连、超时、取消和限流场景均通过；新旧路径协议行为一致；新路径压测满足阶段 0 设定的性能目标。

### 阶段 5：淘汰遗留运行时

**前提**：所有生产协议和示例均已迁移，且 coroutine 路径至少经过一个发布周期的稳定运行。

任务：

1. 删除 `ucontext` Fiber 的公开依赖和 `Scheduler::schedule(Fiber)` 路径。
2. 删除 `hook.cc` 对 `sleep/read/write/connect` 等函数的全局拦截。
3. 删除仅为 Fiber 服务的 `FdContext`、测试和配置项。
4. 提升 ABI/SOVERSION 主版本，发布迁移说明。

验收：代码库中不再有 `ucontext`、`swapcontext`、`getcontext` 或全局 I/O hook；所有服务由 coroutine runtime 驱动。

## 5. 关键文件映射

| 现有模块 | 迁移目标 | 处理原则 |
| --- | --- | --- |
| `sylar/fiber.*` | `sylar/coroutine/task.*` | 不直接替换；迁移期并存 |
| `sylar/scheduler.*` | `executor.*` | 由 Fiber/回调队列演进为 coroutine handle 队列 |
| `sylar/iomanager.*` | `reactor.*` | epoll 保留，事件载荷改为等待操作 |
| `sylar/timer.*` | `timer_queue.*` | `steady_clock` + deadline |
| `sylar/hook.*` | `async_fd.*` | 新路径不再依赖 hook；最后删除 |
| `sylar/mutex.*`、`thread.*` | 标准同步原语 | 分批替换，保留线程名支持 |
| `sylar/bytearray.*` | span Buffer API | 先增加视图接口，后弃用旧接口 |
| `sylar/socket.*`、`stream.*` | `AsyncSocket` | 显式超时、取消与错误结果 |
| `sylar/http/*` | `Task<Result<T>>` API | 从连接层向服务器层自底向上迁移 |
| `sylar/config.*`、`session_data.*` | 标准库类型 | `std::any`、`from_chars`、chrono |

## 6. 风险与控制措施

| 风险 | 控制措施 |
| --- | --- |
| C++20 coroutine 与有栈 Fiber 语义不同 | 新旧运行时并存；不做全局替换 |
| 超时、取消和 I/O 同时触发 | 等待操作状态机 + exactly-once resume 测试 |
| fd 关闭后被系统复用 | fd generation 校验 |
| `span/string_view` 跨挂起悬垂 | 跨 `co_await` 使用拥有型 Buffer 或 shared ownership |
| `jthread` 在自身线程析构导致自 join | 由外部生命周期控制器统一停止和 join |
| Sanitizer 对 stackful context 切换有误报 | 遗留路径单独测试；新路径以 coroutine sanitizer 结果为准 |
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
9. `remove: legacy fiber and syscall hooks`

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
- `ucontext`、Fiber 和系统调用 hook 已删除；
- Sanitizer、协议回归和性能门禁持续在 CI 执行；
- 发布 ABI 主版本升级及迁移文档。
