const std = @import("std");
const zstd = @import("zstd");
const common = @import("common.zig");

pub fn main(init: std.process.Init) !void {
    const allocator = std.heap.c_allocator;
    const io = init.io;
    const args = try init.minimal.args.toSlice(init.arena.allocator());

    if (args.len != 2) {
        std.debug.print("wrong arguments\nusage:\n{s} FILE\n", .{args[0]});
        return;
    }

    const input_filename = args[1];

    const fin = try std.Io.Dir.cwd().openFile(io, input_filename, .{});
    defer fin.close(io);

    const buffInSize = zstd.c.ZSTD_DStreamInSize();
    const buffIn = try allocator.alloc(u8, buffInSize);
    defer allocator.free(buffIn);

    const buffOutSize = zstd.c.ZSTD_DStreamOutSize();
    const buffOut = try allocator.alloc(u8, buffOutSize);
    defer allocator.free(buffOut);

    const dctx = zstd.c.ZSTD_createDCtx();
    if (dctx == null) return error.ZstdCreateDCtxFailed;
    defer _ = zstd.c.ZSTD_freeDCtx(dctx);

    while (true) {
        const read = try fin.readStreaming(io, &.{buffIn});
        if (read == 0) break;

        var input = zstd.c.ZSTD_inBuffer{
            .src = buffIn.ptr,
            .size = read,
            .pos = 0,
        };

        while (input.pos < input.size) {
            var output = zstd.c.ZSTD_outBuffer{
                .dst = buffOut.ptr,
                .size = buffOutSize,
                .pos = 0,
            };

            const ret = zstd.c.ZSTD_decompressStream(dctx, &output, &input);
            if (zstd.c.ZSTD_isError(ret) != 0) {
                std.debug.print("Decompression error: {s}\n", .{zstd.c.ZSTD_getErrorName(ret)});
                return error.DecompressionFailed;
            }

            std.debug.print("{s}", .{buffOut[0..output.pos]});
        }
    }
}
