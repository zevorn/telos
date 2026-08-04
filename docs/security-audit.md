# Security & Memory Audit Rules

本文档定义 Telus 代码库的安全审计和内存安全审查规则。所有代码变更必须
通过以下检查才能合入。

---

## 一、内存安全规则

### 1.1 分配-释放配对（Malloc-Free Pairing）

每个 `malloc` / `calloc` / `realloc` 必须在**同一函数**或**同一所有权上下文**
中有对应的 `free`。

- ✅ 使用 `goto cleanup` 模式集中释放
- ✅ 使用 `telos_*_retain` / `telos_*_release` 配对管理引用计数
- ❌ 禁止将 `malloc` 返回的指针通过 `return` 传给调用者而不文档化所有权

**检查命令**：
```sh
# 列出所有 malloc 和 free，手动核对每条路径
grep -n "malloc\|calloc\|realloc" file.c
grep -n "free(" file.c
```

### 1.2 引用计数完整性

`telos_*_retain()` 的每次调用必须有对应的 `telos_*_release()`。
涉及类型：`telos_value`、`telos_error`、`telos_event`、
`telos_plugin_instance`、`telos_registry_generation`。

**检查命令**：
```sh
grep -n "_retain\|_release" file.c
```

### 1.3 资源句柄泄漏

- `fopen` → `fclose`
- `popen` → `pclose`
- `open` / `socket` → `close`
- `dlopen` → `dlclose`
- `pthread_mutex_init` → `pthread_mutex_destroy`
- `pthread_cond_init` → `pthread_cond_destroy`
- `pthread_create` → `pthread_join` 或 `pthread_detach`

### 1.4 错误路径覆盖

每个含有资源分配的函数必须确保**所有错误返回路径**都释放已分配资源。

模式：
```c
char *buf = malloc(size);
if (buf == NULL) {
    return false;  /* 尚未分配其他资源，安全返回 */
}

FILE *f = fopen(path, "r");
if (f == NULL) {
    free(buf);     /* 必须释放之前分配的 buf */
    return false;
}
/* ... */
fclose(f);
free(buf);
return true;
```

更好的方式——`goto cleanup`：
```c
char *buf = NULL;
FILE *f = NULL;
bool ok = false;

buf = malloc(size);
if (buf == NULL) goto cleanup;

f = fopen(path, "r");
if (f == NULL) goto cleanup;

ok = true;
cleanup:
if (f) fclose(f);
free(buf);
return ok;
```

### 1.5 数组越界

- 所有 `memcpy` / `memmove` / `snprintf` 的目标缓冲区大小必须验证
- 使用 `sizeof(buf)` 而非硬编码数值
- `snprintf` 返回值必须检查（`>= sizeof(buf)` 表示截断）

---

## 二、并发安全规则

### 2.1 共享状态保护

- `pthread_mutex_lock` 必须有对应的 `pthread_mutex_unlock`（包括错误路径）
- 原子操作使用 `memory_order_acquire` / `memory_order_release` 配对
- `atomic_fetch_add` 释放时使用 `memory_order_acq_rel`

### 2.2 线程生命周期

- `pthread_create` 创建的线程必须在销毁相关资源前 join 或 detach
- 通过 `atomic_bool` 或 `telos_cancel` 通知线程退出
- 禁止 `pthread_cancel` 异步取消（资源泄漏风险）

---

## 三、输入验证规则

### 3.1 外部输入

以下输入源视为不可信：
- 用户输入（TUI 编辑框）
- 模型输出（streaming text / tool arguments）
- 文件内容（Skills、配置、AGENTS.md）
- 环境变量
- RPC 消息
- Plugin 返回值

必须：
- 验证长度（不超过目标缓冲区 - 1）
- 过滤控制字符（0x00-0x1F, 0x7F）
- 验证 UTF-8 合法性（如需要）
- 使用 `telos_tui_host_sanitize` 或等价函数清洗输出

### 3.2 Shell 注入防护

使用 `popen` 或 `fork/exec` 执行外部命令时必须：
- ❌ 禁止拼接用户输入到 shell 字符串（如 `sprintf(cmd, "ls %s", user_input)`）
- ✅ 使用参数数组调用 `execvp` / `execvpe`
- 若必须用 shell，对单引号进行转义：`'` → `'\''`

---

## 四、代码冗余规则

### 4.1 禁止重复的静态工具函数

同一函数体出现在 ≥3 个文件中时，必须提取到共享头文件或 Core 库。

**已知违规（已修复）**：
- ~~`static void set_error()` — 33 个副本~~ → 已合并入 `telos_error_set()` in `error.h`

**已知违规（待修复）**：
- `static bool write_all()` — 4 个副本（tui.c, openai_codex_auth.c, api_key_auth.c, rpc.c）
  → 建议：提取到 `telos/io.h` 或 `telos/base.h`

### 4.2 浅封装检查

函数如果只是调用另一个函数并传递相同参数，且不添加任何：
- 验证逻辑
- 错误处理
- 状态转换
- 抽象层次变化

则该函数是**浅封装**，应删除或合并。

**检查方法**：
```sh
# 查找 ≤3 行的函数体
grep -A3 "^static .* {$" file.c
```

### 4.3 深层模块特征

深层模块的接口应满足：
- 接口面积小（≤5 个公开函数）
- 隐藏复杂实现细节
- 调用者无需了解内部状态机

**反例**（浅模块）：
```c
// 只是转发，没有增加价值
bool foo_bar(struct foo *f) { return baz(f->x); }
```

**正例**（深模块）：
```c
// 隐藏了事务、代数管理、能力检查
bool telos_registry_transaction_commit(telos_registry_transaction *tx,
                                       struct telos_error **error);
```

---

## 五、审查清单（Code Review Checklist）

每条 PR 必须通过以下检查：

### 内存
- [ ] 所有 malloc/calloc/realloc 有对应的 free
- [ ] 所有 retain 有对应的 release
- [ ] 所有 open/socket/fopen/popen/dlopen 有对应的 close/fclose/pclose/dlclose
- [ ] 所有错误返回路径释放了已分配资源
- [ ] 无 use-after-free（释放后不再访问指针）
- [ ] 无 double-free（同一指针不释放两次）

### 并发
- [ ] 所有 mutex_lock 有对应的 mutex_unlock
- [ ] 线程创建后有 join 或 detach
- [ ] 共享数据通过 mutex 或 atomic 保护

### 输入
- [ ] 外部输入长度已验证
- [ ] 控制字符已过滤
- [ ] shell 命令无注入风险
- [ ] 格式化字符串使用字面量（不用变量作 format）

### 设计
- [ ] 无重复的静态工具函数（≥3 副本）
- [ ] 无浅封装（≤3 行且无额外逻辑）
- [ ] 接口抽象层次一致
- [ ] 错误信息使用英文，不含调试信息

---

## 六、自动化检查

建议在 CI 中启用：

```sh
# 静态分析（Clang 已启用 -Weverything 子集）
meson setup build -Dwarning_level=3 -Dwerror=true

# Address Sanitizer（检测内存错误）
meson setup build-sanitize -Db_sanitize=address,undefined

# Leak Sanitizer（检测内存泄漏）
ASAN_OPTIONS=detect_leaks=1 meson test -C build-sanitize

# 代码冗余检测
./scripts/check-function-layout.py    # 项目已有
grep -rn "static void set_error" --include="*.c" | wc -l  # 应为 0
```
