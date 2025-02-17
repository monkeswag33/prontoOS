const uacpi = @import("uacpi");
const log = @import("std").log.scoped(.acpi);
const std = @import("std");
const kernel = @import("kernel");

pub const VTable = struct {
    hids: []const [*c]const u8,
    ret: *uacpi.uacpi_namespace_node,
};

pub var TablesTask = kernel.Task{
    .name = "ACPI Tables",
    .init = init_tables,
    .dependencies = &.{
        .{ .task = &kernel.lib.mem.KHeapTask },
        .{ .task = &kernel.lib.mem.KMapperTask },
    },
};

pub var NamespaceLoadTask = kernel.Task{
    .name = "ACPI Namespace Load",
    .init = namespace_load,
    .dependencies = &.{
        .{ .task = &TablesTask },
        .{ .task = &kernel.drivers.timers.Task },
        .{ .task = &kernel.drivers.intctrl.Task },
        .{ .task = &kernel.drivers.pci.Task },
        .{ .task = &kernel.lib.scheduler.Task },
    },
};

pub var DriversTask = kernel.Task{
    .name = "ACPI Drivers",
    .init = init_drivers,
    .dependencies = &.{
        .{ .task = &NamespaceLoadTask },
    },
};

const AVAILABLE_DRIVERS = [_]type{};

const CallbackInfo = struct {
    vtable: *VTable,
    task: *kernel.Task,
};

fn init_tables() kernel.Task.Ret {
    if (uacpi.uacpi_initialize(0) != uacpi.UACPI_STATUS_OK) return .failed;
    if (uacpi.uacpi_install_fixed_event_handler(uacpi.UACPI_FIXED_EVENT_POWER_BUTTON, fixed_shutdown_handler, null) != uacpi.UACPI_STATUS_OK) return .failed;
    return .success;
}

fn install_notify_handler(_: ?*anyopaque, node: ?*uacpi.uacpi_namespace_node, _: uacpi.uacpi_u32) callconv(.C) uacpi.uacpi_iteration_decision {
    _ = uacpi.uacpi_install_notify_handler(node, shutdown_notify_handler, null);
    return uacpi.UACPI_ITERATION_DECISION_CONTINUE;
}

fn shutdown_notify_handler(_: ?*anyopaque, _: ?*uacpi.uacpi_namespace_node, value: uacpi.uacpi_u64) callconv(.C) uacpi.uacpi_status {
    if (value != 0x80) return uacpi.UACPI_STATUS_OK;

    log.info("shutdown_notify_handler called", .{});
    return uacpi.UACPI_STATUS_OK;
}

fn fixed_shutdown_handler(_: ?*anyopaque) callconv(.C) uacpi.uacpi_interrupt_ret {
    log.info("Shutdown fixed event handler", .{});
    return uacpi.UACPI_INTERRUPT_HANDLED;
}

fn namespace_load() kernel.Task.Ret {
    if (uacpi.uacpi_namespace_load() != uacpi.UACPI_STATUS_OK) return .failed;
    if (uacpi.uacpi_find_devices("PNP0C0C", install_notify_handler, null) != uacpi.UACPI_STATUS_OK) return .failed;
    return .success;
}

fn init_drivers() kernel.Task.Ret {
    inline for (AVAILABLE_DRIVERS) |driver| {
        var info = CallbackInfo{ .task = &driver.ACPITask, .vtable = &driver.ACPIVTable };
        // I am doing this allocation because I don't want the user making ACPI drivers to have to constantly
        // put the uacpi.UACPI_NULL themselves. This should be handled automatically and one extra allocation + free is the cost
        const hids = kernel.lib.mem.kheap.allocator().alloc([*c]const u8, driver.ACPIVTable.hids.len + 1) catch @panic("OOM");
        std.mem.copyForwards([*c]const u8, hids, driver.ACPIVTable.hids);
        hids[driver.ACPIVTable.hids.len] = @ptrCast(uacpi.UACPI_NULL);
        if (uacpi.uacpi_find_devices_at(
            // Simple optimization because basically all devices are in the SB namespace
            // If this proves to be an issue will change it to uacpi_namespace_root
            uacpi.uacpi_namespace_get_predefined(uacpi.UACPI_PREDEFINED_NAMESPACE_SB),
            hids.ptr,
            driver_callback,
            &info,
        ) != uacpi.UACPI_STATUS_OK) @panic("uacpi_find_devices_at failed");
        kernel.lib.mem.kheap.allocator().free(hids);
    }
    return .success;
}

fn driver_callback(_info: ?*anyopaque, node: ?*uacpi.uacpi_namespace_node, _: uacpi.uacpi_u32) callconv(.C) uacpi.uacpi_iteration_decision {
    const info: *CallbackInfo = @alignCast(@ptrCast(_info));
    info.vtable.ret = node.?;
    info.task.run();
    // We don't care about the return code because the next driver will be completely different
    // ACPI driver author is in charge of making sure that errors across drivers are handled properly
    return uacpi.UACPI_ITERATION_DECISION_CONTINUE;
}

pub fn get_table(T: type, sig: [*c]const u8) ?*const T {
    var tbl: uacpi.uacpi_table = undefined;
    if (uacpi.uacpi_table_find_by_signature(sig, &tbl) != uacpi.UACPI_STATUS_OK) {
        return null;
    }
    return @ptrCast(tbl.unnamed_0.hdr);
}

pub fn Iterator(T: type) type {
    return struct {
        id: u8,
        end: usize,
        cur: *const uacpi.acpi_entry_hdr,

        pub fn create(id: u8, table: *const uacpi.acpi_sdt_hdr, tbl_len: usize) @This() {
            return .{
                .id = id,
                .end = @intFromPtr(table) + table.length,
                .cur = @ptrFromInt(@intFromPtr(table) + tbl_len),
            };
        }

        pub fn next(self: *@This()) ?*const T {
            while (@intFromPtr(self.cur) < self.end) {
                const cur = self.cur;
                self.cur = @ptrFromInt(@intFromPtr(self.cur) + cur.length);
                if (cur.type == self.id) return @ptrCast(cur);
            }
            return null;
        }
    };
}
