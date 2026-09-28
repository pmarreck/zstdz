/* Experimental, opt-in zstdz diagnostics. Upstream ZSTD_* APIs are unchanged. */
#ifndef ZSTDZ_ERRORS_H
#define ZSTDZ_ERRORS_H
#include "zstd.h"
#include "zstd_errors.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ZSTDz_none, ZSTDz_decoder, ZSTDz_truncated, ZSTDz_checksum,
	ZSTDz_content_size, ZSTDz_dictionary, ZSTDz_block_size, ZSTDz_resource
} ZSTDz_DetectionKind;
typedef enum {
	ZSTDz_frame_header, ZSTDz_block_header, ZSTDz_block_payload,
	ZSTDz_frame_checksum, ZSTDz_skippable
} ZSTDz_Stage;
#define ZSTDZ_UNKNOWN_INDEX UINT64_MAX

typedef struct {
	ZSTD_ErrorCode code;
	ZSTDz_DetectionKind kind;
	ZSTDz_Stage stage;
	uint64_t frame_index, block_index;
	uint64_t frame_offset;
	/* Half-open range inspected when the error was detected, not a damaged-byte claim. */
	uint64_t input_begin, input_end;
	/* EOF for truncation; otherwise end of the inspected unit. */
	uint64_t detected_offset;
	/* Broad possible cause range: checksum failures implicate the entire frame. */
	uint64_t suspect_begin, suspect_end;
	/* Decoder output counted before the failing call; a lower bound, not proven-good data. */
	uint64_t decoded_bytes;
	/* 0 raw, 1 RLE, 2 compressed, 3 reserved; UINT32_MAX when unavailable. */
	uint32_t block_type;
	unsigned values_present;
	uint64_t expected, actual;
} ZSTDz_ErrorDetail;

typedef struct ZSTDz_DStream_s ZSTDz_DStream;
/* Each tracker owns one upstream context. No caller input is retained. */
ZSTDLIB_API ZSTDz_DStream* ZSTDz_createDStream(void);
ZSTDLIB_API void ZSTDz_freeDStream(ZSTDz_DStream* stream);
/* Reset offsets and telemetry; underlying session reset preserves parameters/dictionaries. */
ZSTDLIB_API size_t ZSTDz_resetDStream(ZSTDz_DStream* stream);
/* Borrow for configuration only, before decoding or after reset. Standard
 * framed input only; do not decode directly through this borrowed context. */
ZSTDLIB_API ZSTD_DCtx* ZSTDz_getDCtx(ZSTDz_DStream* stream);
/* Same buffer and return conventions as ZSTD_decompressStream. Positive hints
 * and per-call progress may differ because calls stop at structural boundaries.
 * Call repeatedly while input remains; retain unconsumed input as usual. */
ZSTDLIB_API size_t ZSTDz_decompressStream(ZSTDz_DStream* stream,
 ZSTD_outBuffer* output, ZSTD_inBuffer* input);
/* Signal EOF after draining output. Returns an error for an unfinished frame. */
ZSTDLIB_API size_t ZSTDz_finishDStream(ZSTDz_DStream* stream);
/* Valid until reset/free. Decoder failures are sticky until reset. */
ZSTDLIB_API const ZSTDz_ErrorDetail* ZSTDz_getErrorDetail(const ZSTDz_DStream* stream);

/* Optional convenience replay through the same tracker for complete input.
 * Returns 0 on success or a ZSTD_isError-compatible result. Never changes a caller's
 * decoder. maxWindowSize limits the advertised window (upstream's minimum
 * allocation limit is 1 KiB); scratch memory adds input/output buffers, decoder
 * context, and dictionary storage. Dictionary is optional.
 * Concatenated/skippable frames are supported; indexes include skippable frames.
 * Internal entropy sections are reported as whole block payloads. No legacy or
 * magicless format and no custom streaming parameters. Input must remain alive
 * during the call. Empty input is truncated. detail is required and reset per call.
 */
ZSTDLIB_API size_t ZSTDz_diagnose(const void* src, size_t srcSize,
	const void* dict, size_t dictSize, size_t maxWindowSize, ZSTDz_ErrorDetail* detail);
#ifdef __cplusplus
}
#endif
#endif
