#ifndef __MBNET_CORE_H__
#define __MBNET_CORE_H__
#include <pthread.h>
#include "mbnet_api.h"
#include "list.h"


/*模块已经停止工作,线程已经退出*/
#define CARD_STATUS_STOPPED	1<<0
/*模块服务线程已经开始运行*/
#define CARD_STATUS_STARTED	1<<1
/*模块已经被初始化*/
#define CARD_STATUS_INITED	1<<2
/*模块已经检测到simcard*/
#define CARD_STATUS_SIMED	1<<3
/*模块已经检测到运营商*/
#define CARD_STATUS_VENDOR	1<<4
/*模块已经注册上网络*/
#define CARD_STATUS_REGISTERED	1<<5
/*模块正在拨号*/
#define CARD_STATUS_UPPING	1<<6
/*模块已经拨上号*/
#define CARD_STATUS_DIALED	1<<7
/*模块正在关闭*/
#define CARD_STATUS_DOWNING	1<<8
/*模块处在调试模式*/
#define CARD_STATUS_DEBUG	1<<9
/*模块处在退出模式*/
#define CARD_STATUS_DEBUG_EXIT	1<<10


#define PPPD_DIAL_TYPE		0
#define NDIS_DIAL_TYPE		1
#define QMANAGE_DIAL_TYPE	2
/*此结构用于process与管理线程之间交互*/

/*用于msg结构中的设置mask*/
#define	SIMCARD_MASK		1<<0
#define NETMODE_MASK		1<<1
#define SIGNAL_MASK		1<<2
#define IMSI_MASK		1<<3
#define SMS_MASK		1<<4
#define IMEI_MASK		1<<5
#define NDIS_DISCON_MASK	1<<6

typedef struct msg{
	/*记录哪些项被设置了,共六项*/
	unsigned int mask;
	int simcard;
	int netmode;
	int signal;      
	char imsi[MBNET_MAX_IMSI_LEN];
	sms_t sms;
	char imei[MBNET_MAX_IMEI_LEN];
}msg_t;


typedef struct {

	unsigned int request;
	struct list_head node;

}request_t;
/*用于模块状态管理*/
typedef struct thread {

	/*结构同步锁　*/
	pthread_mutex_t thread_mutex;
	/*当前模块运行状态*/
	unsigned int flag;	
	/*模块权限抢占之前的状态*/
	unsigned int old_flag;
	/*存储外部请求链表*/
	struct list_head req;
	/*模块上线时的时间点*/
	long onLine;
	//实例引用计算
	unsigned int count;
	/*AT通道描述符*/
	int  at_fd;
	/*AT通道write锁*/
	pthread_mutex_t fd_mutex;
	/*拨号程序的进程号*/
	pid_t dial_pid;
	/*模块服务线程号*/
	pthread_t server_tid;

}thread_t;

typedef struct instance {

	/*用于保护dev与status,其它的不用保护*/
	pthread_mutex_t dev_mutex;
	card_t dev;

	/*配制信息*/
	dial_t cfg;

	/*驱动指针*/
	const struct driver *driver;

	thread_t thread_info;
	/*添加到实例链表上*/
	struct list_head node;

}cardinst_t;

typedef struct driver {

	unsigned int Pid;
	unsigned int Vid;
	char sVendor[MBNET_MAX_VENDOR_NAME + 1];
	char sProduct[MBNET_MAX_PRODUCT_NAME + 1];
	int datapath;//offset
	int atpath;//offset
	int DialType;
	/*模块的能力集，都支持哪些网络制式*/
	int Capability;

	/*
	  初始化,模块必须实现此接口

	 */
	void (*init)( const thread_t *thread, const card_t *dev);

	/*
	  simcard状态请求,模块必须实现此接口

	 */

	void (*sim)(  const thread_t *thread, const card_t *dev);

	/*
		获得imsi请求,模块必须实现此接口
	*/

	void (*imsi)( const thread_t *thread , const card_t *dev);

	/*
		信号强度请求,模块必须实现此接口

	*/

	void (*signal)( const thread_t *thread, const card_t *dev );


	/*
		注册上的网络请求,模块必须实现此接口
	*/
	void (*netmode)( const thread_t *thread, const card_t *dev);

	/*
		网络注册模式设置,模块可以选择实现此接口
	*/
	void (*netscanmode)( const thread_t *thread, const card_t *dev, const dial_t *cfg);

	/*
	   这个接口是为有些模块定制的，正常的模块此接口可以不实现，
	   但是有些模块如果要求在拨号时候还要对模块操作，则要调用此接口
	   去处理一些额外的配制，这个接口就要有用了*/

	void (*fix)(  const thread_t *thread, const card_t *dev,  const dial_t *cfg );

	/*
	   负责收集模块的相关信息,模块必须实现此接口，主要针对上面请求后，
	
	　　处理模块的返回信息。
	 */

	int(*process)( const thread_t *thread, const card_t *dev, msg_t *msg);

	/*
	   当断开拨号时，要做的模块预处理接口,此接口根据模块特性，可选实现
	 */
	void(*stop)( const thread_t *thread , const card_t *dev);

	/*
	   整个阶段有效，此接品用于复位模块,模此情况下，模块要被复位,可选实现　
	 */

	void (*reset)( const thread_t *thread, const card_t *dev );
	
	/*用于存储拨号相关的信息,具体结构与拨号类型有关,支持动态扩展*/
	const void *data;
	
	/*用于调试模式,发一条命令*/
	void (*send_cmd)(const thread_t *thread, char *cmd);
	/*用于调试模式，接收一条信息*/
	int (*get_one_msg)(const thread_t *thread, char *msg, unsigned int len);

	/*添加到driver链表上*/
	struct list_head node;

}driver_t;

#endif /*__3G_MODULE_H__*/
