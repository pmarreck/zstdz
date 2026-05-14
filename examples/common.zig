const std = @import("std");

pub fn readFile(io: std.Io, allocator: std.mem.Allocator, filename: []const u8) ![]u8 {
    const file = try std.Io.Dir.cwd().openFile(io, filename, .{});
    defer file.close(io);

    const stat = try file.stat(io);
    if (stat.size > 1024 * 1024 * 1024) return error.FileTooLarge;

    const size: usize = @intCast(stat.size);
    const buf = try allocator.alloc(u8, size);
    errdefer allocator.free(buf);

    var read_buf: [4096]u8 = undefined;
    var file_reader = file.reader(io, &read_buf);
    try file_reader.interface.readSliceAll(buf);
    return buf;
}

pub fn writeFile(io: std.Io, filename: []const u8, data: []const u8) !void {
    const file = try std.Io.Dir.cwd().createFile(io, filename, .{});
    defer file.close(io);
    try file.writeStreamingAll(io, data);
}

pub fn createOutFilename(allocator: std.mem.Allocator, filename: []const u8) ![]u8 {
    return std.fmt.allocPrint(allocator, "{s}.zst", .{filename});
}
