#ifndef XIPS_H
#define XIPS_H

#include "xpatchlib.h"
#include <StdIO.h>

int CheckIPS(FILE* fips);
int ApplyIPS(const char* ips, const char* dst);

#endif // XIPS_H