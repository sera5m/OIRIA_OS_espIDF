#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_master.h"
#include "hardware/drivers/sd_card/d_sdc.h"
#include <string.h>
#include <ctype.h>

static int str_ieq(const char* a, const char* b){
    while(*a && *b){
        if(tolower((unsigned char)*a) !=
           tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

OFV_Mode OFV_GetModeFromExt(const char* ext){

    for(size_t i = 0;
        i < sizeof(g_ofv_ext_map)/sizeof(g_ofv_ext_map[0]);
        i++)
    {
        if(str_ieq(ext, g_ofv_ext_map[i].ext)){
            return g_ofv_ext_map[i].mode;
        }
    }

    return OFV_TXT; //fallback
}

const char* OFV_GetExtension(const char* path){

    const char* dot = strrchr(path, '.');

    if(!dot || dot == path)
        return "";

    return dot;
}


// use example
/*
OFV_Mode mode =
    OFV_GetModeFromExt(
        OFV_GetExtension("scan.glb")
    );
    */