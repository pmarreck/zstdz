pub const c = @cImport({
    @cDefine("ZSTD_STATIC_LINKING_ONLY", "");
    @cInclude("zstd.h");
    @cInclude("zdict.h");
    @cInclude("zstd_errors.h");
});
