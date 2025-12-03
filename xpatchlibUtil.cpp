#include "xpatchlibUtil.h"
#include "xpatchlib.h"

// ------------------------------------------------------------------
// CreateBak function with added prog (src-dev)
// ------------------------------------------------------------------
int UpdateBakProgress(ULONGLONG wb) {
	
	if (CopyProgress::g_copyProgFn) {
		if (!CopyProgress::g_copyProgFn(-wb, NULL, NULL, CopyProgress::g_copyProgUser)) {
			return -1;
		}
		return 0;
	}

}