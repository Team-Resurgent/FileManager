#ifndef XIPSLIB_H
#define XIPSLIB_H

typedef enum {
	E_NO_ERROR,
	E_FOPEN_SRC,
	E_FOPEN_DST,
	E_FOPEN_IPS,
	E_NOT_IPS,
	E_OUT_OF_MEMORY,
	E_CANNOT_OVR,
	E_REN_ERROR,
	E_BAD_IPS,
	E_FWRITE_DST,
	E_FSEEK_SRC,
	E_FSEEK_IPS
} ErrorCode;

#ifdef __cplusplus
extern "C" {
#endif

	int CheckIPS(FILE* fips);

	int CreateBak(const char* src, bool ovr);

	int RestoreBak(const char* src, bool ovr);

	int ApplyIPS(const char* ips, const char* src);

#ifdef __cplusplus
}
#endif

#endif // XIPSLIB_H