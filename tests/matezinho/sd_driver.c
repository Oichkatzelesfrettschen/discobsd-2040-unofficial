/* Original test code is under the repository's BSD 3-Clause license. */
#include "../../sys/arch/rp2040/dev/sdspi.h"
#include <string.h>
int sd_driver_test(const struct sdspi_ops *ops,unsigned sectors,int readonly)
{
    struct sdspi sd;
    unsigned char buffer[512];
    unsigned i;
    sd.ops=ops;
    if(sdspi_init(&sd)!=SDE_OK)return 1;
    if(sd.nsectors!=sectors)return 2;
    if(sdspi_read(&sd,sectors-1,buffer)!=SDE_OK)return 3;
    for(i=0;i<512;i++)buffer[i]=(unsigned char)(i^0x5a);
    if(readonly) return sdspi_write(&sd,sectors-1,buffer)==SDE_OK?4:0;
    if(sdspi_write(&sd,sectors-1,buffer)!=SDE_OK)return 5;
    memset(buffer,0,sizeof(buffer));
    if(sdspi_read(&sd,sectors-1,buffer)!=SDE_OK)return 6;
    for(i=0;i<512;i++)if(buffer[i]!=(unsigned char)(i^0x5a))return 7;
    if(sdspi_read(&sd,sectors,buffer)==SDE_OK)return 8;
    return 0;
}
