const std = @import("std");
const zstd = @import("zstd");
const common = @import("common.zig");

pub fn main(init: std.process.Init) !void {
    const allocator = std.heap.c_allocator;
    const io = init.io;
    const args = try init.minimal.args.toSlice(init.arena.allocator());

    if (args.len < 2) {
        std.debug.print("wrong arguments\nusage:\n{s} FILE(s)\n", .{args[0]});
        return;
    }

    // Pre-calculate max file size and max filename length
    var max_file_size: usize = 0;

    var i: usize = 1;
    while (i < args.len) : (i += 1) {
        const file = try std.Io.Dir.cwd().openFile(io, args[i], .{});
        const stat = try file.stat(io);
        file.close(io);

        if (stat.size > max_file_size) max_file_size = @intCast(stat.size);
    }

    const c_buffer_size = zstd.c.ZSTD_compressBound(max_file_size);
    const f_buffer = try allocator.alloc(u8, max_file_size);
    defer allocator.free(f_buffer);

    const c_buffer = try allocator.alloc(u8, c_buffer_size);
    defer allocator.free(c_buffer);

    const cctx = zstd.c.ZSTD_createCCtx();
    if (cctx == null) {
        std.debug.print("zstd.eateCCtx() failed!\n", .{});
        return;
    }
    defer _ = zstd.c.ZSTD_freeCCtx(cctx);

    i = 1;
    while (i < args.len) : (i += 1) {
        const input_filename = args[i];

        // Load file into shared buffer
        const file = try std.Io.Dir.cwd().openFile(io, input_filename, .{});
        const stat = try file.stat(io);
        const f_size: usize = @intCast(stat.size);
        var read_buf: [4096]u8 = undefined;
        var file_reader = file.reader(io, &read_buf);
        try file_reader.interface.readSliceAll(f_buffer[0..f_size]);
        file.close(io);

        // Compress
        const cSize = zstd.c.ZSTD_compressCCtx(cctx, c_buffer.ptr, c_buffer_size, f_buffer.ptr, f_size, 1);
        if (zstd.c.ZSTD_isError(cSize) != 0) {
            std.debug.print("error compressing: {s}\n", .{zstd.c.ZSTD_getErrorName(cSize)});
            continue;
        }

        const out_filename = try common.createOutFilename(allocator, input_filename);
        defer allocator.free(out_filename);
        try common.writeFile(io, out_filename, c_buffer[0..cSize]);

        std.debug.print("{s} : {d} -> {d} - {s} \n", .{
            input_filename,
            f_size,
            cSize,
            out_filename,
        });
    }

    std.debug.print("compressed {d} files \n", .{args.len - 1});
}
