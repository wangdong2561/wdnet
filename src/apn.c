/*
   zhaoxin@kedacom.com
   2016.3.15

 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "mbnet.h"
#include "mbnet_api.h"
#define   MAX_VENDOR_NUM	5
#define	  MAX_APN_NUM		3
#define   MAX_IMSI_NUM		3

typedef struct {
	int 	NetMode;
	char APN[MBNET_MAX_APN_LEN +1 ];
	char CallNum[MBNET_MAX_CALL_NUM +1];
	char UserName[MBNET_MAX_USENAME_LEN +1];
	char PassWord[MBNET_MAX_PASSWORD_LEN +1];
}ApnInfo;


typedef struct {
	char 	NetVendor[MBNET_MAX_NETVENDOR_NAME ];
	int	ImsiHead[ MAX_IMSI_NUM ];
	ApnInfo		ApnInfo[ MAX_APN_NUM];
}VendorInfo;


static VendorInfo gChinaMobile = 
{

	.NetVendor = "china mobile",

	.ImsiHead = {46000,46002,46007},

	.ApnInfo[0] = 
	{
		.NetMode = MBNET_4G_MODE_TDLTE,
		.APN  =  "CMNET"	,
		.CallNum	=  "*99#",
	},

	.ApnInfo[1] = 
	{
		.NetMode = MBNET_3G_MODE_TDSCDMA| \
			   MBNET_2G_MODE_GSM,
		.APN  =  "CMNET"	,
		.CallNum	=  "*99#"
	},

};


static VendorInfo gChinaUnicom = 
{

	.NetVendor = "china unicom",

	.ImsiHead = {46001},

	.ApnInfo[0] = 
	{
		.NetMode = MBNET_4G_MODE_UNICOM_FDDLTE,
		.APN  =  "3gnet" ,
		.CallNum	=  "*99#",
	},
	.ApnInfo[1] = 
	{
		.NetMode = MBNET_3G_MODE_WCDMA| \
			   MBNET_2G_MODE_GSM,
		.APN  =  "3gnet"	,
		.CallNum	=  "*99#",
	},


};


static VendorInfo gChinaTelecom = 
{

	.NetVendor = "china telecom",
	.ImsiHead = {46003 ,46005, 46011},
	.ApnInfo[0] = 
	{
		.NetMode = MBNET_3G_MODE_CDMA2000,
		.APN =  "ctnet",
		.CallNum	=  "#777",
		.UserName = "ctnet@mycdma.cn",
		.PassWord = "vnet.mobi",
	},
	.ApnInfo[1] = 
	{
		.NetMode = MBNET_2G_MODE_GSM | MBNET_4G_MODE_TELECOM_FDDLTE,
		.APN =  "ctlte",
		.CallNum	=  "*99#",
	},
	/* 预留....*/
};

static VendorInfo gChinaMobileIOT =
{

	.NetVendor = "china mobile iot",

	//46004 in not a regular IMSI head, it's IOT card.
	.ImsiHead = {46004},

	.ApnInfo[0] =
	{
		.NetMode = MBNET_4G_MODE_TDLTE | MBNET_3G_MODE_TDSCDMA| \
			   MBNET_2G_MODE_GSM,
		.APN  =  "CMIOT"        ,
		.CallNum        =  "*99#"
	},

};

static VendorInfo gChinaUnicomIOT =
{

	.NetVendor = "china unicom iot",

	//46006 in not a regular IMSI head, it's IOT card.
	.ImsiHead = {46006},

	.ApnInfo[0] =
	{
		.NetMode = MBNET_4G_MODE_UNICOM_FDDLTE | MBNET_3G_MODE_WCDMA| \
			   MBNET_2G_MODE_GSM,
		.APN  =  "wonet"        ,
		.CallNum        =  "*99#"
	},

};

VendorInfo *gVendorInfo[ MAX_VENDOR_NUM ]={

	&gChinaTelecom ,
	&gChinaUnicom, 
	&gChinaMobile,
	&gChinaMobileIOT,
	&gChinaUnicomIOT,

};

/*根据运营商名、或者IMSI与当前注册上的网络来获得通用APN**/
int get_general_apn(cardinst_t *inst)
{
	if( NULL == inst )
		return MBNET_ERROR;

	VendorInfo *vendor = NULL;
	card_t *dev = &inst->dev;
	dial_t *cfg = &inst->cfg;

	int num = 0;
	int i = 0;
	if(!strlen(inst->dev.NetVendor) && !strlen(inst->dev.IMSI))
		return MBNET_ERROR;

	for(num = 0; num < MAX_VENDOR_NUM; num++){
		vendor = gVendorInfo[num];
		if(!strcmp(dev->NetVendor, vendor->NetVendor)){

			for( i =0 ; i< MAX_APN_NUM; i++){
				if(dev->CurrentNetMode & vendor->ApnInfo[i].NetMode){
					memcpy( cfg->sAPN, vendor->ApnInfo[i].APN, MBNET_MAX_APN_LEN );
					memcpy( cfg->sCallNum, vendor->ApnInfo[i].CallNum, MBNET_MAX_CALL_NUM );
					if(strlen(vendor->ApnInfo[i].UserName)){
						memcpy( cfg->sUserName, vendor->ApnInfo[i].UserName, MBNET_MAX_USENAME_LEN);
					}
					if(strlen(vendor->ApnInfo[i].PassWord)){
						memcpy( cfg->sPassWord, vendor->ApnInfo[i].PassWord, MBNET_MAX_PASSWORD_LEN);
					}
					return MBNET_OK;
				}
			}
			/*if it hasn't netmode, we will write a default apn*/
			memcpy( cfg->sAPN, vendor->ApnInfo[0].APN, MBNET_MAX_APN_LEN );
			memcpy( cfg->sCallNum, vendor->ApnInfo[0].CallNum, MBNET_MAX_CALL_NUM );
			if(strlen(vendor->ApnInfo[0].UserName)){
				memcpy( cfg->sUserName, vendor->ApnInfo[0].UserName, MBNET_MAX_USENAME_LEN);
			}
			if(strlen(vendor->ApnInfo[0].PassWord)){
				memcpy( cfg->sPassWord, vendor->ApnInfo[0].PassWord, MBNET_MAX_PASSWORD_LEN);
			}
			return MBNET_OK;
		}
	}

	return MBNET_ERROR;
}
/*根据IMSI来获得运营商名*/
int get_net_vendor(card_t *dev)
{
	char head_imsi[6];
	int  imsi = 0;
	int  num = 0;
	VendorInfo *vendor = NULL;
	int i = 0;
	if( NULL == dev )
		return MBNET_ERROR;
	if( !strlen(dev->IMSI) )
		return MBNET_ERROR;
	memset(head_imsi, 0, sizeof(head_imsi));
	memcpy(head_imsi, dev->IMSI, sizeof(head_imsi));
	head_imsi[5] = 0;
	imsi = (int)atoi(head_imsi);

	for(num = 0; num < MAX_VENDOR_NUM; num++){
		vendor = gVendorInfo[num] ;

		for(i = 0; i< MAX_IMSI_NUM ;i++){
			if(vendor->ImsiHead[i] == imsi){
				strcpy(dev->NetVendor, vendor->NetVendor);
				return MBNET_OK;
			}
		}
	}
	strcpy(dev->NetVendor, "LOCAL OPERATOR");

	return MBNET_ERROR;
}
