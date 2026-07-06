const std = @import("std");
const zstd = @import("zstd");
const common = @import("common.zig");

/// Regression fence for corrupt-input handling: every file argument must be
/// rejected with a clean ZSTD error code — never a crash/signal (SIGILL seen
/// 2026-07-06 when an ISA-mismatched build trapped inside HUF table parsing).
/// Uses the streaming API so a corrupt header can't induce a huge allocation.
pub fn main(init: std.process.Init) !void {
    const allocator = std.heap.c_allocator;
    const io = init.io;
    const args = try init.minimal.args.toSlice(init.arena.allocator());

    if (args.len < 2) {
        std.debug.print("wrong arguments\nusage:\n{s} CORRUPT_FILE...\n", .{args[0]});
        return error.WrongArguments;
    }

    const dctx = zstd.c.ZSTD_createDCtx() orelse return error.OutOfMemory;
    defer _ = zstd.c.ZSTD_freeDCtx(dctx);

    const out_buf = try allocator.alloc(u8, zstd.c.ZSTD_DStreamOutSize());
    defer allocator.free(out_buf);

    var clean_decodes: usize = 0;

    for (args[1..]) |input_filename| {
        const input_data = try common.readFile(io, allocator, input_filename);
        defer allocator.free(input_data);

        _ = zstd.c.ZSTD_DCtx_reset(dctx, zstd.c.ZSTD_reset_session_and_parameters);

        var in = zstd.c.ZSTD_inBuffer{ .src = input_data.ptr, .size = input_data.len, .pos = 0 };
        var got_error = false;
        var frame_done = false;

        while (in.pos < in.size and !frame_done) {
            var out = zstd.c.ZSTD_outBuffer{ .dst = out_buf.ptr, .size = out_buf.len, .pos = 0 };
            const ret = zstd.c.ZSTD_decompressStream(dctx, &out, &in);
            if (zstd.c.ZSTD_isError(ret) != 0) {
                got_error = true;
                break;
            }
            if (ret == 0) frame_done = true;
        }

        if (got_error) {
            std.debug.print("{s}: rejected with clean error (good)\n", .{input_filename});
        } else {
            std.debug.print("{s}: DECODED WITHOUT ERROR — expected corrupt input to be rejected!\n", .{input_filename});
            clean_decodes += 1;
        }
    }

    if (clean_decodes != 0) return error.CorruptInputAccepted;
    std.debug.print("All {d} corrupt inputs rejected cleanly.\n", .{args.len - 1});
}
