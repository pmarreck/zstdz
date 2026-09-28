/* Sustained decode benchmark: print bytes processed so the work is observable. */
#include <stdio.h>
#include <stdlib.h>
#include "zstd.h"
#if defined(TELEMETRY_REPLAY) || defined(TELEMETRY_STREAM)
#include "zstdz_errors.h"
#endif
int main(int argc,char** argv) {
	FILE* f; long size; unsigned char* input; unsigned char* output;
	ZSTD_DCtx* ctx; unsigned long loops,i; unsigned long long bytes=0;
#ifdef TELEMETRY_STREAM
	ZSTDz_DStream* tracked=ZSTDz_createDStream();
	if(!tracked) return 2;
#endif
	if(argc!=3) return 2;
	loops=strtoul(argv[2],NULL,10); if(!loops) return 2;
	f=fopen(argv[1],"rb"); if(!f || fseek(f,0,SEEK_END)) return 2;
	size=ftell(f); if(size<=0) return 2; rewind(f);
	input=(unsigned char*)malloc((size_t)size); output=(unsigned char*)malloc(131072);
	ctx=ZSTD_createDCtx(); if(!input || !output || !ctx) return 2;
	if(fread(input,1,(size_t)size,f)!=(size_t)size) return 2;
	fclose(f);
	for(i=0;i<loops;++i) {
#ifdef TELEMETRY_REPLAY
		ZSTDz_ErrorDetail detail;
		size_t const r=ZSTDz_diagnose(input,(size_t)size,NULL,0,64<<20,&detail);
		if(ZSTD_isError(r)) { fprintf(stderr,"%s\n",ZSTD_getErrorName(r)); return 1; }
		bytes+=detail.decoded_bytes;
#else
		ZSTD_inBuffer in={input,(size_t)size,0};
		size_t r=1;
		#ifdef TELEMETRY_STREAM
		if(ZSTD_isError(ZSTDz_resetDStream(tracked))) return 1;
		#else
		if(ZSTD_isError(ZSTD_DCtx_reset(ctx,ZSTD_reset_session_only))) return 1;
		#endif
		while(in.pos<in.size || r) {
			ZSTD_outBuffer out={output,131072,0}; size_t before=in.pos;
			#ifdef TELEMETRY_STREAM
			r=ZSTDz_decompressStream(tracked,&out,&in);
			#else
			r=ZSTD_decompressStream(ctx,&out,&in);
			#endif
			if(ZSTD_isError(r)) { fprintf(stderr,"%s\n",ZSTD_getErrorName(r)); return 1; }
			bytes+=out.pos;
			if(r && in.pos==before && !out.pos) return 1;
		}
		#ifdef TELEMETRY_STREAM
		if(ZSTD_isError(ZSTDz_finishDStream(tracked))) return 1;
		#endif
#endif
	}
	printf("%llu\n",bytes);
#ifdef TELEMETRY_STREAM
	ZSTDz_freeDStream(tracked);
#endif
	ZSTD_freeDCtx(ctx); free(input); free(output); return 0;
}
