const std = @import("std");
const Step = std.Build.Step;
const Options = Step.Options;

const CPU_FEATURES_ADD = std.Target.x86.featureSet(&.{
    .soft_float,
});

const CPU_FEATURES_SUB = std.Target.x86.featureSet(&.{
    .mmx,
    .sse,
    .sse2,
    .avx,
    .avx2,
});

const CPU_FEATURES_QUERY = std.Target.Query{
    .cpu_arch = .x86_64,
    .abi = .none,
    .os_tag = .freestanding,
    .cpu_features_add = CPU_FEATURES_ADD,
    .cpu_features_sub = CPU_FEATURES_SUB,
};

const ISO_COPY: [1][]const u8 = .{"limine.conf"};

const XORRISO_ARGS = .{
    "xorriso",
    "-as",
    "mkisofs",
    "-b",
    "limine-bios-cd.bin",
    "-no-emul-boot",
    "-boot-load-size",
    "4",
    "-boot-info-table",
    "--efi-boot",
    "limine-uefi-cd.bin",
    "-efi-boot-part",
    "--efi-boot-image",
    "--protective-msdos-label",
};

const QEMU_ARGS = .{
    "qemu-system-x86_64",
    "-m",
    "128M",
    "-smp",
    "2",
    "-serial",
    "stdio",
    "-no-reboot",
    "-no-shutdown",
    "-enable-kvm",
    "-cpu",
    "host,+invtsc",
};

var limineZigModule: *std.Build.Module = undefined;
var limineBinDep: *std.Build.Dependency = undefined;
var limineBuildExeStep: *Step = undefined;

var flantermModule: *std.Build.Module = undefined;
var flantermCSourceFileOptions: std.Build.Module.AddCSourceFilesOptions = undefined;

var uacpiModule: *std.Build.Module = undefined;
var uacpiCSourceFileOptions: std.Build.Module.AddCSourceFilesOptions = undefined;
var uacpiIncludePath: std.Build.LazyPath = undefined;

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(CPU_FEATURES_QUERY);
    const optimize = b.standardOptimizeOption(.{});
    initFlanterm(b, target, optimize);
    initUACPI(b, target, optimize);
    initLimine(b);

    createNoQEMUPipeline(b, target, optimize);
    createQEMUPipeline(b, target, optimize);
}

fn createNoQEMUPipeline(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) void {
    const options = b.addOptions();
    options.addOption(bool, "qemu", false);
    const kernel = getKernelStep(b, target, optimize, options); // Options implicitly added as dependency
    const iso_dir = getISODirStep(b, kernel); // Implicitly add kernel as dependency
    const iso_out = getXorrisoStep(b, iso_dir.getDirectory()); // Implictly added iso_dir as dependency
    const install_iso = b.addInstallBinFile(iso_out, "os.iso");
    install_iso.step.dependOn(getLimineBiosStep(b, iso_out));

    b.getInstallStep().dependOn(&b.addInstallArtifact(kernel, .{}).step);
    b.getInstallStep().dependOn(&install_iso.step);
}

fn createQEMUPipeline(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) void {
    const options = b.addOptions();
    options.addOption(bool, "qemu", true);
    const kernel = getKernelStep(b, target, optimize, options);
    const iso_dir = getISODirStep(b, kernel);
    const iso_out = getXorrisoStep(b, iso_dir.getDirectory());
    const install_iso = b.addInstallBinFile(iso_out, "qemu/os.iso");
    install_iso.step.dependOn(getLimineBiosStep(b, iso_out));

    const qemu = b.addSystemCommand(&QEMU_ARGS);
    qemu.step.dependOn(&install_iso.step);
    qemu.addArg("-cdrom");
    qemu.addFileArg(iso_out);
    const run_step = b.step("run", "Run with QEMU emulator");
    run_step.dependOn(&qemu.step);
    run_step.dependOn(&b.addInstallArtifact(kernel, .{ .dest_sub_path = "qemu/pivot-os" }).step);
}

fn getLimineBiosStep(b: *std.Build, iso: std.Build.LazyPath) *Step {
    const step = b.addSystemCommand(&.{ "./limine", "bios-install" });
    step.step.dependOn(limineBuildExeStep);
    step.setCwd(limineBinDep.path(""));
    step.addFileArg(iso);
    return &step.step;
}

fn getXorrisoStep(b: *std.Build, dir: std.Build.LazyPath) std.Build.LazyPath {
    const xorriso = b.addSystemCommand(&XORRISO_ARGS);
    xorriso.addDirectoryArg(dir);
    xorriso.addArg("-o");
    return xorriso.addOutputFileArg("os.iso");
}

fn getISODirStep(b: *std.Build, kernel: *Step.Compile) *Step.WriteFile {
    const wf = b.addWriteFiles();
    _ = wf.addCopyDirectory(
        limineBinDep.path(""),
        "",
        .{ .include_extensions = &.{
            "sys",
            "bin",
        } },
    );
    _ = wf.addCopyDirectory(
        limineBinDep.path(""),
        "EFI/BOOT",
        .{ .include_extensions = &.{"EFI"} },
    );
    _ = wf.addCopyFile(kernel.getEmittedBin(), "pivot-os");
    for (ISO_COPY) |f| _ = wf.addCopyFile(b.path(f), f);
    return wf;
}

fn getKernelStep(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode, options: *Options) *Step.Compile {
    const kernel = b.addExecutable(.{
        .name = "pivot-os",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
        .code_model = .kernel,
        .linkage = .static,
        .strip = true,
    });
    kernel.want_lto = false;
    kernel.setLinkerScript(b.path("linker.ld"));
    kernel.addCSourceFiles(flantermCSourceFileOptions);
    kernel.addCSourceFiles(uacpiCSourceFileOptions);
    kernel.addIncludePath(uacpiIncludePath);

    kernel.root_module.addImport("config", options.createModule());
    kernel.root_module.addImport("limine", limineZigModule);
    kernel.root_module.addImport("flanterm", flantermModule);
    kernel.root_module.addImport("uacpi", uacpiModule);
    kernel.root_module.addImport("kernel", &kernel.root_module);
    return kernel;
}

fn initUACPI(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) void {
    const uacpi = b.dependency("uacpi", .{});
    const translateC = b.addTranslateC(.{
        .link_libc = false,
        .optimize = optimize,
        .target = target,
        .root_source_file = b.path("src/drivers/uacpi.h"),
    });
    uacpiIncludePath = uacpi.path("include");
    translateC.addIncludeDir(uacpiIncludePath.getPath(b)); // This is a hack. Specifically states that should only be called during make phase
    uacpiModule = translateC.createModule();
    uacpiCSourceFileOptions = .{
        .root = uacpi.path("source"),
        .files = &.{
            "default_handlers.c",
            "event.c",
            "interpreter.c",
            "io.c",
            "mutex.c",
            "namespace.c",
            "notify.c",
            "opcodes.c",
            "opregion.c",
            "osi.c",
            "registers.c",
            "resources.c",
            "shareable.c",
            "sleep.c",
            "stdlib.c",
            "tables.c",
            "types.c",
            "uacpi.c",
            "utilities.c",
        },
        .flags = &.{"-DUACPI_SIZED_FREES"},
    };
}

fn initFlanterm(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) void {
    const flanterm = b.dependency("flanterm", .{});
    const translateC = b.addTranslateC(.{
        .link_libc = false,
        .optimize = optimize,
        .target = target,
        .root_source_file = flanterm.path("backends/fb.h"),
    });
    flantermModule = translateC.createModule();
    flantermCSourceFileOptions = .{
        .root = flanterm.path(""),
        .files = &.{ "flanterm.c", "backends/fb.c" },
        .flags = &.{"-DFLANTERM_FB_DISABLE_BUMP_ALLOC"},
    };
}

fn initLimine(b: *std.Build) void {
    limineZigModule = b.dependency("limine_zig", .{}).module("limine");
    limineBinDep = b.dependency("limine_bin", .{});

    const buildLimineExe = b.addSystemCommand(&.{"make"});
    buildLimineExe.setCwd(limineBinDep.path(""));
    limineBuildExeStep = &buildLimineExe.step;
}

// const ChmodExe = struct {
//     path: std.Build.LazyPath,
//     step: Step,

//     pub fn create(b: *std.Build, path: std.Build.LazyPath) *@This() {
//         const s = b.allocator.create(@This()) catch @panic("OOM");
//         s.* = .{
//             .path = path,
//             .step = Step.init(.{
//                 .id = .custom,
//                 .owner = b,
//                 .name = "chmod",
//                 .makeFn = make,
//             }),
//         };
//     }
// };

// fn make(step: *Step, prog_node: std.Progress.Node) !void {
//     const parent: *ChmodExe = @fieldParentPtr("step", step);
//     const path = parent.path.getPath2(step.owner, step);
//     const file = std.fs.openFileAbsolute(path, .{}) catch @panic("Could not open file");
// }
