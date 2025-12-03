#ifndef XPATCHLIB_H
#define XPATCHLIB_H

#include <StdIO.h>

typedef enum {
	E_ERROR = -1,
	E_NO_ERROR,
	E_NOT_IPS,
	E_NOT_UPS,
	E_NOT_BPS,
	E_BAD_PATCH,
	E_FOPEN_SRC,
	E_FOPEN_DST,
	E_FOPEN_PATCH,
	E_FREAD_SRC,
	E_FREAD_DST,
	E_FREAD_PATCH,
	E_FSEEK_SRC,
	E_FSEEK_DST,
	E_FSEEK_PATCH,
	E_FWRITE_DST,
	E_FTELL_PATCH,
	E_CANNOT_OVR,
	E_OUT_OF_MEMORY,
} ErrorCode;

int CreateBak(const char* src, bool ovr, int (*Upd)(unsigned long long));
int RestoreBak(const char* src, bool ovr);
int ReadVarInt(FILE* fp, unsigned int* ui);

#endif // XPATCHLIB_H