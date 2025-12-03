#include "xbps.h"
#include <String.h>

int CheckBPS(FILE* fbps) {
	if (fseek(fbps, 0, SEEK_SET) != 0) return E_FSEEK_PATCH;

	char header[4];
	if ((fread(header, 1, 4, fbps) != 4) || (memcmp(header, "BPS1", 4) != 0)) return E_NOT_BPS;

	unsigned int inputSize = 0;
	int e = ReadVarInt(fbps, &inputSize);
	if (e != E_NO_ERROR) return e;

	unsigned int outputSize = 0;
	e = ReadVarInt(fbps, &outputSize);
	if (e != E_NO_ERROR) return e;

	unsigned int metaSize = 0;
	e = ReadVarInt(fbps, &metaSize);
	if (e != E_NO_ERROR) return e;

	if (fseek(fbps, metaSize, SEEK_CUR) != 0) return E_BAD_PATCH;

	for (;;) {
		long pos = ftell(fbps);
		if (pos < 0) return E_FTELL_PATCH;

		if (fseek(fbps, 0, SEEK_END) != 0) return E_FSEEK_PATCH;
		long end = ftell(fbps);
		if (end < 0) return E_FTELL_PATCH;

		if (fseek(fbps, pos, SEEK_SET) != 0) return E_FSEEK_PATCH;
		long rem = end - pos;

		if (rem == 12) {
			char crc[12];
			if (fread(crc, 1, 12, fbps) != 12) return E_BAD_PATCH;
			fseek(fbps, 0, SEEK_SET);
			return E_NO_ERROR;
		}

		if (rem < 12) return E_BAD_PATCH;

		unsigned int cmd = 0;
		e = ReadVarInt(fbps, &cmd);
		if (e != E_NO_ERROR) return e;

		unsigned int type = cmd & 3;
		unsigned int len = cmd >> 2;
		unsigned int off = 0;

		switch (type) {
		case 0:
			break;
		case 1:
			if (fseek(fbps, len, SEEK_CUR) != 0) return E_BAD_PATCH;
			break;
		case 2:
			e = ReadVarInt(fbps, &off);
			if (e != E_NO_ERROR) return E_BAD_PATCH;
			break;
		case 3:
			e = ReadVarInt(fbps, &off);
			if (e != E_NO_ERROR) return E_BAD_PATCH;
			break;
		default:
			return E_BAD_PATCH;
			break;
		}
	}

	return E_ERROR;
}