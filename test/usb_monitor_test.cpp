/*
 * @Author: Junwen Cui 
 * @Date: 2021-07-26 20:53:41 
 */

#include <cinttypes>
#include <numeric>
#include <set>
#include <string>
#include <string.h>
#include <tuple>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "usb_monitor_test.h"


// 定义所用信息的结构体
struct DataInfo{
    unsigned char kernel_time[8];       //8 Byte 
    unsigned char status;               //1 Byte
    char name[128];                         //unknow
}data_info;


int main(){

    int mFd,i=0;
    int leng = 0;
    // 初始化读取缓存 
    char mBuf[128];
    // 打开/proc/usb_monitor 文件准备进行读取
    mFd = open(DEV_NAME, O_RDWR); 
    if (mFd == -1) {
			printf("open %s fail, Check!!!\n",DEV_NAME);
            return errno;
        }
    printf("USB Monitor is working !!! \n ");
    while(1){
        // 读取文件函数所调用的 就是驱动中 usb_monitor_read
        leng = read(mFd, mBuf, KERNEL_DATA_LENG);
        if ( leng > 0 ){
            printf("read length is %d\n",leng);
            //8 字节的 kernel time
            for (i = 0; i < 8; i++){
                data_info.kernel_time[i] = mBuf[i];
                printf("kernel_time[%d] = 0x%x \n", i, data_info.kernel_time[i]);
            }
            // 记录插拔状态
            data_info.status = mBuf[8];
            // 拷贝USB名称
            memcpy(data_info.name,mBuf+9,leng-9);

            if(data_info.status==1){
                printf("USB %s -> plug In \n",data_info.name);
            }else{
                printf("USB %s -> plug Out \n",data_info.name);
            }
            printf("\n");
        }
    }
}



