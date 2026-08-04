#ifndef TELOS_TASK_SCOPE_H
#define TELOS_TASK_SCOPE_H

#include <telos/types.h>

#include <telos/error.h>

/*
 * Task Scope — 长程任务的副作用作用域
 *
 * 每个长程任务（workflow、巡检、维护流程）开启一个作用域；任务中
 * 注册的所有副作用（资源、注册项、订阅、定时器）归该作用域管理，
 * 在任务完成 / 失败 / 取消时按注册逆序（LIFO）整体撤销。
 *
 * 语义（对应 Cordis 可逆性思想在 C 中的落点）：
 *   - LIFO 逆序：后注册先撤销（先退订、再释放资源，防 use-after-free）
 *   - 幂等：dispose 回调自身可重复调用；scope 层保证每个回调只调用一次
 *   - 原子边界：要么全部生效（commit），要么全部撤销（dispose）
 *   - 嵌套：子作用域挂在父作用域下，父 dispose 级联未清理的子作用域
 *
 * 约束：
 *   - dispose 回调不得分配内存、不得阻塞（否则清理本身失败）
 *   - 任务取消时正在执行的工具：先 cancel，等其退出，再 dispose
 *   - 本实现使用动态数组（Linux 可堆路径）；Zephyr 无堆路径下
 *     由静态池提供同一接口（见无堆路径纪律）
 */

struct telos_task_scope;

typedef void (*telos_task_scope_dispose_fn)(void *context);

/*
 * 打开一个任务作用域。parent 可为 NULL（顶层作用域）。
 * name 仅用于审计与诊断。
 */
struct telos_task_scope *
telos_task_scope_open(struct telos_task_scope *parent,
                      const char *name,
                      struct telos_error **error);

/*
 * 注册一个副作用项。dispose 回调在作用域 dispose 时按 LIFO 调用。
 * commit 之后禁止再注册。
 */
bool telos_task_scope_register(struct telos_task_scope *scope,
                               const char *name,
                               telos_task_scope_dispose_fn dispose,
                               void *context,
                               struct telos_error **error);

/*
 * 任务成功：成果保留。释放作用域骨架但**不调用** dispose 回调。
 * 之后 scope 只可销毁，不可再注册或 dispose。
 */
bool telos_task_scope_commit(struct telos_task_scope *scope,
                             struct telos_error **error);

/*
 * 任务失败 / 取消：按 LIFO 逆序调用全部 dispose 回调（每个一次），
 * 级联处理未清理的子作用域，然后释放作用域骨架。
 * 幂等：重复调用是 no-op。
 */
void telos_task_scope_dispose(struct telos_task_scope *scope);

/*
 * 销毁作用域骨架（不调用 dispose 回调）。通常由 commit / dispose
 * 内部完成；仅在需要提前释放审计名称等资源时显式调用。
 */
void telos_task_scope_destroy(struct telos_task_scope *scope);

/* 作用域当前状态（诊断 / 审计用）。 */
enum telos_task_scope_state {
    TELOS_TASK_SCOPE_OPEN = 1,
    TELOS_TASK_SCOPE_COMMITTED,
    TELOS_TASK_SCOPE_DISPOSED,
};

enum telos_task_scope_state
telos_task_scope_state(const struct telos_task_scope *scope);

#endif
