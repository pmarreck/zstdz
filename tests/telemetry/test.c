#define ZSTD_STATIC_LINKING_ONLY
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "zstdz_errors.h"
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static const unsigned char raw[] = {0x28,0xb5,0x2f,0xfd,0x20,3, 0x19,0,0, 'a','b','c'};
static ZSTDz_ErrorDetail detail;
/* Feed disjoint input buffers and tiny output buffers, as a real caller may.
 * Compare output against independently known source bytes on valid fixtures. */
static size_t chunked(const unsigned char* bytes,size_t size,size_t inputChunk,size_t outputChunk,
 const unsigned char* expected,size_t expectedSize,ZSTDz_ErrorDetail* diagnostic) {
	ZSTDz_DStream* s=ZSTDz_createDStream();
	unsigned char* output=(unsigned char*)malloc(outputChunk);
	size_t offset=0,decoded=0,result=1;
	REQUIRE(s && output);
	while(offset<size) {
		ZSTD_inBuffer in={bytes+offset,(size-offset<inputChunk?size-offset:inputChunk),0};
		while(in.pos<in.size) {
			ZSTD_outBuffer out={output,outputChunk,0}; size_t before=in.pos;
			result=ZSTDz_decompressStream(s,&out,&in);
			if(ZSTD_isError(result)) goto done;
			if(expected) { REQUIRE(decoded+out.pos<=expectedSize); REQUIRE(memcmp(output,expected+decoded,out.pos)==0); }
			decoded+=out.pos;
			REQUIRE(in.pos>before || out.pos>0);
		}
		offset+=in.pos;
	}
	while(result) {
		ZSTD_inBuffer in={NULL,0,0}; ZSTD_outBuffer out={output,outputChunk,0};
		result=ZSTDz_decompressStream(s,&out,&in);
		if(ZSTD_isError(result)) goto done;
		if(expected) { REQUIRE(decoded+out.pos<=expectedSize); REQUIRE(memcmp(output,expected+decoded,out.pos)==0); }
		decoded+=out.pos;
		if(!out.pos) break;
	}
	result=ZSTDz_finishDStream(s);
	if(expected) { REQUIRE(!ZSTD_isError(result)); REQUIRE(decoded==expectedSize); }
done:
	*diagnostic=*ZSTDz_getErrorDetail(s);
	if(ZSTD_isError(result)) REQUIRE(ZSTDz_finishDStream(s)==result); /* Sticky until reset. */
	REQUIRE(ZSTDz_resetDStream(s)==0);
	REQUIRE(ZSTDz_getErrorDetail(s)->kind==ZSTDz_none);
	{
		unsigned char scratch[16]; ZSTD_inBuffer in={raw,sizeof(raw),0}; ZSTD_outBuffer out={scratch,sizeof(scratch),0};
		REQUIRE(ZSTDz_decompressStream(s,&out,&in)==0);
		REQUIRE(out.pos==3 && memcmp(scratch,"abc",3)==0);
		REQUIRE(ZSTDz_finishDStream(s)==0);
	}
	free(output); ZSTDz_freeDStream(s); return result;
}
static void streaming_tests(void) {
	unsigned char bad[sizeof(raw)], output[64];
	ZSTDz_DStream* stream=ZSTDz_createDStream();
	ZSTD_inBuffer in={bad,sizeof(bad),0};
	ZSTD_outBuffer out={output,sizeof(output),0};
	const ZSTDz_ErrorDetail* d;
	REQUIRE(stream!=NULL);
	memcpy(bad,raw,sizeof(raw)); bad[6]|=6;
	REQUIRE(ZSTD_isError(ZSTDz_decompressStream(stream,&out,&in)));
	d=ZSTDz_getErrorDetail(stream);
	REQUIRE(d->kind==ZSTDz_decoder && d->stage==ZSTDz_block_header);
	REQUIRE(d->input_begin==6 && d->input_end==9);
	ZSTDz_freeDStream(stream);
	{
		const size_t chunks[]={1,2,3,5,6,7,11,64}; size_t i,j,n;
		for(i=0;i<sizeof(chunks)/sizeof(chunks[0]);++i) for(j=0;j<sizeof(chunks)/sizeof(chunks[0]);++j) {
			ZSTDz_ErrorDetail error;
			REQUIRE(chunked(raw,sizeof(raw),chunks[i],chunks[j],(const unsigned char*)"abc",3,&error)==0);
			REQUIRE(ZSTD_isError(chunked(bad,sizeof(bad),chunks[i],chunks[j],NULL,0,&error)));
			REQUIRE(error.input_begin==6 && error.input_end==9 && error.block_index==0);
			REQUIRE(error.stage==ZSTDz_block_header && error.block_type==3);
		}
		for(n=0;n<sizeof(raw);++n) {
			ZSTDz_ErrorDetail error;
			REQUIRE(ZSTD_isError(chunked(raw,n,1,1,NULL,0,&error)));
			REQUIRE(error.kind==ZSTDz_truncated && error.detected_offset==n);
		}
		{
			unsigned char two[2*sizeof(raw)]; ZSTDz_ErrorDetail error;
			memcpy(two,raw,sizeof(raw)); memcpy(two+sizeof(raw),raw,sizeof(raw));
			REQUIRE(chunked(two,sizeof(two),7,1,(const unsigned char*)"abcabc",6,&error)==0);
			two[18]|=6;
			REQUIRE(ZSTD_isError(chunked(two,sizeof(two),1,1,NULL,0,&error)));
			REQUIRE(error.frame_index==1 && error.frame_offset==12 && error.input_begin==18 && error.input_end==21);
			REQUIRE(error.decoded_bytes==3);
		}
		{
			unsigned char framed[2*sizeof(raw)+8]; ZSTDz_ErrorDetail error;
			const unsigned char skipped[]={0x50,0x2a,0x4d,0x18,0,0,0,0};
			memcpy(framed,raw,sizeof(raw)); memcpy(framed+12,skipped,8); memcpy(framed+20,raw,sizeof(raw));
			REQUIRE(chunked(framed,sizeof(framed),1,1,(const unsigned char*)"abcabc",6,&error)==0);
			framed[26]|=6;
			REQUIRE(ZSTD_isError(chunked(framed,sizeof(framed),1,1,NULL,0,&error)));
			REQUIRE(error.frame_index==2 && error.frame_offset==20 && error.decoded_bytes==3);
			memcpy(framed,raw,sizeof(raw)); framed[6]=0x1b;
			REQUIRE(chunked(framed,10,1,1,(const unsigned char*)"aaa",3,&error)==0);
		}
	}
}
static size_t diagnose(const void* p, size_t n) {
	return ZSTDz_diagnose(p,n,NULL,0,8<<20,&detail);
}
static void invalid(const void* p, size_t n, ZSTDz_DetectionKind kind, ZSTDz_Stage stage,
	size_t begin, size_t end) {
	REQUIRE(ZSTD_isError(diagnose(p,n)));
	REQUIRE(detail.kind == kind);
	REQUIRE(detail.stage == stage);
	if(detail.input_begin!=begin || detail.input_end!=end)
		fprintf(stderr,"kind %u: range %llu..%llu, expected %zu..%zu\n",(unsigned)kind,
		 (unsigned long long)detail.input_begin,(unsigned long long)detail.input_end,begin,end);
	REQUIRE(detail.input_begin == begin && detail.input_end == end);
	REQUIRE(detail.detected_offset == end);
}
static void structural_tests(void) {
	unsigned char bad[64];
	size_t n;
	REQUIRE(diagnose(raw,sizeof(raw)) == 0);
	REQUIRE(detail.decoded_bytes == 3);
	memcpy(bad,raw,sizeof(raw)); bad[6] |= 6;
	invalid(bad,sizeof(raw),ZSTDz_decoder,ZSTDz_block_header,6,9);
	REQUIRE(detail.frame_index == 0 && detail.block_index == 0);
	REQUIRE(detail.block_type == 3 && detail.decoded_bytes == 0);
	for (n=0;n<sizeof(raw);++n) {
		REQUIRE(ZSTD_isError(diagnose(raw,n)));
		REQUIRE(detail.kind == ZSTDz_truncated);
		REQUIRE(detail.detected_offset == n);
	}
	memcpy(bad,raw,sizeof(raw)); bad[5]=4;
	invalid(bad,sizeof(raw),ZSTDz_content_size,ZSTDz_block_payload,9,12);
	REQUIRE(detail.values_present && detail.expected==4 && detail.actual==3);
	REQUIRE(detail.decoded_bytes==0);
	memcpy(bad,raw,sizeof(raw)); bad[6]=0x21;
	invalid(bad,sizeof(raw),ZSTDz_block_size,ZSTDz_block_header,6,9);
	REQUIRE(detail.values_present && detail.expected==3 && detail.actual==4);
	/* RLE uses one input byte to produce the declared three bytes. */
	memcpy(bad,raw,sizeof(raw)); bad[6]=0x1b;
	REQUIRE(diagnose(bad,10)==0 && detail.decoded_bytes==3);
	bad[6]=0x23;
	invalid(bad,10,ZSTDz_block_size,ZSTDz_block_payload,9,10);
	REQUIRE(detail.expected==3 && detail.actual==4);
	{ const unsigned char empty[]={0x28,0xb5,0x2f,0xfd,0x20,1,1,0,0};
		invalid(empty,sizeof(empty),ZSTDz_content_size,ZSTDz_block_header,6,9);
		REQUIRE(detail.expected==1 && detail.actual==0);
	}
	/* Dictionary ID 1, without a supplied dictionary. */
	{ const unsigned char dictionary[]={0x28,0xb5,0x2f,0xfd,0x21,1,3,0x19,0,0,'a','b','c'};
		invalid(dictionary,sizeof(dictionary),ZSTDz_dictionary,ZSTDz_frame_header,0,7);
		REQUIRE(detail.expected==1 && detail.actual==0);
	}
	/* Two frames: indexes/offsets are absolute across concatenation. */
	memcpy(bad,raw,sizeof(raw)); memcpy(bad+sizeof(raw),raw,sizeof(raw)); bad[18]|=6;
	invalid(bad,24,ZSTDz_decoder,ZSTDz_block_header,18,21);
	REQUIRE(detail.frame_index==1 && detail.frame_offset==12 && detail.decoded_bytes==3);
	/* Two blocks in one frame, with corruption in the second block header. */
	memcpy(bad,raw,sizeof(raw)); bad[4]=0; bad[5]=0; bad[6]=0x18;
	memcpy(bad+12,raw+6,6); bad[12]|=6;
	invalid(bad,18,ZSTDz_decoder,ZSTDz_block_header,12,15);
	REQUIRE(detail.frame_index==0 && detail.block_index==1 && detail.decoded_bytes==3);
	/* Skippable frame before the normal frame. */
	memset(bad,0,8); bad[0]=0x50; bad[1]=0x2a; bad[2]=0x4d; bad[3]=0x18;
	memcpy(bad+8,raw,sizeof(raw)); bad[14]|=6;
	invalid(bad,20,ZSTDz_decoder,ZSTDz_block_header,14,17);
	REQUIRE(detail.frame_index==1 && detail.frame_offset==8);
	REQUIRE(diagnose(bad,8)==0);
	bad[4]=5;
	invalid(bad,8,ZSTDz_truncated,ZSTDz_skippable,8,8); /* Missing payload starts after its header. */
	REQUIRE(ZSTD_isError(ZSTDz_diagnose(raw,sizeof(raw),NULL,0,2,&detail)));
	REQUIRE(detail.kind==ZSTDz_resource && detail.expected==2 && detail.actual==3);
	REQUIRE(diagnose(raw,sizeof(raw))==0 && !detail.values_present && detail.kind==ZSTDz_none);
	REQUIRE(ZSTD_isError(ZSTDz_diagnose(NULL,1,NULL,0,1024,&detail)));
	REQUIRE(ZSTD_isError(ZSTDz_diagnose(raw,sizeof(raw),NULL,1,1024,&detail)));
	REQUIRE(ZSTD_isError(ZSTDz_diagnose(raw,sizeof(raw),NULL,0,1024,NULL)));
}
static void compressed_tests(void) {
	unsigned char data[32768], compressed[40000], mutated[40000];
	ZSTD_CCtx* cctx=ZSTD_createCCtx();
	size_t n,i; unsigned seed=42;
	REQUIRE(cctx!=NULL);
	for(i=0;i<sizeof(data);++i) { seed=seed*1664525U+1013904223U; data[i]=(unsigned char)((seed>>24)%16); }
	REQUIRE(!ZSTD_isError(ZSTD_CCtx_setParameter(cctx,ZSTD_c_checksumFlag,1)));
	n=ZSTD_compress2(cctx,compressed,sizeof(compressed),data,sizeof(data));
	REQUIRE(!ZSTD_isError(n)); REQUIRE(diagnose(compressed,n)==0);
	REQUIRE(detail.decoded_bytes==sizeof(data));
	memcpy(mutated,compressed,n); mutated[n-1]^=1;
	{
		const size_t inputChunks[]={1,7,4096}; const size_t outputChunks[]={1,17,131072}; size_t a,b;
		for(a=0;a<3;++a) for(b=0;b<3;++b) {
			ZSTDz_ErrorDetail error;
			REQUIRE(chunked(compressed,n,inputChunks[a],outputChunks[b],data,sizeof(data),&error)==0);
			REQUIRE(ZSTD_isError(chunked(mutated,n,inputChunks[a],outputChunks[b],NULL,0,&error)));
			REQUIRE(error.kind==ZSTDz_checksum && error.input_begin==n-4 && error.input_end==n);
			REQUIRE(error.decoded_bytes==sizeof(data));
		}
	}
	invalid(mutated,n,ZSTDz_checksum,ZSTDz_frame_checksum,n-4,n);
	REQUIRE(detail.suspect_begin==0 && detail.suspect_end==n);
	REQUIRE(detail.values_present && detail.expected!=detail.actual);
	REQUIRE(detail.decoded_bytes==sizeof(data));
	/* Sweep every compressed byte. Assertions are bounds, not a claim that every
		* mutated bit is detectable. The independent CLI verdict sweep is separate. */
	for(i=0;i<n;++i) {
		size_t r;
		memcpy(mutated,compressed,n); mutated[i]^=0x80;
		r=diagnose(mutated,n);
		if (ZSTD_isError(r)) {
			REQUIRE(detail.code==ZSTD_getErrorCode(r));
			REQUIRE(detail.kind!=ZSTDz_none);
			REQUIRE(detail.input_begin<=detail.input_end && detail.input_end<=n);
			REQUIRE(detail.suspect_begin<=detail.suspect_end && detail.suspect_end<=n);
		}
	}
	ZSTD_freeCCtx(cctx);
}
static void ring_tests(void) {
	size_t const size=1024*1024;
	unsigned char* data=(unsigned char*)malloc(size);
	unsigned char* compressed=(unsigned char*)malloc(ZSTD_compressBound(size));
	ZSTD_CCtx* ctx=ZSTD_createCCtx();
	size_t i,n;
	REQUIRE(data && compressed && ctx);
	for(i=0;i<size;++i) data[i]=(unsigned char)((i*13+(i/512))%251);
	REQUIRE(!ZSTD_isError(ZSTD_CCtx_setParameter(ctx,ZSTD_c_windowLog,10)));
	REQUIRE(!ZSTD_isError(ZSTD_CCtx_setParameter(ctx,ZSTD_c_contentSizeFlag,0)));
	REQUIRE(!ZSTD_isError(ZSTD_CCtx_setParameter(ctx,ZSTD_c_checksumFlag,1)));
	n=ZSTD_compress2(ctx,compressed,ZSTD_compressBound(size),data,size);
	REQUIRE(!ZSTD_isError(n));
	REQUIRE(diagnose(compressed,n)==0 && detail.decoded_bytes==size);
	{ ZSTDz_ErrorDetail error; REQUIRE(chunked(compressed,n,113,17,data,size,&error)==0); }
	ZSTD_freeCCtx(ctx); free(data); free(compressed);
}
static void configuration_tests(void) {
	unsigned char dict[1024],source[512],compressed[1024],output[512];
	unsigned seed=99; size_t i,n;
	ZSTD_CCtx* encoder=ZSTD_createCCtx(); ZSTDz_DStream* stream=ZSTDz_createDStream();
	REQUIRE(encoder && stream);
	for(i=0;i<sizeof(dict);++i) { seed=seed*1664525U+1013904223U; dict[i]=(unsigned char)(seed>>24); }
	memcpy(source,dict+128,sizeof(source));
	n=ZSTD_compress_usingDict(encoder,compressed,sizeof(compressed),source,sizeof(source),dict,sizeof(dict),3);
	REQUIRE(!ZSTD_isError(n) && n<sizeof(source)/2);
	REQUIRE(ZSTD_DCtx_loadDictionary(ZSTDz_getDCtx(stream),dict,sizeof(dict))==0);
	{
		ZSTD_inBuffer in={compressed,n,0}; ZSTD_outBuffer out={output,sizeof(output),0};
		REQUIRE(ZSTDz_decompressStream(stream,&out,&in)==0);
		REQUIRE(out.pos==sizeof(source) && memcmp(output,source,sizeof(source))==0);
	}
	REQUIRE(ZSTDz_resetDStream(stream)==0);
	REQUIRE(ZSTD_DCtx_setMaxWindowSize(ZSTDz_getDCtx(stream),1024)==0);
	{
		const unsigned char large_window[]={0x28,0xb5,0x2f,0xfd,0,0x60};
		ZSTD_inBuffer in={large_window,sizeof(large_window),0}; ZSTD_outBuffer out={output,sizeof(output),0};
		const ZSTDz_ErrorDetail* d;
		REQUIRE(ZSTD_isError(ZSTDz_decompressStream(stream,&out,&in)));
		d=ZSTDz_getErrorDetail(stream);
		REQUIRE(d->kind==ZSTDz_resource && d->expected==1024 && d->actual==4*1024*1024);
		REQUIRE(d->input_begin==0 && d->input_end==6);
	}
	ZSTD_freeCCtx(encoder); ZSTDz_freeDStream(stream);
}
/* JSON adapter for independent CLI comparisons; accepts exactly one file. */
static int diagnose_file(const char* path,int streaming) {
	FILE* f=fopen(path,"rb"); unsigned char* bytes; long length; size_t result;
	REQUIRE(f!=NULL); REQUIRE(fseek(f,0,SEEK_END)==0); length=ftell(f); REQUIRE(length>=0);
	rewind(f); bytes=(unsigned char*)malloc((size_t)length+1); REQUIRE(bytes!=NULL);
	REQUIRE(fread(bytes,1,(size_t)length,f)==(size_t)length); fclose(f);
	result=streaming?chunked(bytes,(size_t)length,1,17,NULL,0,&detail):diagnose(bytes,(size_t)length); free(bytes);
	printf("{\"error\":%u,\"kind\":%u,\"stage\":%u,\"begin\":%llu,\"end\":%llu,\"detected\":%llu,\"frame\":%llu,\"block\":%llu,\"decoded\":%llu}\n",
		(unsigned)detail.code,(unsigned)detail.kind,(unsigned)detail.stage,(unsigned long long)detail.input_begin,(unsigned long long)detail.input_end,(unsigned long long)detail.detected_offset,
		(unsigned long long)detail.frame_index,(unsigned long long)detail.block_index,(unsigned long long)detail.decoded_bytes);
	return ZSTD_isError(result)?1:0;
}
int main(int argc,char** argv) {
	if(argc==2) return diagnose_file(argv[1],0);
	if(argc==3 && strcmp(argv[1],"--stream")==0) return diagnose_file(argv[2],1);
	streaming_tests(); structural_tests(); compressed_tests(); ring_tests(); configuration_tests(); puts("Telemetry structure and byte-mutation tests passed"); return 0;
}
