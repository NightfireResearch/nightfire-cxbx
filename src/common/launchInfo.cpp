#include <stdio.h>
#include <stdlib.h>

// Replaces XAPILIB::XGetLaunchInfo
int __stdcall XGetLaunchInfo(int *someIdentifier, void* data) {

	printf("XGetLaunchInfo: 0x%08x, 0x%08x\n", (int) data, (int) someIdentifier);
    // Open the file
    FILE* file = fopen("psiLaunch.bin", "rb");
    if (file == NULL) {
        // File not found
        return 0x490;
    }

    printf("Reading psiLaunch.bin\n");

    // Get the length of the file
    fseek(file, 0, SEEK_END);
    int length = ftell(file);
    fseek(file, 0, SEEK_SET);

    printf("We got 0x%08x bytes\n", length);

    // Read 0x300 words into the given address
    fread(data, 4, 0x300, file);

    // Close the file
    fclose(file);

    *someIdentifier = 0; // Unclear what these values are. Set at boot by XBox kernel, values 2 and 3 cause some extra checks (launch title), Action checks if this is non-zero and fails out if it is. So 0 is the best result?
    return 0;

	// Returns 0 (success) or 0x490 (LaunchDataPage pointer was 0)
}

int __stdcall XLaunchNewImageA(char *executableName,void *launchInfo) {

    printf("XLaunchNewImageA: %s, 0x%08x\n", executableName, launchInfo);

    // Open the file in binary write mode

    char filename[256];

    FILE* file = fopen("psiLaunch.bin", "wb");

    printf("Writing psiLaunch.bin\n");

    if (file == NULL) {
        // Handle error if the file couldn't be opened
        perror("Error opening file");
        exit(-1);
        return 0x490;
    }

    // Write the data to the file
    size_t written = fwrite(launchInfo, 4, 0x300, file);

    if (written != (0x300*4)) {
        // Handle error if not all data could be written
        perror("Error writing to file");
    } else {
        printf("Data written to %s successfully\n", filename);
    }

    // Close the file
    fclose(file);

    printf("State written - you need to close this, and launch the opposite executable...\n\n");
    while(1);//so you can read log

    exit(0);
}