const pmm: type = @import("pmm.zig");

const PAGE_SIZE: u64 = 4096;
const ENTRIES_PER_TABLE: usize = 512;

pub const PageTable: type = [ENTRIES_PER_TABLE]u64;

const PTE_PRESENT: u64 = 1 << 0;
const PTE_WRITABLE: u64 = 1 << 1;
const PTE_USER: u64 = 1 << 2;

const ADDR_MASK: u64 = 0x000FFFFFFFFFF000;

fn pml4Index(vaddr: u64) usize {
    return @intCast((vaddr >> 39) & 0x1FF);
}

fn pdptIndex(vaddr: u64) usize {
    return @intCast((vaddr >> 30) & 0x1FF);
}

fn pdIndex(vaddr: u64) usize {
    return @intCast((vaddr >> 21) & 0x1FF);
}

fn ptIndex(vaddr: u64) usize {
    return @intCast((vaddr >> 12) & 0x1FF);
}

fn getOrCreateTable(table: *PageTable, index: usize, user: bool) !*PageTable {
    if (table[index] & PTE_PRESENT == 0) {
        const new_page: u64 = pmm.allocPage() orelse return error.OutOfMemory;

        const new_table: *PageTable = @ptrFromInt(new_page);
        for (new_table) |*entry| {
            entry.* = 0;
        }

        var flags: u64 = PTE_PRESENT | PTE_WRITABLE;
        if (user) flags |= PTE_USER;

        table[index] = new_page | flags;
    } else if (user) {
        table[index] |= PTE_USER;
    }

    const phys: u64 = table[index] & ADDR_MASK;
    const result: *PageTable = @ptrFromInt(phys);
    return result;
}

pub fn mapPage(pml4: *PageTable, vaddr: u64, paddr: u64, writable: bool, user: bool) !void {
    const pdpt: *PageTable = try getOrCreateTable(pml4, pml4Index(vaddr), user);
    const pd: *PageTable = try getOrCreateTable(pdpt, pdptIndex(vaddr), user);
    const pt: *PageTable = try getOrCreateTable(pd, pdIndex(vaddr), user);

    var entry: u64 = paddr | PTE_PRESENT;
    if (writable) entry |= PTE_WRITABLE;
    if (user) entry |= PTE_USER;

    pt[ptIndex(vaddr)] = entry;

    asm volatile ("invlpg (%[addr])"
        :
        : [addr] "r" (vaddr),
        : .{ .memory = true });
}

pub fn unmapPage(pml4: *PageTable, vaddr: u64) void {
    const pml4_idx: usize = pml4Index(vaddr);
    if (pml4[pml4_idx] & PTE_PRESENT == 0) return;
    const pdpt: *PageTable = @ptrFromInt(pml4[pml4_idx] & ADDR_MASK);

    const pdpt_idx: usize = pdptIndex(vaddr);
    if (pdpt[pdpt_idx] & PTE_PRESENT == 0) return;
    const pd: *PageTable = @ptrFromInt(pdpt[pdpt_idx] & ADDR_MASK);

    const pd_idx: usize = pdIndex(vaddr);
    if (pd[pd_idx] & PTE_PRESENT == 0) return;
    const pt: *PageTable = @ptrFromInt(pd[pd_idx] & ADDR_MASK);

    const pt_idx: usize = ptIndex(vaddr);
    pt[pt_idx] = 0;

    asm volatile ("invlpg (%[addr])"
        :
        : [addr] "r" (vaddr),
        : .{ .memory = true });
}

pub fn getCurrentPML4() *PageTable {
    var cr3: u64 = undefined;
    asm volatile ("mov %%cr3, %[out]"
        : [out] "=r" (cr3),
    );
    const result: *PageTable = @ptrFromInt(cr3 & ADDR_MASK);
    return result;
}
