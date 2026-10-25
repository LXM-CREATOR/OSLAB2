# Lab 2 四段式 AI 提示词

本文档记录 Lab 2 中两个核心任务所使用的标准提示词。每个提示词均按照四段式框架组织：

1. **PROMPT（任务说明）**：说明任务详情、修改范围、操作约束和验证要求。
2. **RELY（依赖说明）**：只提供完成当前任务必需的类型、宏、函数和参考文件。
3. **GUARANTEE（接口清单）**：规定必须交付或保持不变的对外接口。
4. **SPECIFICATION（行为规格）**：逐个说明函数的前置状态、执行逻辑、分支处理和后置效果。

---

# 提示词一：物理内存探测与初始化 `page_init`

## [PROMPT]

你是一名熟悉 RISC-V、ucore 和物理内存管理的内核开发者。请在现有工程中完成 `kern/mm/pmm.c` 内标有 `//lab2 function1 YOUR CODE` 的 `page_init()` 函数。

### 任务详情

`page_init()` 需要读取设备树解析出的物理内存起点和大小，建立物理页描述符数组 `pages`，将不可分配区域标记为保留，并把扣除 OpenSBI、内核镜像、内核数据以及 `Page` 描述符数组自身占用空间后的完整空闲页交给当前物理内存管理器。

### 操作约束

1. 必须直接修改工程中的 `kern/mm/pmm.c`，不能只给出伪代码或孤立代码片段。
2. 只允许修改 `page_init()` 以及该函数紧邻的必要注释，不得修改 `pmm.h`、`memlayout.h`、`dtb.c`、链接脚本和测试文件。
3. 不得改变 `pmm_init()` 的调用顺序，不得绕过 `init_memmap()` 直接操作某个具体分配器。
4. 不得把物理内存大小硬编码为 128 MiB，必须使用 `get_memory_base()` 和 `get_memory_size()`。
5. 必须区分虚拟地址和物理地址。链接符号 `end` 是内核虚拟地址，参与物理内存边界计算前必须通过 `PADDR()` 转换。
6. 页边界计算必须使用工程已有的 `ROUNDUP`、`ROUNDDOWN`、`PGSIZE`、`PPN` 等宏，不重复定义同类宏。
7. 不得把内核镜像、`Page` 数组或非完整页加入空闲链表。

### 输出与验证要求

1. 输出对 `kern/mm/pmm.c` 的实际修改，并简要解释各计算步骤。
2. 修改后代码必须能够通过现有工具链编译，不产生新的警告或未定义符号。
3. 在评分测试注入后，应输出：

```text
[Test 1] Page Init: PASS
```

4. 同时不得破坏后续的分配器自检和 Best-Fit 测试。

## [RELY]

仅使用以下完成任务所需的最小依赖集合：

### 设备树提供的物理内存信息

```c
uint64_t get_memory_base(void);
uint64_t get_memory_size(void);
```

### `kern/mm/pmm.c` 和 `kern/mm/pmm.h` 已有对象

```c
extern char end[];

struct Page *pages;
size_t npage;
const size_t nbase;
uint64_t va_pa_offset;

static void init_memmap(struct Page *base, size_t n);
```

其中：

- `nbase` 是 DRAM 起始地址对应的物理页号。
- `npage` 表示物理内存末端对应的物理页号上界。
- `pages[i]` 描述的物理页号为 `i + nbase`。
- `PADDR(kva)` 把内核虚拟地址转换为物理地址。

### `kern/mm/memlayout.h` 与 `kern/mm/mmu.h` 已有定义

```c
#define PHYSICAL_MEMORY_OFFSET  0xFFFFFFFF40000000
#define PGSIZE                 4096

struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};
```

允许使用 `SetPageReserved()`、`ROUNDUP()`、`ROUNDDOWN()` 和 `PPN()` 等现有宏。

## [GUARANTEE]

不得新增或改变对外函数接口。实现必须正确维护以下全局状态并调用既有内部接口：

```c
static void page_init(void);

extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;
extern uint64_t va_pa_offset;

static void init_memmap(struct Page *base, size_t n);
```

实现完成后必须保证：

1. `va_pa_offset == PHYSICAL_MEMORY_OFFSET`。
2. `npage == PPN(memory_base + memory_size)`。
3. `pages` 非空、按页对齐，并位于内核 `end` 之后。
4. 内核镜像尾页与 `Page` 数组占用页保持保留状态。
5. `page2pa()` 与 `pa2page()` 对合法页能够正确往返转换。
6. 至少存在一个可交给 `init_memmap()` 管理的完整空闲页，否则应明确报错。

## [SPECIFICATION]

### `page_init()`

**前置状态：**

- `dtb_init()` 已经执行，设备树中的内存信息可通过 `get_memory_base()` 和 `get_memory_size()` 读取。
- 当前 `pmm_manager` 已完成 `init()`，空闲链表处于已初始化但尚未加入可用页的状态。
- 链接符号 `end` 指向内核镜像和静态数据结束后的虚拟地址。

**处理流程：**

1. 读取 `memory_base` 和 `memory_size`。
2. 若任一值为 0，则设备树数据无效，调用 `panic()` 停止初始化。
3. 将 `va_pa_offset` 设为固定的 `PHYSICAL_MEMORY_OFFSET`，不能使用 `KERNBASE - memory_base` 代替。
4. 计算物理内存末地址，并令 `npage` 等于末地址的物理页号上界。
5. 将 `pages` 放在 `end` 之后的第一个页边界处。
6. 初始化受管理物理页对应的 `Page` 描述符，先统一标记为保留，防止尚未确认的区域被分配。
7. 用 `PADDR(end)` 和 `PADDR(pages)` 完成虚拟地址到物理地址的转换。
8. 计算 `Page` 数组末端占用的物理地址，并向上对齐得到第一个候选空闲页。
9. 将物理内存末端向下对齐，得到空闲区上界。
10. 若空闲区起点不小于终点，则没有可用物理页，调用 `panic()`。
11. 把空闲区物理页号换算成 `pages` 数组下标，调用 `init_memmap()` 注册连续空闲页。

**后置效果：**

- 所有不可分配区域仍保持 Reserved。
- 只有 `Page` 数组之后、物理内存末端之前的完整页面进入空闲链表。
- `nr_free_pages()` 返回值大于 0，且不超过 `npage - nbase`。

---

# 提示词二：Best-Fit 连续物理内存分配器

## [PROMPT]

你是一名熟悉 C 语言、双向链表和 ucore 物理页管理框架的内核开发者。请在 `kern/mm/best_fit_pmm.c` 中标有 `//lab2 function2-2 YOUR CODE` 的位置实现 Best-Fit 连续物理内存分配器，并在 `kern/mm/pmm.c` 的 `init_pmm_manager()` 中选择 `best_fit_pmm_manager`。

Best-Fit 的基本目标是：分配 `n` 个连续页时，遍历全部空闲块，在所有 `property >= n` 的候选块中选取容量最小的一块；释放时保持空闲链表按物理地址升序排列，并合并物理地址连续的相邻空闲块。

### 操作约束

1. 必须直接修改工程中的真实文件，不能只展示示例代码。
2. 允许修改范围仅限：
   - `kern/mm/best_fit_pmm.c` 中待实现的分配器代码；
   - `kern/mm/pmm.c` 的 `init_pmm_manager()` 中管理器选择语句。
3. `basic_check()`、`best_fit_check()` 及其已有断言和测试场景不得修改、删除或绕过。
4. 不得修改 `pmm.h`、`memlayout.h`、`list.h` 和 `best_fit_pmm.h` 中已有接口与数据结构。
5. 必须复用工程已有双向链表接口，不得另外实现数组、树或新的容器替代 `free_list`。
6. 可以新增仅在 `best_fit_pmm.c` 内部使用的 `static` 辅助函数，但不得改变管理器对外接口。
7. 分配失败时不得改变空闲链表、页属性或 `nr_free`。

### 输出与验证要求

1. 完成 Best-Fit 的全部核心函数以及 `best_fit_pmm_manager` 注册。
2. 在 `init_pmm_manager()` 中执行：

```c
pmm_manager = &best_fit_pmm_manager;
```

3. 代码应通过 `basic_check()` 和 `best_fit_check()`，并正常输出：

```text
check_alloc_page() succeeded!
[Test 2] Best-Fit: PASS
```

4. 输出修改摘要，说明最优块选择、剩余块拆分、地址有序插入及前后合并分别在哪里完成。

## [RELY]

只依赖以下最小文件和接口：

### `kern/mm/pmm.h` 中的统一管理器接口

```c
struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};
```

### `kern/mm/memlayout.h` 中的空闲块表示

```c
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

typedef struct {
    list_entry_t free_list;
    unsigned int nr_free;
} free_area_t;
```

约定如下：

- 只有空闲块首页设置 `PG_property`。
- 空闲块首页的 `property` 保存该连续块包含的页数。
- `nr_free` 保存当前所有空闲页的总数，而不是空闲块数量。
- `le2page(le, page_link)` 用于从链表节点取得对应 `Page`。

### `libs/list.h` 中允许使用的链表操作

```c
list_init();
list_empty();
list_next();
list_prev();
list_add();
list_add_before();
list_del();
```

`kern/mm/default_pmm.c` 仅作为 First-Fit 的结构参考；Best-Fit 的候选块选择逻辑必须重新实现，不能找到第一个可用块后立即返回。

## [GUARANTEE]

必须提供以下管理器对象：

```c
extern const struct pmm_manager best_fit_pmm_manager;
```

管理器必须注册以下核心函数：

```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
static void best_fit_check(void);
```

允许自行添加内部辅助函数，例如“按地址插入空闲块”或“合并相邻块”，但这些函数必须声明为 `static`，且不能改变以上接口的参数与返回值。

最终注册形式必须与以下语义一致：

```c
const struct pmm_manager best_fit_pmm_manager = {
    .name = "best_fit_pmm_manager",
    .init = best_fit_init,
    .init_memmap = best_fit_init_memmap,
    .alloc_pages = best_fit_alloc_pages,
    .free_pages = best_fit_free_pages,
    .nr_free_pages = best_fit_nr_free_pages,
    .check = best_fit_check,
};
```

## [SPECIFICATION]

### 1. `best_fit_init()`

**前置状态：** 管理器对象已经被选中，空闲区尚未初始化。

**行为：** 初始化 `free_list` 为空的双向循环链表，并把 `nr_free` 置 0。

**后置效果：** `list_empty(&free_list)` 为真，`nr_free == 0`。

### 2. `best_fit_init_memmap(struct Page *base, size_t n)`

**前置状态：**

- `n > 0`。
- `[base, base + n)` 是一段连续、合法、尚未加入空闲链表的物理页。
- 这些页当前均带有 Reserved 标记。

**行为：**

1. 遍历这 `n` 个页，清理旧标志、旧 `property` 和引用计数。
2. 只在首页 `base` 设置 `property = n` 和 Property 标志。
3. 将 `base` 按物理地址升序插入 `free_list`；空链表、插入表头中间和追加表尾三种情况都必须正确。
4. 执行 `nr_free += n`。

**后置效果：** 新空闲块能够被分配器发现，链表仍保持地址有序，总空闲页数增加 `n`。

### 3. `best_fit_alloc_pages(size_t n)`

**前置状态：** `n > 0`，空闲链表结构合法且所有节点均为空闲块首页。

**分支与行为：**

1. 若 `n > nr_free`，立即返回 `NULL`，不得修改任何状态。
2. 遍历完整空闲链表，忽略 `property < n` 的块。
3. 在全部可用块中记录 `property` 最小的块；链表按地址排序时，容量相同可保留先遇到的低地址块。
4. 若遍历后没有候选块，说明总空闲页数虽然可能足够但不存在足够大的连续块，返回 `NULL`，状态保持不变。
5. 若候选块大小恰好等于 `n`，从链表中删除该块。
6. 若候选块大于 `n`，分配其前 `n` 页，并让 `page + n` 成为剩余空闲块首页；剩余块的 `property` 为原大小减 `n`，并保持在原链表位置的地址顺序。
7. 清除已分配块首页的 Property 标志，执行 `nr_free -= n`，返回已分配块首页。

**后置效果：** 返回 `n` 个连续物理页；除选中块及其拆分结果外，其他空闲块不变。

### 4. `best_fit_free_pages(struct Page *base, size_t n)`

**前置状态：**

- `n > 0`。
- `[base, base + n)` 是一段连续且当前已分配的页。
- 各页不带 Reserved 和 Property 标志。

**行为：**

1. 清理各页标志与引用计数。
2. 在 `base` 设置 `property = n` 和 Property 标志。
3. 按物理地址升序把 `base` 插回空闲链表。
4. 执行 `nr_free += n`，该计数只增加一次。
5. 若后继空闲块满足 `base + base->property == next`，将后继块大小累加到 `base->property`，清除后继块 Property 标志并删除其链表节点。
6. 若前驱空闲块满足 `prev + prev->property == base`，将当前块大小累加到 `prev->property`，清除 `base` 的 Property 标志并删除 `base` 的链表节点。
7. 必须支持同时与前、后两个空闲块合并，最终链表中只保留合并后大块的首页。

**后置效果：** 被释放页重新可用；链表保持地址有序；任何物理相邻的空闲块不会以两个独立节点长期存在。

### 5. `best_fit_nr_free_pages()`

**行为：** 只读返回 `nr_free`。

**后置效果：** 不修改链表、页属性或计数器。

### 6. `best_fit_check()` 与 `basic_check()`

这两个函数是既有测试，不允许修改。实现应满足其中所有断言，包括：

- 单页和多页分配成功且页面互不重叠；
- 空链表或不存在足够大连续块时返回 `NULL`；
- 能正确选择更小的合适空闲块，而非第一个可用块；
- 拆分后剩余块大小和 Property 标志正确；
- 释放后能与前后相邻块合并；
- 测试结束后空闲块数量和空闲页总数恢复到测试前状态。

---

## 最终验收标准

完成两项实现后，在实验指定的 WSL 和 QEMU 环境中编译并执行评分测试，目标输出为：

```text
[Test 1] Page Init: PASS
[Test 2] Best-Fit: PASS
Total Score: 20/20
```

若测试失败，应把失败项括号中的具体原因、QEMU 输出以及相关函数当前代码一同提供给 AI，不允许通过删除断言、修改测试文本或硬编码 PASS 字符串绕过验证。
