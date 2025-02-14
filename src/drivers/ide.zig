const kernel = @import("kernel");
const uacpi = @import("uacpi");
const pci = kernel.drivers.pci;
const acpi = kernel.drivers.acpi;
const serial = kernel.drivers.serial;
const log = @import("std").log.scoped(.ide);
const mem = kernel.lib.mem;
const timers = kernel.drivers.timers;

pub var PCIVTable = pci.VTable{
    .target_codes = &.{
        .{ .class_code = 0x1, .subclass_code = 0x1 },
    },
    .target_ret = undefined,
};

pub var PCITask = kernel.Task{
    .name = "IDE PCI Driver",
    .init = init,
    .dependencies = &.{
        .{ .task = &kernel.drivers.timers.Task },
    },
};

// TODO: Change into enums?
const ATA_PRIMARY = 0;
const ATA_SECONDARY = 1;

const ATA_REG_CONTROL = 0xC;
const ATA_REG_HDDEVSEL = 0x6;
const ATA_REG_COMMAND = 0x7;
const ATA_REG_STATUS = 0x7;
const ATA_REG_LBA1 = 4;
const ATA_REG_LBA2 = 5;
const ATA_REG_DATA = 0;

const ATA_CMD_IDENTIFY = 0xEC;
const ATA_CMD_IDENTIFY_PACKET = 0xA0;

const ATA_SR_ERR = 1;
const ATA_SR_BSY = 0x80;
const ATA_SR_DRQ = 8;

const Drive = struct {
    pub const Type = enum {
        ATA,
        ATAPI,
    };

    dtype: Type,
};

const Controller = struct {
    channels: [2]ChannelRegisters,
    drives: []Drive,
};

const ChannelRegisters = struct {
    base: u16,
    ctrl: u16,
    bmide: u16,
    nIEN: u8 = 0,

    pub fn read(self: @This(), reg: u8) u8 {
        var result: u8 = undefined;
        if (reg > 7 and reg < 0xC) {
            self.write(ATA_REG_CONTROL, 0x80 | self.nIEN);
        }
        if (reg < 8) {
            result = serial.in(self.base + reg, u8);
        } else if (reg < 0xC) {
            result = serial.in(self.base + reg - 6, u8);
        } else if (reg < 0xE) {
            result = serial.in(self.ctrl + reg - 0xA, u8);
        } else if (reg < 0x16) {
            result = serial.in(self.bmide + reg - 0xE, u8);
        }
        if (reg > 7 and reg < 0xC) self.write(ATA_REG_CONTROL, self.nIEN);

        return result;
    }

    pub fn write(self: @This(), reg: u8, data: u8) void {
        if (reg > 7 and reg < 0xC) {
            self.write(ATA_REG_CONTROL, 0x80 | self.nIEN);
        }
        if (reg < 8) {
            serial.out(self.base + reg, data);
        } else if (reg < 0xC) {
            serial.out(self.base + reg - 6, data);
        } else if (reg < 0xE) {
            serial.out(self.ctrl + reg - 0xA, data);
        } else if (reg < 0x16) {
            serial.out(self.bmide + reg - 0xE, data);
        }
        if (reg > 7 and reg < 0xC) self.write(ATA_REG_CONTROL, self.nIEN);
    }

    pub fn read_buf(self: @This(), reg: u8, size: usize) void {
        const buf = mem.kheap.allocator().alloc(u8, size) catch @panic("OOM");
        const ptr = buf.ptr;
        _ = self;
        _ = reg;
        _ = ptr;
    }
};

const Device = packed struct {
    exists: u8,
    channel: u8,
    drive: u8,
    dtype: u16,
    sig: u16,
    cap: u16,
    cmdsets: u32,
    size: u32,
    model: [41]u8,
};

fn init() kernel.Task.Ret {
    const bmide = get_bmide(PCIVTable.target_ret.prog_if, PCIVTable.target_ret.addr);
    const channels = mem.kheap.allocator().alloc(ChannelRegisters, 2) catch return .failed;
    channels[ATA_PRIMARY] = .{
        .base = get_primary_channel(PCIVTable.target_ret.prog_if, PCIVTable.target_ret.addr),
        .ctrl = get_primary_channel_control_port(PCIVTable.target_ret.prog_if, PCIVTable.target_ret.addr),
        .bmide = bmide,
    };
    channels[ATA_SECONDARY] = .{
        .base = get_secondary_channel(PCIVTable.target_ret.prog_if, PCIVTable.target_ret.addr),
        .ctrl = get_secondary_channel_control_port(PCIVTable.target_ret.prog_if, PCIVTable.target_ret.addr),
        .bmide = bmide + 8,
    };

    channels[ATA_PRIMARY].write(ATA_REG_CONTROL, 2);
    channels[ATA_SECONDARY].write(ATA_REG_CONTROL, 2);

    for (channels) |channel| {
        for (0..2) |drive| {
            channel.write(ATA_REG_HDDEVSEL, @intCast((drive << 4) | 0xA));
            timers.sleep(1_000_000);
            channel.write(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

            if (channel.read(ATA_REG_STATUS) == 0) continue;

            var status: u8 = undefined;
            while (true) {
                status = channel.read(ATA_REG_STATUS);
                if (status & ATA_SR_ERR > 0 or (status & ATA_SR_BSY == 0 and status & ATA_SR_DRQ > 0)) break;
            }

            var drive_type = Drive.Type.ATA;
            if (status & ATA_SR_ERR > 0) {
                // Check if ATAPI
                const cl = channel.read(ATA_REG_LBA1);
                const ch = channel.read(ATA_REG_LBA2);

                if ((cl == 0x14 and ch == 0xEB) and (cl == 0x69 and ch == 0x96)) {
                    drive_type = .ATAPI;
                } else continue;

                channel.write(ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
            }

            channel.read_buf(ATA_REG_DATA, 512);
        }
    }

    return .success;
}

inline fn get_primary_channel(prog_if: u8, addr: uacpi.uacpi_pci_address) u16 {
    return if (prog_if & 1 == 0) 0x1F0 else get_bar(addr, 0x10);
    // TODO: Use bit 1 to modify pci native/compatibility
}

inline fn get_primary_channel_control_port(prog_if: u8, addr: uacpi.uacpi_pci_address) u16 {
    return if (prog_if & 1 == 0) 0x3F6 else get_bar(addr, 0x14);
    // TODO: Use bit 1 to modify pci native/compatibility
}

inline fn get_secondary_channel(prog_if: u8, addr: uacpi.uacpi_pci_address) u16 {
    return if (prog_if & (1 << 2) == 0) 0x170 else get_bar(addr, 0x18);
}

inline fn get_secondary_channel_control_port(prog_if: u8, addr: uacpi.uacpi_pci_address) u16 {
    return if (prog_if & (1 << 2) == 0) 0x376 else get_bar(addr, 0x1C);
}

inline fn get_bmide(prog_if: u8, addr: uacpi.uacpi_pci_address) u16 {
    return if (prog_if & (1 << 7) == 0) 0 else get_bar(addr, 0x20);
}

fn get_bar(addr: uacpi.uacpi_pci_address, off: u8) u16 {
    const bar = pci.read_reg(addr, off);
    if (bar & 1 == 0) @panic("IDE Bar is in physical memory");
    return @intCast(bar & 0xFFFC);
}
