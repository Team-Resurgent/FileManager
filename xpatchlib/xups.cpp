#include "xups.h"
#include <String.h>

int CheckUPS(FILE* fups) {
	if (fseek(fups, 0, SEEK_SET) != 0) return E_FSEEK_PATCH;

	char header[4];
	if ((fread(header, 1, 4, fups) != 4) || (memcmp(header, "UPS1", 4) != 0)) return E_NOT_UPS;

	unsigned int inputSize = 0;
	int e = ReadVarInt(fups, &inputSize);
	if (e != E_NO_ERROR) return e;

	unsigned int outputSize = 0;
	e = ReadVarInt(fups, &outputSize);
	if (e != E_NO_ERROR) return e;

	for (;;) {
		long pos = ftell(fups);
		if (pos < 0) return E_FTELL_PATCH;

		if (fseek(fups, 0, SEEK_END) != 0) return E_FSEEK_PATCH;
		long end = ftell(fups);
		if (end < 0) return E_FTELL_PATCH;

		if (fseek(fups, pos, SEEK_SET) != 0) return E_FSEEK_PATCH;
		long rem = end - pos;

		if (rem == 12) {
			char crc[12];
			if (fread(crc, 1, 12, fups) != 12) return E_BAD_PATCH;
			fseek(fups, 0, SEEK_SET);
			return E_NO_ERROR;
		}

		if (rem < 12) return E_BAD_PATCH;

		unsigned int rel = 0;
		e = ReadVarInt(fups, &rel);
		if (e != E_NO_ERROR) return e;

		int b;
		do {
			b = fgetc(fups);
			if (b == EOF) return E_BAD_PATCH;
		} while (b != 0);
	}

	return E_ERROR;
}