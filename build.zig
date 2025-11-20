const std = @import("std");

const SynetArch = enum { x86, x64 };
const SynetConf = enum { Debug, Release, Ship };
const SynetWinDivertSign = enum { A, B, C };

pub fn build(b: *std.Build) void {
    const arch = b.option(SynetArch, "arch", "x86, x64") orelse .x64;
    const conf = b.option(SynetConf, "conf", "Debug, Release, Ship") orelse .Debug;
    const windivert_sign = b.option(SynetWinDivertSign, "sign", "A, B, C") orelse .A;
    const windows_kit_bin_root = b.option([]const u8, "windows_kit_bin_root", "Windows SDK Bin root") orelse "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0";

    const arch_tag = @tagName(arch);
    const conf_tag = @tagName(conf);
    const sign_tag = @tagName(windivert_sign);
    const windivert_dir = b.fmt("WinDivert-2.2.0-{s}", .{sign_tag});

    std.debug.print("- arch: {s}, conf: {s}, sign: {s}\n", .{ arch_tag, conf_tag, sign_tag });
    std.debug.print("- windows_kit_bin_root: {s}\n", .{windows_kit_bin_root});

    const prefix = b.fmt("{s}_{s}_{s}", .{ arch_tag, conf_tag, sign_tag });

    std.debug.print("- out: zig-out/{s}\n", .{prefix});

    const tmp_path = b.fmt("tmp/{s}", .{prefix});
    std.fs.cwd().makePath(tmp_path) catch @panic("unable to create tmp directory");

    // Install WinDivert files
    b.installFile(b.fmt("external/{s}/{s}/WinDivert.dll", .{ windivert_dir, arch_tag }), b.fmt("{s}/WinDivert.dll", .{prefix}));
    switch (arch) {
        .x64 => b.installFile(b.fmt("external/{s}/{s}/WinDivert64.sys", .{ windivert_dir, arch_tag }), b.fmt("{s}/WinDivert64.sys", .{prefix})),
        .x86 => b.installFile(b.fmt("external/{s}/{s}/WinDivert32.sys", .{ windivert_dir, arch_tag }), b.fmt("{s}/WinDivert32.sys", .{prefix})),
    }

    b.installFile("etc/config.txt", b.fmt("{s}/config.txt", .{prefix}));
    if (conf == .Ship)
        b.installFile("LICENSE", b.fmt("{s}/License.txt", .{prefix}));

    const res_obj_path = b.fmt("{s}/synet_res.obj", .{tmp_path});

    // Find rc.exe
    const rc_exe = b.findProgram(&.{"rc"}, &.{
        b.pathJoin(&.{ windows_kit_bin_root, arch_tag }),
    }) catch @panic("unable to find `rc.exe`, check your windows_kit_bin_root");

    const archFlag = switch (arch) {
        .x86 => "X86",
        .x64 => "X64",
    };

    // Resource compilation
    const rc_cmd = b.addSystemCommand(&.{
        rc_exe,
        "/nologo",
        "/d",
        "NDEBUG",
        "/d",
        archFlag,
        "/r",
        "/fo",
        res_obj_path,
        "etc/clumsy.rc",
    });

    // Set up target query
    const target_query: std.Target.Query = .{
        .cpu_arch = switch (arch) {
            .x64 => .x86_64,
            .x86 => .x86,
        },
        .os_tag = .windows,
        .abi = .gnu,
    };

    // Set up optimization
    const optimize: std.builtin.OptimizeMode = switch (conf) {
        .Debug => .Debug,
        .Release => .ReleaseSafe,
        .Ship => .ReleaseFast,
    };

    // Create executable
    const exe = b.addExecutable(.{
        .name = "synet",
        .root_module = b.createModule(.{
            .root_source_file = null,
            .target = b.resolveTargetQuery(target_query),
            .optimize = optimize,
        }),
    });

    // Set subsystem
    exe.subsystem = switch (conf) {
        .Debug => .Console,
        .Release, .Ship => .Windows,
    };

    // Add resource object
    exe.step.dependOn(&rc_cmd.step);
    exe.addObjectFile(.{ .cwd_relative = res_obj_path });

    // Add C source files
    const c_flags = &.{""};
    exe.addCSourceFile(.{ .file = .{ .cwd_relative = "src/auth.c" }, .flags = c_flags });
    exe.addCSourceFile(.{ .file = .{ .cwd_relative = "src/divert.c" }, .flags = c_flags });
    exe.addCSourceFile(.{ .file = .{ .cwd_relative = "src/elevate.c" }, .flags = c_flags });
    exe.addCSourceFile(.{ .file = .{ .cwd_relative = "src/lag.c" }, .flags = c_flags });
    exe.addCSourceFile(.{ .file = .{ .cwd_relative = "src/main.c" }, .flags = c_flags });
    exe.addCSourceFile(.{ .file = .{ .cwd_relative = "src/packet.c" }, .flags = c_flags });
    exe.addCSourceFile(.{ .file = .{ .cwd_relative = "src/utils.c" }, .flags = c_flags });

    if (arch == .x86)
        exe.addCSourceFile(.{ .file = .{ .cwd_relative = "etc/chkstk.s" }, .flags = c_flags });

    // Add include directories
    exe.addIncludePath(.{ .cwd_relative = b.fmt("external/{s}/include", .{windivert_dir}) });

    const iupLib = switch (arch) {
        .x64 => "external/iup-3.30_Win64_mingw6_lib",
        .x86 => "external/iup-3.30_Win32_mingw6_lib",
    };

    exe.addIncludePath(.{ .cwd_relative = b.pathJoin(&.{ iupLib, "include" }) });
    exe.addObjectFile(.{ .cwd_relative = b.pathJoin(&.{ iupLib, "libiup.a" }) });

    // Link libraries
    exe.linkLibC();
    exe.addLibraryPath(.{ .cwd_relative = b.fmt("external/{s}/{s}", .{ windivert_dir, arch_tag }) });
    exe.linkSystemLibrary("WinDivert");
    exe.linkSystemLibrary("comctl32");
    exe.linkSystemLibrary("winmm");
    exe.linkSystemLibrary("ws2_32");
    exe.linkSystemLibrary("kernel32");
    exe.linkSystemLibrary("gdi32");
    exe.linkSystemLibrary("comdlg32");
    exe.linkSystemLibrary("uuid");
    exe.linkSystemLibrary("ole32");
    exe.linkSystemLibrary("winhttp");

    // Install artifact to custom directory
    const install_exe = b.addInstallArtifact(exe, .{
        .dest_dir = .{ .override = .{ .custom = prefix } },
    });

    b.getInstallStep().dependOn(&install_exe.step);

    // Clean step
    const clean_all = b.step("clean", "purge zig-cache and zig-out");
    clean_all.dependOn(&b.addRemoveDirTree(b.path("zig-out")).step);
}
