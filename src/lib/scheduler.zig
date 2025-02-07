const kernel = @import("kernel");
const idt = kernel.drivers.idt;
const mem = kernel.lib.mem;
const cpu = kernel.drivers.cpu;
const smp = kernel.drivers.smp;
const std = @import("std");
const log = std.log.scoped(.sched);

const QUANTUM = 50;

pub var Task = kernel.Task{
    .name = "Scheduler",
    .init = init,
    .dependencies = &.{
        .{ .task = &kernel.lib.mem.KHeapTask },
        .{ .task = &kernel.drivers.smp.Task },
        .{ .task = &kernel.drivers.timers.Task },
    },
};

pub const Thread = struct {
    id: usize = 0,
    priority: u8,
    ef: cpu.Status,
    mapper: mem.Mapper,
    stack: ?[]u8,
    status: enum {
        ready,
        sleeping,
        dead,
    } = .ready,
};

const ThreadLinkedList = std.DoublyLinkedList(Thread);
const ThreadPriorityQueue = std.PriorityQueue(ThreadLinkedList, void, compareThreadPriorities);

var sched_vec: *idt.HandlerData = undefined;
var id_counter = std.atomic.Value(usize).init(0);
var lock = std.atomic.Value(bool).init(false);
var global_queue: ThreadPriorityQueue = undefined;
var delete_proc: ?*Thread = null;

fn init() kernel.Task.Ret {
    sched_vec = idt.allocate_handler(null);
    sched_vec.handler = schedule;
    global_queue = ThreadPriorityQueue.init(mem.kheap.allocator(), {});

    const kproc = Thread{
        .ef = undefined,
        .mapper = mem.kmapper,
        .stack = null,
        .priority = 100,
    };
    smp.cpu_info(null).cur_proc = enqueue(kproc);
    kernel.drivers.timers.callback(50_000_000, null, schedule) catch return .failed;
    return .success;
}

fn enqueue(thread: Thread) *Thread {
    const node = mem.kheap.allocator().create(ThreadLinkedList.Node) catch @panic("OOM");
    node.data = thread;
    node.data.id = id_counter.fetchAdd(1, .monotonic);
    while (lock.cmpxchgWeak(false, true, .acquire, .monotonic) != null) {}

    for (global_queue.items) |*queue| {
        if (queue.first.?.data.priority == thread.priority) {
            queue.append(node);
            return &node.data;
        }
    }
    var queue = ThreadLinkedList{};
    queue.append(node);
    global_queue.add(queue) catch @panic("OOM");

    lock.store(false, .release);
    return &node.data;
}

fn check_cur_thread(cpu_info: *smp.CPU) void {
    if (cpu_info.cur_proc) |cp| {
        if (cp.status == .dead) {
            // Handle dead thread
            delete_proc = cp;
            cpu_info.cur_proc = null;
        } else if (cp.status == .sleeping) {
            // Handle sleeping thread
            @panic("Handling sleeping threads unimplemented");
        }
    }
}

// Checks if any threads are ready to be scheduled to know whether to preempt current one
fn check_ready_threads(cpu_info: *smp.CPU) ?*Thread {
    if (cpu_info.cur_proc) |cp| {
        if (global_queue.count() > 0) {
            const pq = &global_queue.items[0];
            // SCHED_RR, account for timeslice/quantum
            if (pq.first.?.data.priority > cp.priority) {
                return &pq.popFirst().?.data;
            }
        }
    } else {
        if (global_queue.count() == 0) return null;
        const proc = global_queue.items[0].popFirst().?;
        if (global_queue.items[0].len == 0) _ = global_queue.remove();
        return &proc.data;
    }
    return null;
}

pub fn schedule(ctx: ?*anyopaque, status: *const cpu.Status) *const cpu.Status {
    _ = ctx;
    const cpu_info = smp.cpu_info(null);
    const alloc = mem.kheap.allocator();

    while (lock.cmpxchgWeak(false, true, .acquire, .monotonic) != null) {}
    defer lock.store(false, .release);

    if (delete_proc) |dp| {
        delete_proc = null;
        if (dp.stack) |stack| alloc.free(stack);
        alloc.destroy(dp);
    }

    check_cur_thread(cpu_info);
    const next_thread = check_ready_threads(cpu_info);

    if (cpu_info.cur_proc != null and next_thread != null) {
        // Preempt current process and switch to next thread
    } else if (cpu_info.cur_proc != null) {
        return status;
    } else if (next_thread) |nt| {
        cpu_info.cur_proc = nt;
        return &nt.ef;
    }

    @panic("idle_thread not implemented");
}

fn compareThreadPriorities(_: void, a: ThreadLinkedList, b: ThreadLinkedList) std.math.Order {
    // Highest priority first, lowest priority last
    return std.math.order(b.first.?.data.priority, a.first.?.data.priority);
}
