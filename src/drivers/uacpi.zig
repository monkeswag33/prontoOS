const kernel = @import("kernel");
const uacpi = @import("uacpi");
const serial = kernel.drivers.serial;
const pci = kernel.drivers.pci;
const mem = kernel.lib.mem;
const idt = kernel.drivers.idt;
const cpu = kernel.drivers.cpu;
const math = @import("std").math;
const log = @import("std").log.scoped(.uacpi);
const limine = @import("limine");
const std = @import("std");

const UACPI_CTX = std.meta.Tuple(&.{ uacpi.uacpi_interrupt_handler, uacpi.uacpi_handle });

export var RSDP_REQUEST: limine.RsdpRequest = .{ .revision = 3 };

const HandlerInfo = struct {
    ctx: uacpi.uacpi_handle,
    callback: uacpi.uacpi_interrupt_handler,
    irq: usize,
};

var num_jobs = std.atomic.Value(usize).init(0);

export fn uacpi_kernel_io_map(addr: uacpi.uacpi_io_addr, _: uacpi.uacpi_size, out: [*c]uacpi.uacpi_handle) uacpi.uacpi_status {
    out.* = @ptrFromInt(addr);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_io_unmap(_: uacpi.uacpi_handle) void {}

export fn uacpi_kernel_io_read8(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, out: [*c]uacpi.uacpi_u8) uacpi.uacpi_status {
    out.* = serial.in(@intCast(@intFromPtr(handle.?) + off), u8);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_io_read16(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, out: [*c]uacpi.uacpi_u16) uacpi.uacpi_status {
    out.* = serial.in(@intCast(@intFromPtr(handle.?) + off), u16);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_io_read32(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, out: [*c]uacpi.uacpi_u32) uacpi.uacpi_status {
    out.* = serial.in(@intCast(@intFromPtr(handle.?) + off), u32);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_io_write8(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, val: uacpi.uacpi_u8) uacpi.uacpi_status {
    serial.out(@intCast(@intFromPtr(handle.?) + off), val);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_io_write16(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, val: uacpi.uacpi_u16) uacpi.uacpi_status {
    serial.out(@intCast(@intFromPtr(handle.?) + off), val);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_io_write32(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, val: uacpi.uacpi_u32) uacpi.uacpi_status {
    serial.out(@intCast(@intFromPtr(handle.?) + off), val);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_pci_device_open(addr: uacpi.uacpi_pci_address, out: [*c]uacpi.uacpi_handle) uacpi.uacpi_status {
    const alloced_addr = mem.kheap.allocator().create(uacpi.uacpi_pci_address) catch return uacpi.UACPI_STATUS_OUT_OF_MEMORY;
    alloced_addr.* = addr;
    out.* = alloced_addr;
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_pci_device_close(handle: uacpi.uacpi_handle) void {
    const addr: *uacpi.uacpi_pci_address = @alignCast(@ptrCast(handle));
    mem.kheap.allocator().destroy(addr);
}

export fn uacpi_kernel_pci_read8(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, val: [*c]uacpi.uacpi_u8) uacpi.uacpi_status {
    const aligned = (off / 4) * 4;
    const shift: u5 = @intCast(off % 4);
    val.* = @truncate(pci.read_reg(@as(*uacpi.uacpi_pci_address, @alignCast(@ptrCast(handle))).*, @intCast(aligned)) >> shift);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_pci_read16(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, val: [*c]uacpi.uacpi_u16) uacpi.uacpi_status {
    const aligned = (off / 4) * 4;
    const shift: u5 = @intCast(off % 4);
    val.* = @truncate(pci.read_reg(@as(*uacpi.uacpi_pci_address, @alignCast(@ptrCast(handle))).*, @intCast(aligned)) >> shift);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_pci_read32(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, val: [*c]uacpi.uacpi_u32) uacpi.uacpi_status {
    val.* = pci.read_reg(@as(*uacpi.uacpi_pci_address, @alignCast(@ptrCast(handle))).*, @intCast(off)) & 0xFFFFFFFF;
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_pci_write8(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, val: uacpi.uacpi_u8) uacpi.uacpi_status {
    _ = handle;
    _ = off;
    _ = val;
    @panic("uacpi_kernel_write8");
}

export fn uacpi_kernel_pci_write16(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, val: uacpi.uacpi_u16) uacpi.uacpi_status {
    _ = handle;
    _ = off;
    _ = val;
    @panic("uacpi_kernel_write16");
}

export fn uacpi_kernel_pci_write32(handle: uacpi.uacpi_handle, off: uacpi.uacpi_size, val: uacpi.uacpi_u32) uacpi.uacpi_status {
    _ = handle;
    _ = off;
    _ = val;
    @panic("uacpi_kernel_write32");
}

export fn uacpi_kernel_alloc(size: uacpi.uacpi_size) ?*anyopaque {
    const ptr = mem.kheap.allocator().alignedAlloc(u8, 8, size) catch @panic("OOM");
    return @ptrCast(ptr.ptr);
}

export fn uacpi_kernel_free(_ptr: ?*anyopaque, size: uacpi.uacpi_size) void {
    const ptr: [*]const u8 = @ptrCast(_ptr orelse return);
    mem.kheap.allocator().free(ptr[0..size]);
}

export fn uacpi_kernel_map(addr: uacpi.uacpi_phys_addr, size: uacpi.uacpi_size) ?*anyopaque {
    const start = (addr / 0x1000) * 0x1000;
    const num_pages = std.math.divCeil(usize, addr % 0x1000 + size, 0x1000) catch unreachable;
    for (0..num_pages) |i| {
        mem.kmapper.map(start + i * 0x1000, start + i * 0x1000, 0b11 | (1 << 63));
    }
    return @ptrFromInt(addr);
}

export fn uacpi_kernel_unmap(_: ?*anyopaque, _: uacpi.uacpi_size) void {}

export fn uacpi_kernel_install_interrupt_handler(irq: uacpi.uacpi_u32, callback: uacpi.uacpi_interrupt_handler, _ctx: uacpi.uacpi_handle, out_handle: [*c]uacpi.uacpi_handle) uacpi.uacpi_status {
    const handler = idt.allocate_handler(kernel.drivers.intctrl.pref_vec(@intCast(irq)));
    const ctx = mem.kheap.allocator().create(HandlerInfo) catch return uacpi.UACPI_STATUS_OUT_OF_MEMORY;
    ctx.* = .{
        .ctx = _ctx,
        .callback = callback,
        .irq = kernel.drivers.intctrl.map(idt.handler2vec(handler), irq) catch return uacpi.UACPI_STATUS_INTERNAL_ERROR,
    };
    handler.ctx = ctx;
    handler.handler = uacpi_handler;
    out_handle.* = handler;
    kernel.drivers.intctrl.mask(ctx.irq, false);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_uninstall_interrupt_handler(_: uacpi.uacpi_interrupt_handler, handle: uacpi.uacpi_handle) uacpi.uacpi_status {
    const handler: *idt.HandlerData = @alignCast(@ptrCast(handle));
    const ctx: *HandlerInfo = @alignCast(@ptrCast(handler.ctx));
    kernel.drivers.intctrl.unmap(ctx.irq);
    handler.reserved = false;
    mem.kheap.allocator().destroy(ctx);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_create_mutex() uacpi.uacpi_handle {
    const mutex = mem.kheap.allocator().create(std.atomic.Value(bool)) catch @panic("OOM");
    mutex.* = std.atomic.Value(bool).init(false);
    return @ptrCast(mutex);
}

export fn uacpi_kernel_acquire_mutex(handle: uacpi.uacpi_handle, timeout: uacpi.uacpi_u16) uacpi.uacpi_status {
    const mutex: *std.atomic.Value(bool) = @ptrCast(handle);
    if (timeout == 0) {
        if (mutex.cmpxchgStrong(false, true, .acquire, .monotonic) != null) {
            return uacpi.UACPI_STATUS_TIMEOUT;
        } else {
            return uacpi.UACPI_STATUS_OK;
        }
    } else if (timeout == 0xFFFF) {
        while (mutex.cmpxchgWeak(false, true, .acquire, .monotonic) != null) {}
        return uacpi.UACPI_STATUS_OK;
    } else {
        @panic("mutex with timeout not implemented");
    }
}

export fn uacpi_kernel_release_mutex(handle: uacpi.uacpi_handle) void {
    const mutex: *std.atomic.Value(bool) = @ptrCast(handle);
    mutex.store(false, .release);
}

export fn uacpi_kernel_free_mutex(handle: uacpi.uacpi_handle) void {
    const mutex: *std.atomic.Value(bool) = @ptrCast(handle);
    mem.kheap.allocator().destroy(mutex);
}

export fn uacpi_kernel_create_spinlock() uacpi.uacpi_handle {
    return uacpi_kernel_create_mutex();
}

export fn uacpi_kernel_lock_spinlock(handle: uacpi.uacpi_handle) uacpi.uacpi_cpu_flags {
    asm volatile ("cli");
    _ = uacpi_kernel_acquire_mutex(handle, 0xFFFF);
    return 0;
}

export fn uacpi_kernel_unlock_spinlock(handle: uacpi.uacpi_handle, _: uacpi.uacpi_cpu_flags) void {
    uacpi_kernel_release_mutex(handle);
    asm volatile ("sti");
}

export fn uacpi_kernel_free_spinlock(handle: uacpi.uacpi_handle) void {
    uacpi_kernel_free_mutex(handle);
}

export fn uacpi_kernel_create_event() uacpi.uacpi_handle {
    const event = mem.kheap.allocator().create(std.atomic.Value(u8)) catch @panic("OOM");
    event.* = std.atomic.Value(u8).init(0);
    return @ptrCast(event);
}

export fn uacpi_kernel_signal_event(handle: uacpi.uacpi_handle) void {
    const event: *std.atomic.Value(u8) = @ptrCast(handle);
    _ = event.fetchAdd(1, .monotonic);
}

export fn uacpi_kernel_wait_for_event(handle: uacpi.uacpi_handle, timeout: uacpi.uacpi_u16) uacpi.uacpi_bool {
    if (timeout != 0xFFFF) @panic("wait for event with timeout not supported");
    const event: *std.atomic.Value(u8) = @ptrCast(handle);
    while (event.load(.monotonic) == 0) {}
    _ = event.fetchSub(1, .monotonic);
    return true;
}

export fn uacpi_kernel_reset_event(handle: uacpi.uacpi_handle) void {
    const event: *std.atomic.Value(u8) = @ptrCast(handle);
    event.store(0, .monotonic);
}

export fn uacpi_kernel_free_event(handle: uacpi.uacpi_handle) void {
    const event: *std.atomic.Value(u8) = @ptrCast(handle);
    mem.kheap.allocator().destroy(event);
}

export fn uacpi_kernel_schedule_work(wtype: uacpi.uacpi_work_type, handler: uacpi.uacpi_work_handler, handle: uacpi.uacpi_handle) uacpi.uacpi_status {
    const stack = mem.kvmm.allocator().alloc(u8, 0x1000) catch return uacpi.UACPI_STATUS_OUT_OF_MEMORY;
    const thread = kernel.lib.scheduler.Thread{
        .affinity = if (wtype == uacpi.UACPI_WORK_GPE_EXECUTION) 0 else null,
        .priority = 99,
        .ef = .{
            .iret_status = .{
                .cs = 0x8,
                .ss = 0x10,
                .rip = @intFromPtr(&uacpi_work_handler),
                .rsp = @intFromPtr(stack.ptr) + 0x1000,
            },
            .rdi = @intFromPtr(handler),
            .rsi = @intFromPtr(handle),
        },
        .mapper = mem.kmapper,
    };
    kernel.lib.scheduler.enqueue(kernel.lib.scheduler.create_thread(thread));
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_wait_for_work_completion() uacpi.uacpi_status {
    // Technically it also says to wait for in flight interrupts to be done
    // There basically isn't any way for our interrupts to not be "scheduled"
    // so we don't wait for it
    while (num_jobs.load(.unordered) > 0) asm volatile ("pause");
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_get_thread_id() uacpi.uacpi_thread_id {
    if (kernel.lib.smp.Task.ret) |_| {
        return @ptrFromInt(kernel.lib.smp.cpu_info(null).cur_proc.?.id);
    } else return @ptrFromInt(1);
}

export fn uacpi_kernel_get_rsdp(out: [*c]uacpi.uacpi_phys_addr) uacpi.uacpi_status {
    const req = RSDP_REQUEST.response orelse return uacpi.UACPI_STATUS_NOT_FOUND;
    out.* = @intFromPtr(req.address);
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_get_nanoseconds_since_boot() uacpi.uacpi_u64 {
    return kernel.drivers.timers.time();
}

export fn uacpi_kernel_sleep(ms: uacpi.uacpi_u64) void {
    kernel.drivers.timers.sleep(ms * 1_000_000);
}

export fn uacpi_kernel_stall(us: uacpi.uacpi_u8) void {
    kernel.drivers.timers.sleep(@as(u64, @intCast(us)) * 1_000);
}

export fn uacpi_kernel_handle_firmware_request(request: [*c]uacpi.uacpi_firmware_request) uacpi.uacpi_status {
    switch (request.*.type) {
        uacpi.UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT => log.warn("Ignoring AML breakpoint - CTX: 0x{?}", .{request.*.unnamed_0.breakpoint.ctx}),
        uacpi.UACPI_FIRMWARE_REQUEST_TYPE_FATAL => log.err("Fatal firmware error - type: {}, code: {}, arg: {}", .{
            request.*.unnamed_0.fatal.type,
            request.*.unnamed_0.fatal.code,
            request.*.unnamed_0.fatal.arg,
        }),
        else => return uacpi.UACPI_STATUS_TYPE_MISMATCH,
    }
    return uacpi.UACPI_STATUS_OK;
}

export fn uacpi_kernel_log(level: uacpi.uacpi_log_level, msg: [*c]const uacpi.uacpi_char) void {
    var str: []const u8 = std.mem.span(msg);
    str = str[0 .. str.len - 1];
    switch (level) {
        uacpi.UACPI_LOG_DEBUG, uacpi.UACPI_LOG_TRACE => log.debug("{s}", .{str}),
        uacpi.UACPI_LOG_INFO => log.info("{s}", .{str}),
        uacpi.UACPI_LOG_WARN => log.warn("{s}", .{str}),
        uacpi.UACPI_LOG_ERROR => log.err("{s}", .{str}),
        else => @panic("uacpi_kernel_log received invalid log level"),
    }
}

fn uacpi_work_handler(func: uacpi.uacpi_work_handler, ctx: uacpi.uacpi_handle) void {
    _ = num_jobs.fetchAdd(1, .acquire);
    func.?(ctx);
    _ = num_jobs.fetchSub(1, .release);
    _ = kernel.lib.syscalls.syscall(.EXIT);
}

fn uacpi_handler(_ctx: ?*anyopaque, status: *cpu.Status) *const cpu.Status {
    const ctx: *HandlerInfo = @alignCast(@ptrCast(_ctx));
    if (ctx.callback.?(ctx.ctx) == uacpi.UACPI_INTERRUPT_NOT_HANDLED) {
        log.warn("uACPI handler for IRQ {} was not handled correctly", .{ctx.irq});
    }
    kernel.drivers.intctrl.eoi(ctx.irq);
    return status;
}
