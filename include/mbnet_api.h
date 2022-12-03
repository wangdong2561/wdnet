#ifndef __MBNET_MODULE_H__
#define __MBNET_MODULE_H__

#ifdef __cplusplus
extern "C" {
#endif

/*最大值限制*/
#define MBNET_MAX_VENDOR_NAME     16	/*模块商厂名的最大长度*/
#define MBNET_MAX_PRODUCT_NAME    16	/*模块产品名的最大长度*/
#define MBNET_MAX_APN_LEN         30  /*拨号时,APN的最大长度*/
#define MBNET_MAX_CALL_NUM        10  /*拨号时,拨号号码的最大长度*/
#define MBNET_MAX_USENAME_LEN     50  /*拨号时,用户名的最大长度*/
#define MBNET_MAX_PASSWORD_LEN    30  /*拨号时，密码的最大长度*/
#define MBNET_MAX_IMSI_LEN        20	/*sim卡的imsi号的最大长度*/
#define MBNET_MAX_IMEI_LEN        20	/*模块的imei号的最大长度*/
#define MBNET_MAX_PHYSLOT_LEN	    64  /*模块所在物理槽位号的最大长度*/
#define MBNET_MAX_NETVENDOR_NAME  64	/*运营商名的最大长度*/
#define MBNET_MAX_INTERFACE_NAME  32	/*网络接口名的最大长度，如ppp0,usb0*/

#define MBNET_MAX_PHONE_NUM	    16
#define MBNET_MAX_SMS_LEN	    256

/*网络扫描模式*/
#define MBNET_NETSCANMODE_AUTO		0   /* 4G > 3G > 2G ，自动搜网 */
#define MBNET_NETSCANMODE_2G_ONLY	1   /* 2G网络 */
#define MBNET_NETSCANMODE_3G_ONLY	2   /* 3G网络 */
#define MBNET_NETSCANMODE_4G_ONLY	3   /* 4G网络 */

/*目前支持的网络制式*/

#define  MBNET_MODE_UNKNOWN                0	/*未定义的网络类型*/
#define  MBNET_2G_MODE_GSM                 1<<0	/*第二代移动通信GSM网络*/
#define  MBNET_2G_MODE_CDMA                1<<1 /*第二代移动通信CDMA网络*/
#define  MBNET_3G_MODE_WCDMA               1<<2 /*第三代移动通信WCDMA网络*/
#define  MBNET_3G_MODE_CDMA2000            1<<3	/*第三代移动通信cdma2000网络或者叫EVDO网络*/
#define  MBNET_3G_MODE_TDSCDMA             1<<4	/*第三代移动通信TDSCDMA网络*/
#define  MBNET_4G_MODE_LTE		   1<<5	/*LTE总称*/
#define  MBNET_4G_MODE_TDLTE               1<<6 /*第四代移动通信TDLED网络，目前属于中国移动*/
#define  MBNET_4G_MODE_TELECOM_FDDLTE      1<<7 /*第四代移动通信FDDLTE网络，中国电信*/
#define  MBNET_4G_MODE_UNICOM_FDDLTE       1<<8 /*第四代移动通信FDDLTE网络，中国联通*/
#define  MBNET_3G_MODE_OPERATORS	   1<<9 /*国外运营商3G网络*/
#define  MBNET_4G_MODE_OPERATORS	   1<<10 /*国外运营商4G网络*/

/*运营商*/

#define MBNET_CHINA_UNICOM			0	/*中国联通*/
#define MBNET_CHINA_TELECOM			1	/*中国电信*/
#define MBNET_CHINA_MOBILE			2	/*中国移动*/

/*鉴权方式*/
#define MBNET_AUTHENTICATION_TYPE_NONE 0
#define MBNET_AUTHENTICATION_TYPE_PAP  1
#define MBNET_AUTHENTICATION_TYPE_CHAP 2
#define MBNET_AUTHENTICATION_TYPE_AUTO 3

/*返回值类型*/

#define MBNET_OK         	0	/*成功*/
#define MBNET_NO_DEVICE       -1001	/*没有找到设备*/
#define MBNET_NO_SIM_IN       -1002	/*没有发现sim卡*/
#define MBNET_NO_SIGNAL       -1003	/*没有信号*/
#define MBNET_INVALID_PA      -1004	/*参数不合法*/
#define MBNET_ERROR      	-1005	/*其它错误*/


/*系统事件*/

#define MBNET_CARD_MSG_IN             1<<0	/*有短信进来事件*/
#define MBNET_CARD_CALL_IN            1<<1	/*有电话进来事件*/
#define MBNET_CARD_LINK_UP            1<<2	/*模块上线事件*/
#define MBNET_CARD_LINK_DOWN          1<<3  	/*模块下线事件*/
#define MBNET_CARD_SIM_EVENT          1<<4	/*SIM卡插入事件*/
#define MBNET_CARD_IMSI               1<<5	/*IMSI获得事件*/
#define MBNET_CARD_NET_CHANGED	1<<6	/*网络制式变化事件*//*此事件业务需要处理，调用mbnet_stop，再调用mbnet_start*/
#define MBNET_CARD_PLUGIN		1<<7	/*模块插入事件*/
#define MBNET_CARD_PLUGOUT		1<<8	/*模块移除事件*/
#define MBNET_CARD_NET_VENDOR		1<<9	/*检测到运营商事件*/

/*回调时的信息体*/
typedef struct {
	/*软件层面上的卡号*/
	int card;
	/*事件号*/
	int event;
	/*事件附带的数据*/
	const void *data;
}callback_t;

/*短信、电话时，回调函数参数中的*data原始结构*/
typedef struct {
	char phone[MBNET_MAX_PHONE_NUM];
	char msg[ MBNET_MAX_SMS_LEN];
}sms_t;
/*回调函数类型*/
typedef void (*CardCB) (const callback_t *p);


/*拨号时，用户配制信息*/
typedef struct {

	/*拨号时，用户填的APN信息*/
	char sAPN[MBNET_MAX_APN_LEN + 1];
	/*拨号时，用户填的拨号号码*/
	char sCallNum[MBNET_MAX_CALL_NUM + 1];
	/*拨号时，用户填的用户名*/
	char sUserName[MBNET_MAX_USENAME_LEN + 1];
	/*拨号时，用户填的密码*/
	char sPassWord[MBNET_MAX_PASSWORD_LEN + 1];
	/*拨号时，用户填的鉴权方式*/
	int iAuthentication;
	/*用户填的搜网模式，任何时候都可配置*/
	int iNetScanMode;
}dial_t;

typedef struct {

	/*模块的产品号*/
	unsigned int Pid;
	/*模块的厂商号*/
	unsigned int Vid;
	/*模块的厂商名*/
	char sVendor[MBNET_MAX_VENDOR_NAME + 1];
	/*模块的产品名*/
	char sProduct[MBNET_MAX_PRODUCT_NAME + 1];
	/*物理接口*/
	char PhySlot[MBNET_MAX_PHYSLOT_LEN];
	/*软件层次上的卡号*/
	int CardNo;
	/*系统扫描后ttyUSB的起始号码*/
	int  ttyUSBBase;

	/*上面是扫描后可获得信息，后面是驱动加载后方可获得的信息*/

	/*真正的AT通道号,由base+drier中指定的偏移值计算而来*/
	int  ttyUSBAt;
	/*真正的数据通道号,由base+drier中指定的偏移值计算而来*/
	int  ttyUSBData;
	/*模块的能力集，都支持哪些网络制式*/
	int Capability;
	/*是否有simcard*/
	int SimCard;    /*255--Invalid; 1--valid; other value--Invalid*/
	/*IMIS*/
	char IMSI[MBNET_MAX_IMSI_LEN];
	/*模块的IMEI*/
	char IMEI[MBNET_MAX_IMEI_LEN];
	/*目前的运营商*/
	char NetVendor[MBNET_MAX_NETVENDOR_NAME ];
	/*目前注册上的网络*/
	int CurrentNetMode;

	/*因不能网络下，标准不一，
	  此处根据CurrentNetMode换算后的值,
	  向用户统一
	 */
	int SigStrength;      

	/*拨上号对应的网络接口*/
	char iface[MBNET_MAX_INTERFACE_NAME ];

	/*模块上线总时间*/
	unsigned int iOnLineTime; 
	/*模块总共收到的数据*/
	unsigned int iInDataCount;
	/*模块总共发送的数据*/
	unsigned int iOutDataCount;
	/*模块的本端地址*/
	unsigned int local_ipaddr;
	/*模块的远端地址*/
	unsigned int peer_ipaddr;
	/*模块的网关地址*/
	unsigned int gateway;
	/*模块的子网掩码*/
	unsigned int netmask;
	/*模块的第一dns地址*/
	unsigned int dns1_addr;
	/*模块的第二dns地址*/
	unsigned int dns2_addr;

} card_t;

/**
  函数名：MbnetInit
  参数：cb
  参数产明：回调函数

  返回值：初始化后发现的模块数

  功能说明：
	用于初始化mbnet库。
 */
int MbnetInit( CardCB cb);

/*
函数名：MbneExit
功能说明：
	用于从mbnet退出，销毁mbnet建立的所有环境!	
*/

void MbnetExit( void );

/* 

   函数名：MbnetGetCardInfo
   参数：
	CardNo ----要操作的卡号
	p      -----指向获得卡信息的地址
	

   返回值：成功返回0,其它值为失败!

   功能说明：
   此函数获得移动卡的网络类信息

 */

int MbnetGetCardInfo(int CardNo, card_t *p);


/*
   函数名：MbnetStart
   参数：
   CardNo：指定要操作的模块号
   pt:拨号参数

   返回值：0代表申请成功，其它申请失败

   注意：因此接口为异步，申请成功，不代表拨号成功；

   是否拨号成功还是要依据回调的事件来确认

*/
int MbnetStart(int CardNo, dial_t *pt);

/*
   函数名：MbnetStop
   参数:
   CardNo:　要操作的卡号
   返加值：0成功，其它失败。

   功能说明：
   断开指定模块的网络
*/

int MbnetStop(int CardNo);

/*
   函数名：MbnetNetScanMode
   参数：
   CardNo：指定要操作的模块号
   pt:拨号参数

   返回值：0代表成功，其它失败

   注意：1.此接口根据模块型号不同底层实现不同，模块如果不支持某种搜网模式设置，
         则该接口不会产生任何效果；
         2.此接口只使用dial_t内的 iNetScanMode，忽略其他参数

   功能说明：设置模块的搜网模式
*/
int MbnetNetScanMode(int CardNo, dial_t *pt);

/*
   函数名：MbnetReset
   参数：
   CardNo：指定要操作的模块号

   返回值：0代表成功，其它失败

   注意：1.此接口根据模块型号不同底层实现不同，模块如果不支持复位,则该接口不会产生任何效果；

   功能说明：设置模块软复位
*/
int MbnetReset(int CardNo);

/*
   函数名：MbnetGetVersion

   返回值：版本号的字符串地址

   功能说明：获得mbnet的版本信息。
*/

char *MbnetGetVersion();

/*
   函数名：MbnetDebug
   参数：
   enable =1 开启调试模式；enable=0关闭调试模式
 */

void MbnetDebug(int enable);

#ifdef __cplusplus
}
#endif
#endif
