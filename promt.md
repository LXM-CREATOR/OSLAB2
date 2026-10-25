# Lab 2 AI 提示词与开发记录 (Prompt History)

## 模块一：物理内存初始化 (page_init)

**初始 Prompt：**
你是一个操作系统内核开发者，请帮我完成 ucore 实验中 kern/mm/pmm.c 下的 page_init() 函数。
已知条件：
1. 物理内存起始为 get_memory_base()，大小为 get_memory_size()。
2. 内核镜像结束的虚拟地址用 extern char end[] 表示。
3. 页大小为 4KB，结构体 struct Page 已经定义好。
任务要求：计算总页数，建立 Page 数组，并排除内核及数组自身占用，将剩余内存交给 init_memmap。

**遇到的 Bug：**
AI 生成的代码直接使用了虚拟地址 `end` 去做计算，导致 panic 报错 `PADDR called with invalid kva`。

**改进后的 Prompt：**
（指出错误）由于 CPU 目前在虚拟地址模式，`end` 是虚拟地址。请修改代码，必须使用 `PADDR(end)` 将其转成物理地址后，再进行页对齐计算。

**最终结果：** 编译通过，`make qemu` 成功打印 `DTB Init` 并且没有 panic。

---

## 模块二：Best-Fit 分配算法 (best_fit_pmm.c)

**初始 Prompt：**
参考 kern/mm/default_pmm.c 的代码框架，在 kern/mm/best_fit_pmm.c 中实现 Best-Fit 分配器。
要求在 alloc_pages 时遍历链表，找到 property >= n 且最小的块进行切分。在 free_pages 时，检查前后相邻块是否连续并进行合并。

**遇到的 Bug：**
在测试 `basic_check` 时触发 assertion failed，链表崩溃。排查发现 AI 生成的 free_pages 中，合并相邻块时忘记更新合并后块的 property 属性。

**改进后的 Prompt：**
（指出错误）在 best_fit_free_pages 中，合并 `base` 和 `p` 时，必须执行 `base->property += p->property;`，并且清除 `p` 的 Property 标记。

**最终结果：** Best-Fit 算法单测通过，输出 `check_alloc_page() succeeded!`。