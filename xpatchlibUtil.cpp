#include "xpatchlibUtil.h"

#include "xpatchlib.h"

// CreateBak callback function
bool UpdateBakProgress(ULONGLONG wb) {
	if (CopyProgress::g_copyProgFn) {
		if (!CopyProgress::g_copyProgFn(-wb, NULL, NULL, CopyProgress::g_copyProgUser)) {
			return false;
		}
		return true;
	}
}