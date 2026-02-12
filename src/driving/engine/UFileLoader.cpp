#include "../platform/FILE.h"


void * UFileLoader__FileLoadDirectFromDisk(char* param_1, int param_2, bool z_variant) {
    
    if (z_variant) {
        return FILE_load(param_1, param_2);
    }
    
    return FILE_loadz(param_1, param_2);
    
}

