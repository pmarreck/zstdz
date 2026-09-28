# In-stream corruption-location telemetry

Experimental, opt-in API requested by Peter for validate. Existing `ZSTD_*`
functions, error codes, and decoder context layout are unchanged. The addition
is a separate C translation unit and header. It does not retain the compressed
stream or require replay.

## Integration

Include `zstdz_errors.h`, or use `@import("zstd").c` from Zig.

1. Create a tracker with `ZSTDz_createDStream()`; check for allocation failure.
2. If needed, configure the borrowed context from `ZSTDz_getDCtx()` before
   decoding: load a dictionary or set the maximum window size with upstream
   functions. Do not free that context or decode directly through it.
3. Replace streaming decode calls with `ZSTDz_decompressStream(tracker, &out, &in)`.
   Buffer conventions are upstream's: preserve unconsumed input, use `out.pos`,
   continue while input remains, and treat zero as a completed frame. Positive
   hints and per-call progress may differ because the wrapper limits calls to
   structural boundaries.
4. On `ZSTD_isError(result)`, read `ZSTDz_getErrorDetail(tracker)` before reset/free.
5. At EOF, drain any pending output with normal streaming calls, then call
   `ZSTDz_finishDStream()`. It detects unfinished frames; empty input is truncated.
6. Free with `ZSTDz_freeDStream()`. To reuse, `ZSTDz_resetDStream()` clears offsets
   and telemetry and applies upstream's session-only reset, preserving settings.

For example, the decode/error part of a Zig input loop becomes:

```zig
const c = @import("zstd").c;
const result = c.ZSTDz_decompressStream(tracker, &output, &input);
if (c.ZSTD_isError(result) != 0) {
    const detail = c.ZSTDz_getErrorDetail(tracker).*;
    // Inspect detail.kind before labeling a resource failure as corruption.
    // detail.input_begin .. detail.input_end is a half-open detection range.
}
```

Decoder failures remain sticky until reset. The detail pointer is owned by the
tracker. All offsets are 64-bit and relative to the first input byte after
creation/reset, independent of input chunk boundaries. For ZIP method 93,
validate can add the entry's compressed-data start to present a file offset.
Frame indexes are zero-based and include skippable frames. Block indexes restart
per frame and use `ZSTDZ_UNKNOWN_INDEX` before the first block.

| Field | Meaning |
| --- | --- |
| `code`, `kind`, `stage` | Upstream error category, detection kind, and structural unit |
| `frame_index`, `frame_offset`, `block_index`, `block_type` | Structural position; block type is raw=0, RLE=1, compressed=2, reserved=3, unknown=`UINT32_MAX` |
| `input_begin`, `input_end` | Half-open detection interval within the current frame header, block header, block payload, checksum, or skippable payload |
| `detected_offset` | End of the inspected unit or supplied prefix; EOF for truncation. This is a bound, not the damaged-byte index |
| `suspect_begin`, `suspect_end` | Conservative possible-cause range from frame start through detection |
| `decoded_bytes` | Decoder output counted before the failing call, across frames; a lower bound at a failed block |
| `values_present`, `expected`, `actual` | Stored/computed checksum, advertised/decoded size, required/supplied dictionary ID, or limit/observed size |

Decoded output is not necessarily good output. A checksum failure can implicate
any part of its frame, including the stored checksum. Earlier entropy tables
and match history can also cause a later block to fail. Checksum values are the
low 32 bits of XXH64, as specified in the
[upstream format](https://github.com/facebook/zstd/blob/01b7154f1172432f8abe9b3bb9909e14a1176b7d/doc/zstd_compression_format.md#content_checksum).

`ZSTDz_diagnose()` is an optional whole-buffer convenience function over this
same tracker. It takes compressed bytes, an optional dictionary, a window limit,
and an output detail struct. It returns zero or a `ZSTD_isError`-compatible code.
The upstream minimum allocation limit is 1 KiB; smaller requested byte limits
are checked additionally. Its extra output buffer is 128 KiB. It exists for
callers that already have a complete buffer; validate can use the streaming API.

## Scope and design

Implemented: standard framed input, concatenation, skippable frames,
raw/RLE/compressed block boundaries, truncation, checksum values, content-size
mismatch, block-size limits, dictionary-ID mismatch, and decoded-output counts.
A window-limit or allocation failure is a resource failure, not corruption.

Internal literals/Huffman/FSE/sequence failures report the whole compressed
block payload. There is no exact internal section or bit attribution. Legacy
frames and magicless streams are outside this prototype; magicless mode is
explicitly rejected. Default decoding behavior, dictionary loading, and window
limits are the supported configuration contract. Other experimental upstream
settings have not been validated. This API is intended for commit-pinned users.

`ZSTD_decompressStream` has early error returns before publishing input/output
cursor updates. Its whole-frame shortcut and buffered paths also consume data
differently. The wrapper caps each call at a structural boundary and records
acknowledged input consumption. It keeps at most 18 framing bytes in addition
to counters, metadata, and one ordinary upstream context. Upstream still owns
history and output buffering. Original decoder functions do not call the new
code, so ordinary consumers do not enable any tracking.

There are two explicit checks for differences exposed by this chunking: reject
zero-length compressed blocks, and check advertised content size after an empty
last block. The ordinary whole-frame decoder rejects these, while its
bufferless subpath can accept them. The CLI differential caught the first
mismatch. Both have regression fixtures; no upstream function was changed.

Work remains proportional to compressed input and decoded output. Memory follows
upstream window/buffer requirements plus constant tracker storage. This is not
an output-expansion or CPU-work limit.

Fine-grained internal telemetry remains feasible but would need hooks in
`ZSTD_decodeLiteralsBlock`, Huffman tree readers, `ZSTD_decodeSeqHeaders`, FSE
table readers, and every short/split/long sequence execution variant. Backward
bit readers and state reused across blocks require detection ranges rather
than damaged-byte claims. That deeper instrumentation is deferred.

## Verification

The C API tests run under `zig build test`. They cover known structural ranges,
every truncation of a small frame, 64 input/output chunk combinations, one-byte
input and output, concatenated/skippable frames, checksum values, dictionary IDs,
size failures, EOF, sticky failures, reset, a 1 MiB decode across window wraps,
and every high-bit byte mutation of a compressed 32 KiB payload. Valid output is
compared byte-for-byte with its original source.

The separate CLI comparison runs in two modes: whole-buffer convenience and
direct streaming with one-byte input/17-byte output buffers. Each tested 2,418
cases: 56 accepted and 2,359 rejected verdicts agreed with stock zstd; three
window-limit cases were counted separately. Other resource failures fail the
test. This covers valid and mutated raw, compressed, libarchive, empty,
concatenated, and skippable frames; their truncations; and existing corruption
fixtures. It establishes corpus verdict agreement, not exhaustive equivalence
or an independently established damaged-byte location.

ASan/UBSan passed on the new tracker and chunk tests linked against the baseline
archive; that archive itself was not instrumented. Nix's default check includes
both CLI comparisons, and the ISA check covers the extended archive.

## Measurements

Results from 2026-09-28 are in
[the benchmark record](../bench/telemetry_20260928_threadripper3990x.json).
AMD Threadripper 3990X, Linux x86_64, Zig 0.16.0 ReleaseFast with baseline CPU,
GCC 15.3.0 benchmark driver at `-O3`: 12 runs of 100 decodes, after three warmups.

| Mode | Mixed 8 MiB, wall ms ± SD | Repetitive 8.5 MiB, wall ms ± SD |
| --- | ---: | ---: |
| Before telemetry | 101.80 ± 6.88 | 218.71 ± 1.08 |
| New archive, telemetry unused | 99.45 ± 0.89 | 218.65 ± 0.82 |
| In-stream telemetry enabled | 103.66 ± 1.32 | 218.85 ± 1.01 |
| Convenience replay | 105.52 ± 1.66 | 234.60 ± 12.37 |

Enabled streaming took 4.24% longer on mixed data and 0.09% longer on repetitive
data versus the new archive with telemetry unused; the latter is within noise.
The baseline and unused-telemetry consumer executables were byte-for-byte
identical (SHA-256 `50da131710821bcfa9ad65409106561259750c1e041bcec1450864d9660d8201`).
The optional object adds no instructions to that ordinary static consumer.
This is stronger evidence for unused cost than small timing differences.

This is one shared host and two corpora, not a general throughput guarantee.
All zstdz builds finished before timing, but unrelated host activity continued;
the baseline mixed and replay repetitive results had outliers. Raw wall, user,
system times and all samples are retained in the record.

Use `./bm BASELINE_ARCHIVE CURRENT_ARCHIVE RESULTS_JSON` with matching optimized
archives. The benchmark checks equal output-byte counts for baseline, unused
telemetry, in-stream telemetry, and convenience replay before measuring them.
It records wall, user and system time over two deterministic corpora.
