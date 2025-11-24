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
	E_REN_ERROR
} ErrorCode;

#ifdef __cplusplus
extern "C" {
#endif

	/// <summary>
	/// Creates an identical file with the extension .bak
	/// </summary>
	/// <param name="src">source filepath</param>
	/// <param name="ovr">overwrite</param>
	/// <returns>ErrorCode</returns>
	int createBak(const char* src, bool ovr);

	/// <summary>
	/// Restores a .bak file
	/// </summary>
	/// <param name="src">source filepath</param>
	/// <param name="ovr">overwrite</param>
	/// <returns>ErrorCode</returns>
	int restoreBak(const char* src, bool ovr);

	/// <summary>
	/// Applies IPS patch file
	/// </summary>
	/// <param name="ips">ips filepath</param>
	/// <param name="src">source filepath</param>
	/// <returns>ErrorCode</returns>
	int applyIPS(const char* ips, const char* src);

#ifdef __cplusplus
}
#endif

#endif // XIPSLIB_H