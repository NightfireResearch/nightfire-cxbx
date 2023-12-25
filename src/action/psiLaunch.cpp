#include <stdlib.h>
#include <stdio.h>

void psiLaunchDriving(void *data,unsigned int len)
{
  printf("psiLaunchDriving: 0x%08x bytes", len);

  FILE* file = fopen("psiLaunch.bin", "wb");

    if (file == NULL) {
        // Handle error if the file couldn't be opened
        perror("Error opening file");
        return;
    }

    // Write the data to the file
    size_t written = fwrite(data, 1, len, file);

    if (written != len) {
        // Handle error if not all data could be written
        perror("Error writing to file");
    } else {
        printf("Data written successfully\n");
    }

    // Close the file
    fclose(file);


  exit(0);
  //FUN_000e8fe0("driving.xbe",data,len);
  return;
}