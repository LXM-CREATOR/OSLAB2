#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);
static void page_init(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    // 切换为 best_fit_pmm_manager 以便进行实验二的功能测试
    pmm_manager = &best_fit_pmm_manager; 
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 YOUR CODE
static void page_init(void) {
    // 1. 获取物理内存的基址和大小
    extern uint64_t get_memory_base(void);
    extern uint64_t get_memory_size(void);
    uint64_t memory_base = get_memory_base();
    uint64_t memory_size = get_memory_size();

    if (memory_base == 0 || memory_size == 0) {
        panic("Invalid physical memory info from DTB!\n");
    }

    // 2. 计算偏移量
    // 注意：虚实映射偏移是固定的 PHYSICAL_MEMORY_OFFSET（entry.S 中建立的
    // Sv39 页表把物理地址 0x80000000 映射到虚拟地址 0xFFFFFFFFC0000000），
    // 而不是 KERNBASE - memory_base。DTB 报告的 memory_base 是 DRAM 起始地址
    // 0x80000000（包含 OpenSBI），若用它计算会得到错误的偏移量，导致
    // 评测项 "[Test 1] Page Init" 中的 va_pa_offset 检查失败。
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // 3. 计算物理页总数
    npage = (memory_base + memory_size) / PGSIZE;

    // 4. 设置 Page 数组起始虚拟地址（紧跟内核结束符 end，并向上页对齐）
    extern char end[];
    pages = (struct Page *)ROUNDUP((void *)end, PGSIZE);

    // 5. 初始先将所有页标记为保留
    for (size_t i = 0; i < npage; i++) {
        SetPageReserved(pages + i);
    }

    // 6. 【修复核心】重新计算正确的可用物理内存起点
    // 第一个可用页的物理地址 = 内核结束物理地址 + Page数组总大小，然后向上对齐
    // 使用 sizeof(struct Page) 计算数组大小
    uintptr_t kernel_end_pa = PADDR(end);
    uintptr_t pages_array_end_pa = PADDR(pages) + npage * sizeof(struct Page);
    
    // 跳过所有占用，找到第一个完整的空闲页边界
    uintptr_t free_pa_start = ROUNDUP(pages_array_end_pa, PGSIZE);
    uintptr_t free_pa_end = ROUNDDOWN(memory_base + memory_size, PGSIZE);

    // 容错处理，理论上不会发生
    if (free_pa_start >= free_pa_end) {
        panic("No free physical memory available!\n");
    }

    // 7. 计算在 pages 数组中的索引位置，并计算可用页数
    size_t free_index_start = (free_pa_start - memory_base) / PGSIZE;
    size_t free_page_count = (free_pa_end - free_pa_start) / PGSIZE;

    // 8. 将空闲页交给分配器
    init_memmap(pages + free_index_start, free_page_count);
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}