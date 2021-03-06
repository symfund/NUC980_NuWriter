/*-----------------------------------------------------------------------------------*/
/* Nuvoton Technology Corporation confidential                                       */
/*                                                                                   */
/* Copyright (c) 2018 by Nuvoton Technology Corporation                              */
/* All rights reserved                                                               */
/*                                                                                   */
/*-----------------------------------------------------------------------------------*/
/*
 * Driver for FMI devices
 * SD layer glue code
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nuc980.h"
#include "sys.h"
#include "sd.h"
#include "sdglue.h"
#include "usbd.h"


DISK_DATA_T SD_DiskInfo;
FMI_SD_INFO_T *_pSD0 = NULL;
FMI_SD_INFO_T *_pSD1 = NULL;
UINT8 pSD0_offset = 0;
UINT8 pSD1_offset = 0;

INT  fmiInitSDDevice(void)
{

    FMI_SD_INFO_T *pSD_temp = NULL;

    /* select eMMC/SD function pins */
    if (((inpw(REG_SYS_PWRON) & 0x00000300) == 0x300)) {
        /* Set GPC for eMMC0/SD0 */
        outpw(REG_SYS_GPC_MFPL, 0x66600000);
        outpw(REG_SYS_GPC_MFPH, 0x00060666);
    } else {
        /* Set GPF for eMMC1/SD1 */
        outpw(REG_SYS_GPF_MFPL, 0x02222222);
    }

    // enable SD
    outpw(REG_EMMC_CTL, FMI_CSR_SD_EN);
    outpw(REG_FMI_EMMCCTL, inpw(REG_FMI_EMMCCTL) | SD_CSR_SWRST);// SD software reset
    while(inpw(REG_FMI_EMMCCTL) & SD_CSR_SWRST);
    outpw(REG_FMI_EMMCCTL, (inpw(REG_FMI_EMMCCTL) & ~SD_CSR_NWR_MASK) | (0x09 << 24));// set SDNWR = 9
    outpw(REG_FMI_EMMCCTL, (inpw(REG_FMI_EMMCCTL) & ~SD_CSR_BLK_CNT_MASK) | (0x01 << 16));// set BLKCNT = 1
    outpw(REG_FMI_EMMCCTL, inpw(REG_FMI_EMMCCTL) & ~SD_CSR_DBW_4BIT);// SD 1-bit data bus
    pSD_temp = malloc(sizeof(FMI_SD_INFO_T)+4);
    if (pSD_temp == NULL)
        return -1;
    memset((char *)pSD_temp, 0, sizeof(FMI_SD_INFO_T)+4);

    if (((inpw(REG_SYS_PWRON) & 0x00000300) == 0x300)) {
        pSD0_offset = (UINT32)pSD_temp % 4;
        _pSD0 = (FMI_SD_INFO_T *)((UINT32)pSD_temp + pSD0_offset);
        _pSD0->bIsCardInsert = TRUE;
        if (SD_Init(_pSD0) < 0)
            return SD_INIT_ERROR;
        SD_Get_SD_info(_pSD0, &SD_DiskInfo);
        if (SD_SelectCardType(_pSD0))
            return SD_SELECT_ERROR;
    } else {
        pSD1_offset = (UINT32)pSD_temp % 4;
        _pSD1 = (FMI_SD_INFO_T *)((UINT32)pSD_temp + pSD1_offset);
        _pSD1->bIsCardInsert = TRUE;
        if (SD_Init(_pSD1) < 0)
            return SD_INIT_ERROR;
        SD_Get_SD_info(_pSD1, &SD_DiskInfo);
        if (SD_SelectCardType(_pSD1))
            return SD_SELECT_ERROR;
    }

    MSG_DEBUG("eMMC fmiInitSDDevice Done\n");
    return SD_DiskInfo.totalSectorN;
}

INT fmiSD_Read(UINT32 uSector, UINT32 uBufcnt, UINT32 uDAddr)
{
    int volatile status=0;
    // enable SD
    status = SD_Read_in(pSD0, uSector, uBufcnt, uDAddr);
    return status;
}

INT fmiSD_Write(UINT32 uSector, UINT32 uBufcnt, UINT32 uSAddr)
{
    int volatile status=0;
    // enable SD
    MSG_DEBUG("uSector = %d   uBufcnt =%d   uSAddr=0x%x\n", uSector, uBufcnt, uSAddr);
    status = SD_Write_in(pSD0, uSector, uBufcnt, uSAddr);
    return status;
}

//================================================================================


/******************************************************************************
 *
 *  eMMC Functions
 *
 ******************************************************************************/
static BOOL bIsmmcMatch = FALSE;
static UINT32 *pmmcUpdateImage=0;

extern unsigned char *pImageList;
extern unsigned char imageList[400];

FW_MMC_IMAGE_T mmcImage;
FW_MMC_IMAGE_T *pmmcImage;

UINT32 GetMMCReserveSpace()
{
    unsigned int volatile *ptr;
    UINT32 volatile bmark, emark;
    UCHAR _fmi_ucBuffer[512];
    unsigned int volatile size=0;
    ptr = (unsigned int *)((UINT32)_fmi_ucBuffer | 0x80000000);
    fmiSD_Read(MMC_INFO_SECTOR,1,(UINT32)ptr);
    bmark = *(ptr+125);
    emark = *(ptr+127);
    MSG_DEBUG("ReserveSpace=>bmark=0x%08x,emark=0x%08x\n",bmark,emark);
    if ((bmark == 0x11223344) && (emark == 0x44332211))
        size=*(ptr+126);
    else
        size=0;
    return size;
}
UINT32 GetMMCImageInfo(unsigned int *image)
{
    UINT32 volatile bmark, emark;
    int volatile i, imageCount=0;
    unsigned int volatile *ptr;
    FW_MMC_IMAGE_T *pmmcimage=NULL;
    UCHAR _fmi_ucBuffer[512];

    ptr = (unsigned int *)((UINT32)_fmi_ucBuffer | 0x80000000);
    fmiSD_Read(MMC_INFO_SECTOR,1,(UINT32)ptr);
    bmark = *(ptr+0);
    emark = *(ptr+3);
    MSG_DEBUG("bmark=0x%08x,emark=0x%08x\n",bmark,emark);
    if ((bmark == 0xAA554257) && (emark == 0x63594257)) {
        imageCount = *(ptr+1);

        /* pointer to image information */
        ptr = ptr+4;
        pmmcimage=(FW_MMC_IMAGE_T *)image;
        for (i=0; i<imageCount; i++) {
            /* fill into the image list buffer */
            pmmcimage->actionFlag=0;
            pmmcimage->fileLength = 0;
            pmmcimage->imageNo= *(ptr) & 0xffff;
            memcpy((CHAR *)pmmcimage->imageName,(char *)(ptr+4),16);
            pmmcimage->imageType = (*(ptr) >> 16) & 0xffff;
            pmmcimage->executeAddr = *(ptr+2);
            pmmcimage->flashOffset = *(ptr+1);
            pmmcimage->endAddr = *(ptr+3);
            MSG_DEBUG("\nNo[%d], Flag[%d], name[%s] exeAdr[%d] flashOff[%d] ednAdr[%d]\n\n",
                      pmmcimage->imageNo,
                      pmmcimage->imageType,
                      pmmcimage->imageName,
                      pmmcimage->executeAddr,
                      pmmcimage->flashOffset,
                      pmmcimage->endAddr
                     );
            pmmcimage += 1;
            ptr = ptr+8;
        }
    } else
        imageCount = 0;

    return imageCount;
}


int SetMMCImageInfo(FW_MMC_IMAGE_T *mmcImageInfo)
{
    int i, count=0;
    unsigned char *pbuf;
    unsigned int *ptr, *pImage;
    UCHAR _fmi_ucBuffer[512];

    pbuf = (UINT8 *)((UINT32)_fmi_ucBuffer | 0x80000000);
    ptr = (unsigned int *)((UINT32)_fmi_ucBuffer | 0x80000000);


    fmiSD_Read(MMC_INFO_SECTOR,1,(UINT32)ptr);

    pImage = ptr+4;

    if (((*(ptr+0)) == 0xAA554257) && ((*(ptr+3)) == 0x63594257)) {
        count = *(ptr+1);

        /* pointer to image information */
        for (i=0; i<count; i++) {
            if ((*pImage & 0xffff) == mmcImageInfo->imageNo) {
                pmmcUpdateImage = pImage;
                bIsmmcMatch = TRUE;
                break;
            }
            /* pointer to next image */
            pImage += 8;
        }
    } else
        memset(ptr,0xFF,SD_SECTOR);
    /* update image information */
    *(ptr+0) = 0xAA554257;
    if (!bIsmmcMatch) {
        *(ptr+1) = count+1;
        pmmcUpdateImage = (ptr+4) + (count * 8);
    }
    *(ptr+3) = 0x63594257;
    *(pmmcUpdateImage+0) = (mmcImageInfo->imageNo & 0xffff) | ((mmcImageInfo->imageType & 0xffff) << 16);   // image number / type
    *(pmmcUpdateImage+1) = mmcImageInfo->flashOffset;
    *(pmmcUpdateImage+2) = mmcImageInfo->executeAddr;
    *(pmmcUpdateImage+3) = mmcImageInfo->flashOffset+((mmcImageInfo->fileLength+SD_SECTOR-1)>>9)-1;
    memcpy((char *)(pmmcUpdateImage+4), mmcImageInfo->imageName, 16);   // image name

    fmiSD_Write(MMC_INFO_SECTOR,1,(UINT32)pbuf);

    return Successful;
}

int ChangeMMCImageType(UINT32 imageNo, UINT32 imageType)
{
    int i, count;
    unsigned int *ptr;
    UCHAR _fmi_ucBuffer[512];

    ptr = (unsigned int *)((UINT32)_fmi_ucBuffer | 0x80000000);

    fmiSD_Read(MMC_INFO_SECTOR,1,(UINT32)ptr);

    if (((*(ptr+0)) == 0xAA554257) && ((*(ptr+3)) == 0x63594257)) {
        count = *(ptr+1);

        /* pointer to image information */
        ptr += 4;
        for (i=0; i<count; i++) {
            if ((*ptr & 0xffff) == imageNo) {
                *ptr = ((imageType & 0xffff) << 16) | (imageNo & 0xffff);
                break;
            }
            /* pointer to next image */
            ptr = ptr+8;
        }
    }

    fmiSD_Write(MMC_INFO_SECTOR,1,(UINT32)_fmi_ucBuffer);

    return Successful;
}

int DelMMCImage(UINT32 imageNo)
{
    int i=0, count;
    unsigned int *ptr,*ptr2;
    //UCHAR _fmi_ucBuffer[512];
    UCHAR *_fmi_ucBuffer=(UCHAR *)(DOWNLOAD_BASE);
    ptr2 = ptr = (unsigned int *)((UINT32)_fmi_ucBuffer | 0x80000000);
    MSG_DEBUG("Del mmc flash Image imageNo=%d ...\n",imageNo);
    SendAck(10);
    fmiSD_Read(MMC_INFO_SECTOR,1,(UINT32)_fmi_ucBuffer);
    if(imageNo==0xffffffff) { // clear all
        memset((char *)_fmi_ucBuffer+16,0xff,512-16-12);
        *(ptr+1)=0x0;
        fmiSD_Write(MMC_INFO_SECTOR,1,(UINT32)_fmi_ucBuffer);
        SendAck(100);
        return Successful;
    }
    SendAck(40);

    if (((*(ptr+0)) == 0xAA554257) && ((*(ptr+3)) == 0x63594257)) {
        count = *(ptr+1);

        /* pointer to image information */
        ptr += 4;
        for (i=0; i<count; i++) {
            if ((*(ptr) & 0xffff) == imageNo) {
                *(ptr2+1) = count - 1;  // del one image
                memcpy((char *)ptr, (char *)(ptr+8), (count-i-1)*32);
                MSG_DEBUG("Get Del mmc flash Image imageNo=%d ...\n",i);
                /* send status */
                fmiSD_Write(MMC_INFO_SECTOR,1,(UINT32)_fmi_ucBuffer);
                break;
            }
            /* pointer to next image */
            ptr = ptr+8;
        }
    }

    SendAck(100);
    return Successful;
}

void GetMMCImage(void)
{
    int count=0;
    MSG_DEBUG("Get mmc flash Image ...\n");
    pImageList=((unsigned char*)(((unsigned int)imageList)|NON_CACHE));
    memset(pImageList, 0, sizeof(imageList));
    /* send image info to host */
    *(unsigned int *)(pImageList+0) = GetMMCImageInfo((unsigned int *)(pImageList+8));
    *(unsigned int *)(pImageList+4) = GetMMCReserveSpace();
    usb_send(pImageList, 8);
    count = *(unsigned int *)pImageList;
    if (count < 0)
        count = 0;
    MSG_DEBUG("count=%d,ReserveSpace=%d\n",count,*(unsigned int *)(pImageList+4));
    usb_send(pImageList+8, count*(sizeof(FW_MMC_IMAGE_T)));
    MSG_DEBUG("finish get mmc image [%d]!!\n", count);
}
