/* Optional streaming diagnostics. Existing upstream decoder paths are unchanged. */
#define ZSTD_STATIC_LINKING_ONLY
#include "../zstdz_errors.h"
#include "zstd_decompress_internal.h"
#include <stdlib.h>
#include <string.h>

typedef enum { prefix, header, block_header, payload, checksum, skip } Phase;
struct ZSTDz_DStream_s {
	ZSTD_DCtx* ctx;
	ZSTDz_ErrorDetail detail;
	ZSTD_FrameHeader frame;
	Phase phase;
	BYTE small[ZSTD_FRAMEHEADERSIZE_MAX];
	size_t have, remaining;
	uint64_t consumed, unit_start, frame_start, frame_index, block_index;
	uint64_t decoded, completed_output;
	U32 block_type, block_size;
	unsigned last_block, boundary;
	size_t failure;
};

static void unit(ZSTDz_DStream* s, Phase phase, size_t size)
{
	s->phase=phase; s->unit_start=s->consumed; s->remaining=size; s->have=0;
}

static ZSTDz_Stage stage(Phase p)
{
	switch(p) {
	case prefix: case header: return ZSTDz_frame_header;
	case block_header: return ZSTDz_block_header;
	case checksum: return ZSTDz_frame_checksum;
	case skip: return ZSTDz_skippable;
	case payload: return ZSTDz_block_payload;
	default: return ZSTDz_block_payload;
	}
}

/* Capture the enclosing unit before upstream's early error return loses input
 * cursor updates. The end is a detection bound, never a damaged-byte claim. */
static size_t record(ZSTDz_DStream* s, size_t error, uint64_t end, size_t available)
{
	ZSTDz_ErrorDetail* d=&s->detail;
	memset(d,0,sizeof(*d));
	d->code=ZSTD_getErrorCode(error); d->kind=ZSTDz_decoder;
	d->stage=stage(s->phase); d->frame_index=s->frame_index;
	d->frame_offset=s->frame_start;
	d->block_index=(s->phase==prefix || s->phase==header || s->phase==skip) ? ZSTDZ_UNKNOWN_INDEX : s->block_index;
	d->block_type=s->block_type;
	d->input_begin=s->unit_start; d->input_end=d->detected_offset=end;
	d->suspect_begin=s->frame_start; d->suspect_end=end; d->decoded_bytes=s->decoded;
	if(s->phase==block_header && s->have+available>=3) {
		s->block_type=d->block_type=(MEM_readLE24(s->small)>>1)&3;
		s->block_size=MEM_readLE24(s->small)>>3;
	}
	if(s->phase==header || s->phase==prefix) {
		ZSTD_FrameHeader f;
		if(ZSTD_getFrameHeader(&f,s->small,s->have+available)==0) s->frame=f;
	}
	if(d->code==ZSTD_error_checksum_wrong && s->have+available>=4) {
		d->kind=ZSTDz_checksum; d->values_present=1;
		d->expected=MEM_readLE32(s->small); d->actual=(U32)XXH64_digest(&s->ctx->xxhState);
	} else if(d->code==ZSTD_error_dictionary_wrong) {
		d->kind=ZSTDz_dictionary; d->values_present=1;
		d->expected=s->frame.dictID; d->actual=s->ctx->dictID;
	} else if(d->code==ZSTD_error_frameParameter_windowTooLarge) {
		d->kind=ZSTDz_resource; d->values_present=1;
		d->expected=s->ctx->maxWindowSize; d->actual=s->frame.windowSize;
	} else if(d->code==ZSTD_error_memory_allocation ||
	          d->code==ZSTD_error_noForwardProgress_destFull ||
	          d->code==ZSTD_error_parameter_unsupported) {
		d->kind=ZSTDz_resource;
	} else if((s->phase==block_header || s->phase==payload) && s->block_type!=3 &&
	          s->block_size>s->frame.blockSizeMax) {
		d->kind=ZSTDz_block_size; d->values_present=1;
		d->expected=s->frame.blockSizeMax; d->actual=s->block_size;
	}
	s->failure=error;
	return error;
}

static size_t content_size_error(ZSTDz_DStream* s, size_t error, uint64_t end)
{
	record(s,error,end,0);
	s->detail.kind=ZSTDz_content_size; s->detail.values_present=1;
	s->detail.expected=s->frame.frameContentSize; s->detail.actual=s->ctx->decodedSize;
	return error;
}

static void end_frame(ZSTDz_DStream* s)
{
	if(s->frame.frameType!=ZSTD_skippableFrame) s->completed_output+=s->ctx->decodedSize;
	s->decoded=s->completed_output;
	s->boundary=1;
}

static size_t end_block(ZSTDz_DStream* s)
{
	if(s->last_block) {
		if(s->frame.frameContentSize!=ZSTD_CONTENTSIZE_UNKNOWN && s->ctx->decodedSize!=s->frame.frameContentSize)
			return content_size_error(s,ERROR(corruption_detected),s->consumed);
		if(s->frame.checksumFlag) unit(s,checksum,4);
		else end_frame(s);
	} else {
		++s->block_index;
		s->block_type=UINT32_MAX;
		unit(s,block_header,3);
	}
	return 0;
}

/* Advance framing only for bytes upstream has acknowledged as consumed. */
static size_t advance(ZSTDz_DStream* s)
{
	size_t result;
	switch(s->phase) {
	case prefix:
		result=ZSTD_getFrameHeader(&s->frame,s->small,s->have);
		if(ZSTD_isError(result)) return record(s,result,s->consumed,0);
		if(result>s->have) { s->phase=header; s->remaining=result-s->have; return 0; }
		ZSTD_FALLTHROUGH;
	case header:
		result=ZSTD_getFrameHeader(&s->frame,s->small,s->have);
		if(result) return record(s,ZSTD_isError(result)?result:ERROR(srcSize_wrong),s->consumed,0);
		if(s->frame.frameType==ZSTD_skippableFrame) {
			unit(s,skip,(size_t)s->frame.frameContentSize);
			if(!s->remaining) end_frame(s);
		} else unit(s,block_header,3);
		return 0;
	case block_header:
		s->block_type=(MEM_readLE24(s->small)>>1)&3;
		s->block_size=MEM_readLE24(s->small)>>3;
		s->last_block=MEM_readLE24(s->small)&1;
		/* Feeding streaming units avoids the whole-frame shortcut. Reject the
		 * empty-compressed case that its bufferless subpath otherwise accepts. */
		if(s->block_type==2 && s->block_size==0) {
			unit(s,payload,0);
			return record(s,ERROR(corruption_detected),s->consumed,0);
		}
		if(s->block_type==0 && s->block_size==0) return end_block(s);
		unit(s,payload,s->block_type==1?1:s->block_size);
		return 0;
	case payload: return end_block(s);
	case checksum: case skip: end_frame(s); return 0;
	}
	return ERROR(GENERIC);
}

ZSTDz_DStream* ZSTDz_createDStream(void)
{
	ZSTDz_DStream* s=(ZSTDz_DStream*)calloc(1,sizeof(*s));
	if(!s) return NULL;
	s->ctx=ZSTD_createDCtx();
	if(!s->ctx) { free(s); return NULL; }
	ZSTDz_resetDStream(s);
	return s;
}
void ZSTDz_freeDStream(ZSTDz_DStream* s)
{
	if(s) { ZSTD_freeDCtx(s->ctx); free(s); }
}
size_t ZSTDz_resetDStream(ZSTDz_DStream* s)
{
	ZSTD_DCtx* ctx;
	size_t result;
	if(!s) return ERROR(GENERIC);
	ctx=s->ctx;
	result=ZSTD_DCtx_reset(ctx,ZSTD_reset_session_only);
	if(ZSTD_isError(result)) return result;
	memset(s,0,sizeof(*s)); s->ctx=ctx; s->block_type=UINT32_MAX;
	s->detail.block_index=ZSTDZ_UNKNOWN_INDEX; s->detail.block_type=UINT32_MAX;
	unit(s,prefix,5);
	return 0;
}
ZSTD_DCtx* ZSTDz_getDCtx(ZSTDz_DStream* s) { return s?s->ctx:NULL; }
const ZSTDz_ErrorDetail* ZSTDz_getErrorDetail(const ZSTDz_DStream* s) { return s?&s->detail:NULL; }

/* Complexity: O(input + decoded bytes), constant auxiliary framing storage.
 * Restrict each upstream call to one framing unit; output buffering and history
 * remain entirely upstream's responsibility. Ordinary ZSTD callers pay nothing. */
size_t ZSTDz_decompressStream(ZSTDz_DStream* s,ZSTD_outBuffer* output,ZSTD_inBuffer* input)
{
	if(!s || !input || !output || input->pos>input->size || output->pos>output->size ||
	   (!input->src && input->size) || (!output->dst && output->size)) return ERROR(GENERIC);
	if(s->failure) return s->failure;
	if(s->ctx->format!=ZSTD_f_zstd1) return record(s,ERROR(parameter_unsupported),s->consumed,0);
	if(s->boundary) {
		if(input->pos==input->size) return 0;
		s->boundary=0; ++s->frame_index; s->frame_start=s->consumed;
		s->block_index=0; s->block_type=UINT32_MAX;
		memset(&s->frame,0,sizeof(s->frame)); unit(s,prefix,5);
	}
	for(;;) {
		ZSTD_inBuffer bounded=*input;
		size_t const allowed=MIN(input->size-input->pos,s->remaining);
		size_t const before=input->pos, beforeOut=output->pos;
		U64 const beforeDecoded=s->ctx->decodedSize;
		int const small=s->phase!=payload && s->phase!=skip;
		size_t result,used;
		if(allowed>UINT64_MAX-s->consumed) return record(s,ERROR(srcSize_wrong),s->consumed,0);
		bounded.size=bounded.pos+allowed;
		if(small && allowed) memcpy(s->small+s->have,(const BYTE*)input->src+input->pos,allowed);
		result=ZSTD_decompressStream(s->ctx,output,&bounded);
		if(ZSTD_isError(result)) {
			if(s->phase==payload && s->last_block && s->ctx->decodedSize>beforeDecoded &&
			   s->frame.frameContentSize!=ZSTD_CONTENTSIZE_UNKNOWN && s->ctx->decodedSize!=s->frame.frameContentSize)
				return content_size_error(s,result,s->consumed+allowed);
			return record(s,result,s->consumed+allowed,small?allowed:0);
		}
		used=bounded.pos-before;
		input->pos=bounded.pos; s->consumed+=used; s->remaining-=used;
		if(small) s->have+=used;
		if(s->phase==payload) s->decoded=s->completed_output+s->ctx->decodedSize;
		if(!s->remaining) {
			size_t const next=advance(s);
			if(ZSTD_isError(next)) return next;
		}
		if(s->boundary) return 0;
		if((!used && output->pos==beforeOut) || output->pos==output->size || input->pos==input->size)
			return result?result:s->remaining;
	}
}

size_t ZSTDz_finishDStream(ZSTDz_DStream* s)
{
	if(!s) return ERROR(GENERIC);
	if(s->failure) return s->failure;
	if(s->boundary) return 0;
	record(s,ERROR(srcSize_wrong),s->consumed,0);
	s->detail.kind=ZSTDz_truncated;
	return s->failure;
}

/* Convenience replay uses the same streaming tracker; it is not a second decoder. */
size_t ZSTDz_diagnose(const void* src,size_t size,const void* dict,size_t dictSize,
 size_t maxWindowSize,ZSTDz_ErrorDetail* detail)
{
	ZSTDz_DStream* s;
	BYTE* output;
	ZSTD_inBuffer in={src,size,0};
	size_t result=0;
	if(!detail) return ERROR(GENERIC);
	memset(detail,0,sizeof(*detail));
	if((!src && size) || (!dict && dictSize)) { detail->kind=ZSTDz_resource; detail->code=ZSTD_error_GENERIC; return ERROR(GENERIC); }
	s=ZSTDz_createDStream(); output=(BYTE*)malloc(ZSTD_BLOCKSIZE_MAX);
	if(!s || !output) { result=ERROR(memory_allocation); goto done; }
	/* This setter rejects limits below 1 KiB. The explicit header check below
	 * retains the convenience API's exact byte limit, including tiny limits. */
	result=ZSTD_DCtx_setMaxWindowSize(s->ctx,MAX(maxWindowSize,1024));
	if(ZSTD_isError(result)) goto done;
	result=ZSTD_DCtx_loadDictionary(s->ctx,dict,dictSize);
	if(ZSTD_isError(result)) goto done;
	do {
		ZSTD_outBuffer out={output,ZSTD_BLOCKSIZE_MAX,0};
		result=ZSTDz_decompressStream(s,&out,&in);
		if(ZSTD_isError(result)) break;
		if(s->frame.frameType!=ZSTD_skippableFrame && s->frame.windowSize>maxWindowSize) {
			result=record(s,ERROR(frameParameter_windowTooLarge),s->consumed,0);
			s->detail.expected=maxWindowSize; break;
		}
		if(in.pos==in.size && !out.pos) break;
	} while(in.pos<in.size || result);
	if(!ZSTD_isError(result)) result=ZSTDz_finishDStream(s);
	*detail=s->detail;
	if(!ZSTD_isError(result)) {
		detail->decoded_bytes=s->decoded; detail->frame_index=s->frame_index;
		detail->input_begin=detail->input_end=detail->detected_offset=s->consumed;
	}
 done:
	if(ZSTD_isError(result) && detail->kind==ZSTDz_none) {
		detail->code=ZSTD_getErrorCode(result); detail->kind=ZSTDz_resource;
	}
	free(output); ZSTDz_freeDStream(s); return result;
}
