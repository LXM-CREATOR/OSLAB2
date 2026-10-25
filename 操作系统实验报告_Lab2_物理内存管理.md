# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 2: 物理内存管理 |
| **小组成员** | 2411438-李熙铭、2411513-杨珺童、2413101-谢沛霖 |
| **完成日期** | 2026-10-22 |

### 小组分工

| 成员 | 负责的练习/模块 | 实验报告负责部分 |
|------|----------------|------------------|
| 2411438-李熙铭 | **基础物理内存探测与初始化 (`page_init`)** | 负责撰写模块一的实现与 Prompt、以及整体逻辑分析。 |
| 2411513-杨珺童 | **First-Fit 连续物理内存分配算法** | 负责撰写 First-Fit 算法原理、测试以及改进空间分析。 |
| 2413101-谢沛霖 | **Best-Fit 分配算法 (挑战/扩展)** | 负责撰写 Best-Fit 的 Prompt 迭代、Bug 解决与测试。 |

---

## 一、实验目的

1. 理解页表的建立和使用方法，掌握 RISC-V 的 Sv39 分页机制。
2. 理解物理内存的管理方法，掌握如何通过设备树探测内存并建立 `Page` 描述符。
3. 理解页面分配算法，实现并对比 First-Fit 与 Best-Fit 算法。

---

## 二、实验环境

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2411438-李熙铭 | Cline (VS Code) | Claude 3.5 Sonnet | 用于生成底层初始化逻辑 |
| 2411513-杨珺童 | ChatGPT | GPT-4o | 用于辅助分析链表合并逻辑 |
| 2413101-谢沛霖 | Cursor | DeepSeek-V3 | 用于从零生成 Best-Fit 和测试代码 |

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

本章实验围绕**“物理内存管理”**展开。在 Lab 1 的基础上，操作系统已经完成了从物理地址空间到虚拟地址空间的跨越。为了让内核能够高效地使用内存，本章主要解决两个问题：

1. **“有哪些物理内存可用”**：通过设备树 DTB 探测物理内存范围。
2. **“如何管理和分配这些物理内存”**：通过 `page_init` 建立 `Page` 描述符，并利用 First-Fit、Best-Fit 等算法进行分配与回收。

### 3.2 功能的逐步实现

1. **首先实现物理内存探测与描述符初始化 (`page_init`)**：这是内存管理的基础。必须先知道物理内存的边界，并扣除 OpenSBI、内核镜像和 `Page` 数组本身占用的空间，剩下的才是可用内存。
2. **接着实现 First-Fit 分配算法 (`default_pmm`)**：作为最基础的分配器，它通过双向链表管理空闲页，实现按需分配和相邻空闲块合并。
3. **最后实现 Best-Fit 分配算法 (`best_fit_pmm`)**：在理解基础分配流程后，通过实现 Best-Fit 进一步体会不同算法在减少内存碎片方面的差异。

---

## 四、实验内容与实现

### 功能模块一：物理内存初始化功能

**负责人：** 2411438-李熙铭

**需要修改代码的文件名：** `kern/mm/pmm.c`（寻找 `page_init` 函数并替换 `YOUR CODE`）

#### 模块功能描述

`page_init` 负责读取 DTB 提供的物理内存范围，建立全局的 `Page` 数组，并将去掉已占用空间（内核、OpenSBI、Page 数组自身）后的完整空闲物理页交由当前 `pmm_manager` 管理。

#### 练习的 Spec 与 Prompt 说明

**1. 练习的 Spec（需求分析）：**

- **输入：** 从 `get_memory_base()` 和 `get_memory_size()` 获取的物理内存起点和大小。
- **约束：** 需计算 `Page` 数组所需空间，确保内核镜像和数组不会进入空闲区。物理地址计算需正确使用 `va_pa_offset` 或宏 `PADDR`。
- **输出：** 更新全局变量 `npage`、`pages`、`va_pa_offset`，并调用 `init_memmap()`。

**2. 我们的 Prompt 设计：**

```text
在阅读 `pmm.c` 时，我注意到代码中使用了全局变量 `va_pa_offset`。在开启了虚拟内存的 RISC-V Sv39 架构下，内核的虚拟地址和物理地址到底是如何转换的？为什么内核通常需要运行在高地址（例如 `0xFFFFFFFFC0000000`）？

[RELY]

- 链接脚本中的 `BASE_ADDRESS` 设置。
- RISC-V Sv39 分页机制（三级页表、大页映射）。
- 虚拟地址与物理地址的固定偏移量 `va_pa_offset`。
- `PADDR()` 与 `KADDR()` 宏的定义与作用。

[GUARANTEE]

分析结果需要说明：
1. 页表如何实现内核空间的高半区（High Half）映射。
2. `va_pa_offset` 的计算方式及其在地址转换中的数学关系。
3. 为什么这种设计能提高内核访问物理内存的效率。

[SPECIFICATION]

- 必须基于 RISC-V 的虚拟内存机制解释，而非 x86。
- 明确虚拟地址减去偏移量等于物理地址的直接计算关系。
- 不得修改已有的地址转换宏或页表初始化流程。

```

#### 实现与 Bug 解决过程

- **遇到的 Bug：** AI 第一次生成的代码在计算可用内存起点时，直接使用了虚拟地址 `end` 去做页对齐运算，然后去算物理页号，导致程序 panic 报错 `PADDR called with invalid kva`。
- **解决方式：** 修改 Prompt，明确告知 AI：“由于 CPU 目前在虚拟地址模式，`end` 是虚拟地址。需要用 `PADDR(end)` 转成物理地址后，再进行页对齐，并重新计算剩余物理页数。”修正后测试通过。

---

### 功能模块二：物理内存分配功能（First-Fit）

**负责人：** 2411513-杨珺童

**需要修改代码的文件名：** `kern/mm/default_pmm.c`

#### 模块功能描述

基于双向链表管理空闲页块，实现 First-Fit（首次适应）算法。当分配 `n` 个页面时，从空闲链表头开始查找第一个拥有不少于 `n` 个连续页面的块。释放时，将页面插回链表，并检查物理地址上是否与前后块连续以进行合并。

**2. 我们的 Prompt 设计：**

```text
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

```

#### 思考题回答

**原 First-Fit 算法是否有改进空间？**

有。First-Fit 倾向于优先使用低地址的空闲块，这会导致低地址区域频繁分配和释放，产生大量较小的外部碎片；而高地址区域可能长期得不到利用。改进空间包括：

1. **Next-Fit（循环首次适应）**：每次分配从上次分配结束的位置开始查找，使内存分配更均匀，减少低地址碎片。
2. **Best-Fit（最佳适应）**：在分配时遍历整个链表，找到满足要求且最小的块，保留大块内存。
3. **分离空闲链表**：按内存块大小分级维护不同的链表，加快查找速度。

---

### 功能模块三：实现 Best-Fit 连续物理内存分配算法（扩展/Challenge）

**负责人：** 2413101-谢沛霖

**需要修改代码的文件名：** `kern/mm/best_fit_pmm.c`

#### 模块功能描述

在 `pmm_manager` 接口下实现 Best-Fit 算法。分配时，遍历空闲链表，记录所有满足 `property >= n` 的空闲块中，`property` 最小的那一个进行分配；如果当前块比需求大，则切分。

#### 练习的 Spec 与 Prompt 说明

**1. 练习的 Spec（需求分析）：**

- 必须满足 `basic_check()` 和 `best_fit_check()` 两个测试函数的调用约定。
- 不得修改测试函数。
- `alloc_pages` 需返回找到的最优块；`free_pages` 需保持空闲链表按物理地址升序排序，并实现物理相邻块的合并。

**2. 我们的 Prompt 设计（分为两步）：**

**Prompt 1（分析核心算法逻辑）：**

```text
[PROMPT]

我们组已实现了 First-Fit，现在正在做 Best-Fit。理论上 Best-Fit 能减少大块内存被切分，但它有什么潜在缺点？在系统频繁申请和释放内存时，它的性能表现如何？请对比两种算法给出分析。

[RELY]

- 算法的时间复杂度（链表遍历次数）。
- 外部碎片的产生原因。
- 现代操作系统的内存分配策略（如 Buddy System、SLUB）。

[GUARANTEE]

分析结果需要回答：
1. Best-Fit 为什么会产生大量难以利用的微小碎片。
2. 为什么遍历整个空闲链表会带来性能瓶颈。
3. 为什么实际系统中不单独依赖 Best-Fit。

[SPECIFICATION]

- 必须从空间利用率和时间效率两个维度进行对比。
- 分析需符合操作系统实际应用场景，不得局限于实验测试用例。

```

**Prompt 2（测试验证结果分析）：**

```text
[PROMPT]

运行 `make grade` 时，`check page init` 报 WRONG。但我的 `make qemu` 能正常输出 `check_alloc_page() succeeded!`。我在 `page_init` 里用 `npage * sizeof(struct Page)` 减去内核占用算出了 `nr_free`。请分析测试脚本到底在检查 `page_init` 的什么核心指标？我遗漏了什么计算？

#### [RELY]

- `check_alloc_page()` 的基本逻辑（仅测试分配与释放能力）。
- 物理内存的实际划分范围（OpenSBI、内核、`Page` 数组、空闲页）。
- 测试脚本对 `nr_free`（可用物理页总数）的精确校验。

[GUARANTEE]

分析结果需要说明：
1. 为什么分配逻辑正确但初始化数量不对。
2. `Page` 描述符数组本身占用的物理内存大小如何精确计算。
3. OpenSBI 等保留区的物理地址范围是否被正确扣除。

[SPECIFICATION]

- 需要提供计算剩余可用物理页的精确数学公式（考虑页对齐和边界情况）。
- 不得通过修改测试脚本来迁就错误的 `nr_free` 值。

```

#### 实现与 Bug 解决过程

- **遇到的 Bug：** 在测试用例中，当我们连续分配和释放不同大小的块时，链表崩溃。通过排查发现，AI 生成 `best_fit_free_pages` 时，合并相邻块的逻辑写错了，忘记更新合并后块的 `property` 大小。
- **解决方式：** 将报错信息（`assertion failed`）和当前链表状态发给 AI，它指出了在合并时没有把被合并页的 `property` 加到当前块上，也没有 `ClearPageProperty`。修复后通过测试。

---

## 五、测试与验证


### 输出说明

从输出可以看到，内核成功探测到物理内存大小（128MB）。输出：

```text
memory management: best_fit_pmm_manager
```

说明成功注册了我们的最佳适应分配器；同时：

```text
check_alloc_page() succeeded!
```

说明测试通过，算法实现正确。

---

## 六、实验总结与收获

### 6.1 对操作系统的理解

#### 1. 本实验中重要的知识点与 OS 原理的对比

- **物理页描述符（`Page`）与地址转换：** OS 原理中，页表将虚拟页映射到物理页。而在本实验中，为了管理物理内存，引入了 `struct Page` 数组。

  **差异在于：** 操作系统为了管理内存，必须“消耗”内存来存储这些元数据（Metadata），这体现了系统级编程中的“自举（Bootstrap）”以及空间换时间的折中思想。

- **连续内存分配算法：** OS 原理中经常讲解首次适应、最佳适应等算法。本实验将理论与实践结合，发现实际编码中，**双向链表的排序与物理地址相邻块的合并（Coalescing）** 是比算法理论更棘手的工程问题。必须严格判断：

```c
p + p->property == base
```

#### 2. OS 原理中很重要、但在本实验中没有对应上的知识点

- **页表与物理内存的脱钩（按需分页与 Swap）：** 本实验重点在于“物理页的分配”。在真实操作系统中，进程申请内存时并不一定立即分配实际物理页，而是可以通过缺页异常（Page Fault）触发按需分配；在物理内存不足时，还可能将页面换出到磁盘（Swap）。
- 本实验并未涉及缺页中断处理和页面置换算法（如 LRU）。

---

### 6.2 AI 协作开发的经验

1. **大模型非常擅长“照猫画虎”，但极容易在边界条件上翻车。** 例如在从零写 Best-Fit 时，AI 的骨架逻辑是对的，但往往漏掉“物理内存不连续时不能合并”的判断。这就要求我们必须有扎实的 Spec 描述能力，并在代码生成后，自己手动增加严格的断言（Assert）去测试。
2. **“双重调试”中的 AI 妙用。** 助教提供的双重 GDB 调试思路非常强大。在试图理解 QEMU 源码时，我直接把报错的 QEMU 源码片段交给 AI，询问：“在 `riscv_cpu_tlb_fill` 函数中，这个条件分支是用来模拟真实 CPU 的哪一部分？”AI 给出了非常详细的解释，极大地降低了阅读庞大模拟器源码的恐惧感。

---

## 结论

通过本次 Lab 2，我们完成了从**物理内存探测 → Page 描述符初始化 → First-Fit 分配 → Best-Fit 扩展实现 → 测试与调试**的完整流程。

本实验不仅帮助我们理解了 RISC-V Sv39 分页环境下物理内存管理的基本机制，也让我们认识到，在实际内核开发中，算法本身只是第一步，**地址转换、边界条件、链表维护以及内存块合并**等工程细节同样决定了系统是否能够稳定运行。

同时，通过 AI 辅助编程，我们进一步认识到：AI 可以显著提高代码生成、错误定位和源码理解的效率，但不能替代开发者对 Spec、数据结构、地址空间以及测试结果的理解和验证。
