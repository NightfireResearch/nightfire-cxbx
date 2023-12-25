#include <stdio.h>


// We replace XAPILIB::XGetLaunchInfo with this
// Currently DOES NOT WORK - either I've misunderstood something, or some other flag must be set for this change to take effect.
int __stdcall getLaunchInfo(int *someIdentifier, void* data) {

	printf("We are asked to put launch data at 0x%08x and some identifier at 0x%08x\n", (int) data, (int) someIdentifier);
    // Open the file
    FILE* file = fopen("psiLaunch.bin", "rb");
    if (file == NULL) {
        // File not found
        return 0x490;
    }

    printf("Loading psiLaunch.bin\n");

    // Get the length of the file
    fseek(file, 0, SEEK_END);
    int length = ftell(file);
    fseek(file, 0, SEEK_SET);

    printf("We got 0x%08x bytes\n", length);

    // Read 0x300 words into the given address
    fread(data, 4, 0x300, file);

    // Close the file
    fclose(file);


    *someIdentifier = 3;
    return 0;

	// Returns 0 (success) or 0x490 (LaunchDataPage pointer was 0)
}