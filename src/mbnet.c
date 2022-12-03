#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <dirent.h>

#ifndef aligned_u64
#define aligned_u64 unsigned long long __attribute__((aligned(8)))
#endif

#include <linux/if.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <sys/sysinfo.h>
#include <signal.h>
#include <dlfcn.h>
#include <sys/select.h>
#include <errno.h>

#include <netinet/ether.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "mbnet.h"
#include "mbnet_api.h"
#include "tty.h"
#include "list.h"
#include "debug.h"
#include "pppd.h"
#include "ndis.h"
#include "qmanage.h"
#include "driver.h"
#include "apn.h"
#include "auto_modsubver.h"

#define MAIN_VERSION   1
#define MAJOR_VERSION  0
#define MINOR_VERSION  2

#define USB2SERIALFILE	"/proc/tty/driver/usbserial"	
/*此链表用于存储扫描到的设备，只在单线程中使用，不用考虑同步问题*/
static LIST_HEAD(scanned_devices);

pthread_mutex_t inactive_mutex;
/*此链表,用于存储被扫描到的但是没有驱动的设备*/
static LIST_HEAD(inactive_devices);
/*此链表存在多线程操作可能，一个是用户读取、另一个是库内线程增加、删除*/

struct{
	pthread_mutex_t mutex;	
	/*32bit数据用作bitmap已经够用了，虽说支持动态热插拨，设备上不可能超过32个模块！！！*/
	unsigned int bitmap;
	unsigned int cardnums;
	struct list_head head;
}active_devices;

static CardCB  g_cb = NULL;
static pthread_t   g_scan_tid;
static pthread_t   g_debug_tid;

extern int advrt_do_route_cmd(char *cmd);
extern int advrt_do_rule_cmd(char *cmd);

extern int get_net_vendor(card_t *dev);

static int devices_scan( void );

static char version[128];

/*增加计算*/
int get_device( cardinst_t *inst)
{
	thread_t *thread = NULL;

	if( !inst )
		return MBNET_ERROR;
	thread = &inst->thread_info;

	pthread_mutex_lock( &thread->thread_mutex);
	thread->count++;
	pthread_mutex_unlock( &thread->thread_mutex);

	return MBNET_OK;
}

/*减少计数*/
int  put_device( cardinst_t *inst)
{
	thread_t *thread = NULL;
	if( !inst )
		return MBNET_ERROR;

	thread = &inst->thread_info;

	pthread_mutex_lock( &thread->thread_mutex);
	thread->count--;
	pthread_mutex_unlock( &thread->thread_mutex);
	return MBNET_OK;
}

static int is_iface_up(char *name)
{
    struct ifreq ifr;
    int fd = -1;
    
    if(NULL == name)
        return -1;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0) {
	LOGE("socket create fail\n");
        return -1;
    }

    strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));
    if( ioctl(fd,SIOCGIFFLAGS,(caddr_t)&ifr) < 0 ){
        close(fd);
        return 0; 
    }
    
    close(fd);

    if(ifr.ifr_flags & IFF_UP)
        return 1;
    else
        return 0;

}

static int start_dial( cardinst_t *inst)
{

	const driver_t *driver = NULL;
	if(!inst)
		return MBNET_ERROR;

	driver = inst->driver;

	if( driver->DialType == PPPD_DIAL_TYPE){
		if(pppd_start( inst))
			return MBNET_ERROR;
	}else if( driver->DialType == NDIS_DIAL_TYPE){
		if(ndis_start( inst))
			return MBNET_ERROR;
	}else if( driver->DialType == QMANAGE_DIAL_TYPE){
		if(qmanage_start( inst))
			return MBNET_ERROR;
	}else

		return MBNET_ERROR;

	return MBNET_OK;

}

static void stop_dial( cardinst_t *inst)
{

	const driver_t *driver = NULL;

	driver = inst->driver;

	if( driver->DialType == PPPD_DIAL_TYPE){
		pppd_stop( inst);

	}else if( driver->DialType == NDIS_DIAL_TYPE){
		ndis_stop( inst);
	}else if( driver->DialType == QMANAGE_DIAL_TYPE){
		qmanage_stop( inst);
	}


}

int get_gateway(unsigned int *gateway, char *iface)
{
	char devname[64];
	unsigned long d, g, m;
	int flgs, ref, use, metric, mtu, win, ir;

	FILE *fp = fopen("/proc/net/route","r");
	if (fp == NULL){
		printf("/proc/net/route open err\n");
		return -1;
	}

	if (fscanf(fp, "%*[^\n]\n") < 0) { /* Skip the first line. */
		fclose(fp);
		return -1;                /* Empty or missing line, or read error. */
	}
	while (1) {
		int r;
		r = fscanf(fp, "%63s%lx%lx%X%d%d%d%lx%d%d%d\n",
				devname, &d, &g, &flgs, &ref, &use, &metric, &m,
				&mtu, &win, &ir);
		if (r != 11) {
			if ((r < 0) && feof(fp)) { /* EOF with no (nonspace) chars read. */
				break;
			}
			printf("gateway fscanf error");
			fclose(fp);
			return -1;
		}

#define MBNET_RTF_UP          0x0001
		if (!(flgs & MBNET_RTF_UP)) { /* Skip interfaces that are down. */
			continue;
		}

		if(strstr(devname, iface))
			break;
	}
	*gateway = (unsigned int)g;
	fclose(fp);

	return 0;
}



static int get_iface_statistics(const char *netPort, unsigned int *rx ,unsigned int  *tx)
{
    FILE *fp = NULL;
    int nbytes = 0;
    char *line = NULL;
    size_t len = 0;
    char path[128];
    unsigned int tx_byte = 0,rx_byte = 0;

    if(netPort == NULL || (!rx && !tx))
    {
        printf("arg null\n");
        return -1;
    }

    memset(path, 0, sizeof(path));

    sprintf(path,"/sys/class/net/%s/statistics/tx_bytes",netPort);
    fp = fopen(path, "r");
    if(!fp)
    {
        perror("Open Failed");
        return -1;
    }

    if((nbytes = getline(&line, &len, fp)) != -1)
    {
        tx_byte = strtol(line, NULL, 10);
        *tx = tx_byte;
    }

    if(line){
        free(line);
        line = NULL;
    }

    fclose(fp);

    memset(path, 0, sizeof(path));
    sprintf(path,"/sys/class/net/%s/statistics/rx_bytes",netPort);
    fp = fopen(path, "r");
    if(!fp)
    {
        perror("Open Failed");
        return -1;
    }

    if((nbytes = getline(&line, &len, fp)) != -1)
    {
        rx_byte = strtol(line, NULL, 10);
        *rx = rx_byte;
    }

    if(line){
        free(line);
        line = NULL;
    }
    fclose(fp);
    return 0;
}



static int get_ip( char *nic , unsigned int *local_ip, unsigned int *dst_ip, unsigned int *netmask)
{
    int   sock;

    struct sockaddr_in sin;
    struct ifreq ifr;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock == -1){
        perror("socket creat");
        return -1;
    }

    strncpy(ifr.ifr_name, nic, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1]   = 0;
    if( ioctl(sock, SIOCGIFADDR, &ifr) < 0) {
        close(sock);
        return -1;
    }
    memcpy(&sin, &ifr.ifr_addr, sizeof(sin));
    *local_ip = (unsigned int)sin.sin_addr.s_addr;

    if(ioctl(sock, SIOCGIFDSTADDR, &ifr) < 0)
    {
        close(sock);
        return -1;
    }
    memcpy(&sin,   &ifr.ifr_addr,   sizeof(sin));
    *dst_ip = (unsigned int)sin.sin_addr.s_addr;

    if( ioctl(sock, SIOCGIFNETMASK, &ifr) < 0) {
	close(sock);
	return -1;
    }
    memcpy(&sin, &ifr.ifr_addr, sizeof(sin));
    *netmask =  (unsigned int)sin.sin_addr.s_addr;

    close(sock);
    return 0;
}

static int get_iface_info( char *iface, int dial_type, card_t *dev)
{
	unsigned int local_ip = 0;
	unsigned int peer_ip = 0;
	unsigned int gateway = 0;
	unsigned int netmask = 0;
	unsigned int rx = 0;
	unsigned int tx = 0;
	unsigned int dns1 = 0;
	unsigned int dns2 = 0;

	if( !is_iface_up( iface)){
		return -1;	
	}

	if(get_ip( iface , &local_ip, &peer_ip, &netmask)){

		return -1;
	}

	dev->local_ipaddr = local_ip;
	dev->peer_ipaddr = peer_ip;
	dev->netmask = netmask;

	get_gateway(&gateway,iface);
	dev->gateway = gateway;

	if( get_iface_statistics( iface , &rx ,&tx)){
		return -1;
	}	

	dev->iInDataCount = rx/1024;
	dev->iOutDataCount = tx/1024;

	if(dial_type == NDIS_DIAL_TYPE){
		ndis_get_dns(iface, &dns1, &dns2);
	}else if (dial_type == PPPD_DIAL_TYPE){
		pppd_get_dns(iface, &dns1, &dns2);
	}else if (dial_type == QMANAGE_DIAL_TYPE){
		qmanage_get_dns(iface, &dns1, &dns2);
	}

	dev->dns1_addr = dns1;
	dev->dns2_addr = dns2;
	return 0;
}

/*仅判断网络接口状态就行了*/
static int cardlink( char *iface, unsigned int *local_ip, unsigned int *dst_ip, unsigned int *netmask)
{
	if( !iface)
		return -1;
	/*是否存在？是否UP*/
	if( is_iface_up( iface) && !get_ip( iface , local_ip, dst_ip, netmask) ){
		
		return 1;
	}

	return 0;
}


static void add_route( char *iface, unsigned int ip , int index)
{
	struct in_addr addr;
	char sys_cmd[128];
	int ret;
	if( !strlen(iface))
		return;
	memset(&addr , 0, sizeof(addr));
	memset(sys_cmd, 0, sizeof(sys_cmd));
	addr.s_addr = ip;
	sprintf(sys_cmd, "add default dev %s src %s table %d",
			iface, inet_ntoa(addr), index+1);
	ret = advrt_do_route_cmd(sys_cmd);
	sprintf(sys_cmd, "add from %s table %d", 
			inet_ntoa(addr), index+1);
	ret = advrt_do_rule_cmd(sys_cmd);

}

static void del_route(  char *iface, unsigned int ip , int index)
{
	struct in_addr addr;
	char sys_cmd[128];
	int ret ;

	
	if( !strlen(iface))
		return;
	memset(&addr , 0, sizeof(addr));
	memset(sys_cmd, 0, sizeof(sys_cmd));
	addr.s_addr = ip;

	sprintf(sys_cmd, "del default dev %s src %s table %d",
			iface, inet_ntoa(addr), index+1);
	ret = advrt_do_route_cmd(sys_cmd);

	sprintf(sys_cmd, "del from %s table %d", 
			inet_ntoa(addr), index+1);
	ret = advrt_do_rule_cmd(sys_cmd);
}

/*
   模块主线程：主要完成下面三项事务

   1.处理外部请求，由请求控制状态机变化(由request控制)
   主要处理以下几个请求:
   a. debug/debug_exit  	==>request控制状态进入debug模式,或退出 
   b. down/up		==>request控制状态进入downing或者upping,实现由process到cardlink控制权转换
   c. stop		        ==>request控制状态进入stopped
   注：(down/up)请求是process到cardlink 或者cardlink到process控制权转换的触发条件。

   2.由模块自身的返回信息，控制状态机变化(由process结构控制)
   主要处理以下状态:
   a. init			==>process控制状态进入inited
   b. simcard		==>process控制状态进入=>simed
   c. imsi			==>process控制状态进入=>vendor
   d. vendor		==>process控制状态进入registered
   e. registered

   3.由网口的状态来控制状态机变化
   主要处理以下状态：
   a. upping		==>cardlink控制状态进入dialed
   b. downing		==>cardlink控制状态进入registered ，后由process控制
   c. dialed		==>cardlink控制状态进入registered, 后由process控制
 */
static void *service_thread( void *arg )
{
	cardinst_t *inst = (cardinst_t *)arg;
	const driver_t *driver = NULL;
	callback_t cbpara;
	thread_t *thread = NULL;
	card_t *dev = NULL;
	dial_t *cfg = NULL;
	unsigned int thread_flag = 0;
	unsigned int count = 0;
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct list_head pending_request;
	request_t *request = NULL;
	msg_t msg;
	char buf[128];
	unsigned int down_up_request = 0;
	unsigned int debug_request = 0;
	unsigned int stop_request = 0;

	if( !inst ){
		pthread_exit( NULL );
	}

	if( !inst->driver){
		free(inst);
		pthread_exit( NULL );
	}

	list_head_init( &pending_request);

	driver = inst->driver;
	thread = &inst->thread_info;
	dev = &inst->dev;
	cfg = &inst->cfg;

	sprintf(buf, "%s%d(%s:%s)", "mbnet_service/", inst->dev.CardNo, inst->dev.sProduct, inst->dev.PhySlot);
	prctl(PR_SET_NAME, buf);


	memset(&cbpara, 0, sizeof(callback_t));
	cbpara.card = inst->dev.CardNo;

	pthread_mutex_lock(&thread->thread_mutex);	
	thread->flag = CARD_STATUS_STARTED;
	pthread_mutex_unlock(&thread->thread_mutex);

	while ( 1 ){

		/*先处理外部请求,共有五种，stoped,dialing,down,debug,debug_exit*/
		/*其中downing,upping是反向操作，debug/debug_exit是反向操作,注意其顺序
		  最后的动作为准*/
		down_up_request = 0;
		debug_request = 0;
		stop_request = 0;
		pthread_mutex_lock(&thread->thread_mutex);	
		thread_flag = thread->flag; 
		if( !list_empty  ( &thread->req)){
			/*检查出用户最后的有效请求，忽略前面重复请求*/
			list_for_each_safe( pos ,n , &thread->req){
				request = list_entry( pos , request_t , node );	
				list_del( pos );

				if( request->request == CARD_STATUS_DEBUG_EXIT || request->request == CARD_STATUS_DEBUG)
					debug_request = request->request;

				if( request->request == CARD_STATUS_UPPING || request->request == CARD_STATUS_DOWNING)
					down_up_request = request->request;
				if( request->request == CARD_STATUS_STOPPED)
					stop_request = request->request;
				free(request);
				/*目前先采取一条条处理,后续优化处理逻辑、智能分析用户最终用意*/
				break;
			}	
		}

		pthread_mutex_unlock(&thread->thread_mutex);

		/*通过判断扫描请求链情况，来判断如何处理当前请求,由于优先级存存，每次仅处理一种请求即可*/

		/*优先处理断开，模块已经移除,其它动作无意义,包括在debug模式时*/
		if( stop_request ){


			if(thread_flag == CARD_STATUS_STOPPED)
				continue;

			pthread_mutex_lock(&thread->thread_mutex);	
			thread->flag = CARD_STATUS_STOPPED;
			pthread_mutex_unlock(&thread->thread_mutex);
			/*无论如何，先停止拨号吧!*/
			stop_dial( inst );

			/*模块存在的情况下，优先处理debug请求*/
		}else if( debug_request){
			/*如果是处在stopped模式时，其它已经无意义,包括debug*/
			if( thread_flag == CARD_STATUS_STOPPED)
				continue;
			if(debug_request == CARD_STATUS_DEBUG){

				/*已经在debug模式，无意义*/
				if( thread_flag == CARD_STATUS_DEBUG)
					continue;

				/*抢占目前线程权限*/
				pthread_mutex_lock(&thread->thread_mutex);	
				thread->old_flag = thread->flag;
				thread->flag = CARD_STATUS_DEBUG;
				pthread_mutex_unlock(&thread->thread_mutex);
			}else{
				/*并不在debug模式，无意义*/
				if(thread_flag != CARD_STATUS_DEBUG)
					continue;
				/*恢复服务线程权限*/
				pthread_mutex_lock(&thread->thread_mutex);	
				thread->flag = thread->old_flag;
				pthread_mutex_unlock(&thread->thread_mutex);
			}

			/*处理拨号与断网请求*/	
		}else if ( down_up_request){

			/*必须不是处在debug与stopped模式下，才有进一步判断的必要*/
			if(thread_flag != CARD_STATUS_DEBUG && thread_flag != CARD_STATUS_STOPPED){
				if( down_up_request == CARD_STATUS_DOWNING){
					/*是否有pending的请求待处理,如果有，则表明即将变为UPPING，既然现在DOWN操作，那么把它取消*/
					if( !list_empty( &pending_request)){

						list_for_each_safe( pos ,n ,&pending_request){
							request = list_entry( pos , request_t ,node);
							list_del(pos);
							free(request);
						}

					}
					/*非下面两种状态，不用操作状态，任其自由运行*/
					if( thread_flag != CARD_STATUS_UPPING && thread_flag != CARD_STATUS_DIALED)
						continue;

					if(driver->stop)
						driver->stop(thread , dev);
					pthread_mutex_lock(&thread->thread_mutex);	
					thread->flag = CARD_STATUS_DOWNING;
					pthread_mutex_unlock(&thread->thread_mutex);
					/*断开网络,接下来由cardlink来判断是否断开,并由cardlink返回结果决定
					  模块运行状态变更*/
					stop_dial( inst );

				}else{
					/*如果非下面状态，UPPING无意义*/
					if(thread_flag != CARD_STATUS_VENDOR && \
							thread_flag != CARD_STATUS_REGISTERED && \
							thread_flag != CARD_STATUS_DOWNING)
						continue;

					/*是否有pending的请求待处理,如果有，则此次upping无意义*/
					if( !list_empty( &pending_request) ){
						continue;	
					}

					request = (request_t *)malloc ( sizeof(request_t));
					if( !request){
						LOGE("no memory when build pending request\n");
						continue;
					}

					request->request = CARD_STATUS_UPPING;
					list_add_tail ( &request->node  ,&pending_request);

					/*为了兼容有些不太友好的模块设备，此处统一把状态变为
					  VENDOR,重新走一次网络模式判断*/

					if(driver->fix)
						driver->fix( thread, dev, cfg );
					/*通过pending请求进行迂回*/
					pthread_mutex_lock(&thread->thread_mutex);	
					thread->flag = CARD_STATUS_VENDOR;
					pthread_mutex_unlock(&thread->thread_mutex);

				}
			}
		}

		/*接收模块信息并处理*/
		/*更新网络状态*/
		memset(&msg, 0,sizeof(msg));
		driver->process(thread, dev, &msg);
		/*有没有短信？电话?*/
		if( msg.mask & SMS_MASK ){

			cbpara.event = MBNET_CARD_CALL_IN;
			if( strlen(msg.sms.msg))
				cbpara.event = MBNET_CARD_MSG_IN;
			cbpara.data = (void *)(&msg.sms);
			g_cb(&cbpara);
		}
		/*状态机切换*/	
		switch ( thread_flag ){
			case CARD_STATUS_DEBUG:
				sleep(1);
				continue;
				break;
				/*释放实例并退出线程*/
			case CARD_STATUS_STOPPED:
				pthread_mutex_lock(&thread->thread_mutex);	
				count = thread->count;
				pthread_mutex_unlock(&thread->thread_mutex);
				/*在状态为STOPED时，一旦count变为0(仅有本线程占用)，不会再有其它线程可以操作到此实例

				  因为实例已经从链表移除，其它程序不可能访问到此实例*/
				if( !count ){

					/*其它线程已经释放对inst实例的引用*/
					pthread_mutex_destroy( &inst->dev_mutex);
					pthread_mutex_destroy( &thread->thread_mutex);
					pthread_mutex_destroy( &thread->fd_mutex);
					if( !list_empty  ( &thread->req)){
						list_for_each_safe( pos ,n , &thread->req){
							request = list_entry( pos , request_t , node );	
							list_del( pos );
							free(request);
						}	
					}

					close(thread->at_fd);
					free(inst);	
					pthread_exit(0);
				}else{
					/*等待其它线程释放inst的引用*/
					usleep(50*1000);
					continue;
				}
				break;

				/*以下几个状态由process控制变更**/
			case CARD_STATUS_STARTED:
				if( driver->init)
					driver->init( thread, dev);

				pthread_mutex_lock(&thread->thread_mutex);	
				thread->flag = CARD_STATUS_INITED;
				pthread_mutex_unlock(&thread->thread_mutex);
				continue;

				break;	
				/*模块已经初始化，下一步要做的是检测simcard状态*/
			case CARD_STATUS_INITED:
				if( msg.mask & IMEI_MASK && strlen(dev->IMEI) == 0){
					pthread_mutex_lock(&inst->dev_mutex);
					strcpy(dev->IMEI, msg.imei);
					pthread_mutex_unlock(&inst->dev_mutex);
				}

				if( msg.mask & SIMCARD_MASK && strlen(dev->IMEI) != 0){
					/*检测到simcard,切换状态*/
					pthread_mutex_lock(&inst->dev_mutex);
					dev->SimCard = 1;
					pthread_mutex_unlock(&inst->dev_mutex);
					pthread_mutex_lock(&thread->thread_mutex);	
					thread->flag = CARD_STATUS_SIMED;
					pthread_mutex_unlock(&thread->thread_mutex);

					cbpara.event = MBNET_CARD_SIM_EVENT;
					cbpara.data = (void *)&(dev->SimCard);
					g_cb(&cbpara);

					continue;
				}else{
					if(driver->sim){
						driver->sim( thread, dev);
						sleep(1);
					}
					continue;
				}

				break;
				/*simcard已经检测到，下一步去获取运营商*/
			case CARD_STATUS_SIMED:
				if( msg.mask & IMSI_MASK && strlen(dev->IMEI) != 0){

					pthread_mutex_lock(&inst->dev_mutex);
					strcpy(dev->IMSI, msg.imsi);
					get_net_vendor( dev );
					pthread_mutex_unlock(&inst->dev_mutex);

					pthread_mutex_lock(&thread->thread_mutex);	
					thread->flag = CARD_STATUS_VENDOR;
					pthread_mutex_unlock(&thread->thread_mutex);

					cbpara.event = MBNET_CARD_NET_VENDOR;
					cbpara.data = (void *)(dev->NetVendor);
					g_cb(&cbpara);
					continue;

				}else{
					if(driver->imsi)
						driver->imsi( thread, dev);
					continue;
				}
				break;

				/*运营商已经拿到，下一步看注册上什么网络了*/	
			case CARD_STATUS_VENDOR:
				if(msg.mask & NETMODE_MASK){
					if( dev->CurrentNetMode != msg.netmode){
						pthread_mutex_lock(&inst->dev_mutex);
						dev->CurrentNetMode = msg.netmode;
						pthread_mutex_unlock(&inst->dev_mutex);
						cbpara.event = 	MBNET_CARD_NET_CHANGED;
						cbpara.data = (void *) &msg.netmode;
						g_cb(&cbpara);
					}

					pthread_mutex_lock(&thread->thread_mutex);	
					thread->flag = CARD_STATUS_REGISTERED;
					pthread_mutex_unlock(&thread->thread_mutex);

					continue;

				}else{
					if(driver->netmode)
						driver->netmode( thread , dev);
					continue;
				}

				break;

				/********状态变为REGISTERD后，状态变化不再由process处理结果决定*/
			case CARD_STATUS_REGISTERED:
				/*正在拨号*/
			case CARD_STATUS_UPPING:	
				/* 已经拨上号*/
			case CARD_STATUS_DIALED:
				/*正在断开网络*/
			case CARD_STATUS_DOWNING:
				if( msg.mask & NETMODE_MASK ){
					if( dev->CurrentNetMode != msg.netmode){
						pthread_mutex_lock(&inst->dev_mutex);
						dev->CurrentNetMode = msg.netmode;
						pthread_mutex_unlock(&inst->dev_mutex);

						cbpara.event = 	MBNET_CARD_NET_CHANGED;
						cbpara.data = (void *) &msg.netmode;
						g_cb(&cbpara);
					}

				}

				if( msg.mask & SIGNAL_MASK ){
					pthread_mutex_lock( &inst->dev_mutex);
					inst->dev.SigStrength = msg.signal;
					pthread_mutex_unlock( &inst->dev_mutex);
				}

				if(driver->signal)
					driver->signal( thread , dev);
				if(driver->netmode)
					driver->netmode( thread , dev);
				break;
			default:

				LOGE("unsupport card status\n");
				break;
		}

		/*接着处理未处理完的拨号请求*/
		pthread_mutex_lock(&thread->thread_mutex);	
		thread_flag = thread->flag; 
		pthread_mutex_unlock(&thread->thread_mutex);
		if( !list_empty( &pending_request) ){

			/*由于模块差异性，导致模块状态向UPPING状态切换，必须经过此迂回方式进行

			  于UPPING请求处唤应*/
			if ( thread_flag == CARD_STATUS_REGISTERED){
				list_for_each_safe( pos ,n ,&pending_request){
					request = list_entry( pos , request_t ,node);
					list_del(pos);
					free(request);
				}

				/*成功开启拨号,接下来由cardlink来判断是否拨上,并由cardlink返回结果决定
				  模块运行状态变更*/
				if(!start_dial( inst)){

					pthread_mutex_lock(&thread->thread_mutex);	
					thread->flag = CARD_STATUS_UPPING;
					pthread_mutex_unlock(&thread->thread_mutex);
				}

			}


		}

		pthread_mutex_lock(&thread->thread_mutex);	
		thread_flag = thread->flag; 
		pthread_mutex_unlock(&thread->thread_mutex);

		unsigned int local_ip;
		unsigned int peer_ip;
		unsigned int netmask;
		struct sysinfo sys_info;
		/*此三种情况下，要判断网络接口状态，并由网络状态来决定线程下一运行状态*/
		switch ( thread_flag ){

			/*cardlink 0 down ; 1 up;  -1 fail*/
			case CARD_STATUS_DOWNING:
			/*判断是否断开成功？*/
			if( 0 == cardlink( dev->iface, &local_ip, &peer_ip, &netmask) ){
				pthread_mutex_lock(&thread->thread_mutex);	
				thread->flag = CARD_STATUS_REGISTERED;
				pthread_mutex_unlock(&thread->thread_mutex);

				pthread_mutex_lock(&inst->dev_mutex);
				local_ip = dev->local_ipaddr;
				dev->local_ipaddr = 0;
				dev->peer_ipaddr = 0;
				dev->netmask = 0;
				pthread_mutex_unlock(&inst->dev_mutex);
				del_route(dev->iface, local_ip , dev->CardNo);

				cbpara.event = MBNET_CARD_LINK_DOWN;
				cbpara.data = NULL;
				g_cb(&cbpara);
			}

			break;
			case CARD_STATUS_UPPING:
			/*判断是否连上了*/
			if ( 1 == cardlink( dev->iface, &local_ip, &peer_ip, &netmask)){

				sysinfo(&sys_info);

				pthread_mutex_lock(&thread->thread_mutex);	
				thread->flag = CARD_STATUS_DIALED;
				thread->onLine = sys_info.uptime;
				pthread_mutex_unlock(&thread->thread_mutex);

				pthread_mutex_lock(&inst->dev_mutex);
				dev->local_ipaddr= local_ip;	
				dev->peer_ipaddr = peer_ip;
				dev->netmask = netmask;
				pthread_mutex_unlock(&inst->dev_mutex);
				add_route(dev->iface, local_ip , dev->CardNo);	

				cbpara.event = MBNET_CARD_LINK_UP;
				cbpara.data = NULL;
				g_cb(&cbpara);
			}

			break;

			case CARD_STATUS_DIALED:
			/*判断是否意外断掉了*/
			if ( 0 == cardlink( dev->iface, &local_ip, &peer_ip, &netmask)){
				pthread_mutex_lock(&thread->thread_mutex);
				thread->flag = CARD_STATUS_REGISTERED;
				pthread_mutex_unlock(&thread->thread_mutex);

				stop_dial( inst);

				pthread_mutex_lock(&inst->dev_mutex);
				local_ip = dev->local_ipaddr;
				dev->local_ipaddr = 0;
				dev->peer_ipaddr = 0;
				dev->netmask = 0;
				pthread_mutex_unlock(&inst->dev_mutex);
				del_route(dev->iface, local_ip , dev->CardNo);

				cbpara.event = MBNET_CARD_LINK_DOWN;
				cbpara.data = NULL;
				g_cb(&cbpara);
			}
			if( msg.mask & NDIS_DISCON_MASK ){
				cbpara.event = MBNET_CARD_LINK_DOWN;
				cbpara.data = NULL;
				g_cb(&cbpara);
			}


			break;
			default:
			/*此处不处理其它情况*/
			break;
		}

		sleep(1);
	}


}

static int create_service_thread(cardinst_t *card)
{
	int ret = -1;
	pthread_t tid;
	thread_t *thread = NULL;

	if( NULL == card )
		return MBNET_ERROR;

	thread = &card->thread_info;

	ret = pthread_create(&tid, NULL, service_thread, card);
	if(ret)
		return MBNET_ERROR;

	pthread_detach(tid);

	pthread_mutex_lock(&thread->thread_mutex);
	card->thread_info.server_tid = tid;
	pthread_mutex_unlock(&thread->thread_mutex);

	return MBNET_OK;
}

/*从bitmap中找一个空位，其实就是为0的位置*/
static int find_empty_location( unsigned int data)
{
	int l = sizeof(unsigned int) * 8;

	int i = 0;
	for ( i= 0; i< l ; i++){
		if( !(data & (1<<i)) )	
			return i;
	}

	return -1;
}

static void *scan_thread( void *arg )
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct list_head *ipos = NULL;
	int match = 0;
	int CardNo = -1;
	cardinst_t *inst = NULL;
	cardinst_t *iinst = NULL;
	const driver_t *driver = NULL;
	request_t *request = NULL;
	thread_t *thread = NULL;
	FILE *fp = NULL;
	char *line = NULL;
	ssize_t nbytes;
	int ttynum[8] = {0};
	int i = 0;
	size_t len = 0;
	char *p = NULL;
	char buf[128];
	char ttyusb[32];
	int fd = -1;

	callback_t cbpara;
	sprintf(buf, "%s", "mbnet_scan_service");
	prctl(PR_SET_NAME, buf);

	/*启动5s之后运行*/
	sleep(5);

	while(1){

		/*扫描并且更新到scanned_devices list中去*/
		if( devices_scan() <= 0){
			/*如果原来就是空的话，就是没有设备，不用其它处理*/
			if(list_empty( &active_devices.head) && list_empty( &inactive_devices)){
				sleep(5);
				continue;	
			}
		}

		/****************************************处理移除设备**************************************/

		pthread_mutex_lock(&active_devices.mutex);

		list_for_each_safe(pos, n, &active_devices.head){

			match = 0;
			inst = list_entry( pos , cardinst_t , node);

			list_for_each( ipos , &scanned_devices){
				iinst = list_entry( ipos , cardinst_t , node);

				if(inst->dev.Pid == iinst->dev.Pid && \
						inst->dev.Vid == iinst->dev.Vid && \
						! strcmp(inst->dev.PhySlot,iinst->dev.PhySlot)){
					match = 1;
					break;
				}

			}
			/*此设备在刚扫描的列表中没有，设备应该被移除了*/
			if( !match ){

				/*申请释放*/
				request = (request_t *) malloc (sizeof(request_t));
				if(!request){
					LOGE("no memory!\n");
					continue;
				}	

				request -> request = CARD_STATUS_STOPPED;
				/*从链表上取下来,单独操作*/
				list_del(pos);
				active_devices.cardnums--;
				active_devices.bitmap &= ~(1<<inst->dev.CardNo);
				thread = &inst->thread_info;
				pthread_mutex_lock(&thread->thread_mutex);
				list_add_tail( &request->node , &thread->req);
				pthread_mutex_unlock(&thread->thread_mutex);
				memset(&cbpara , 0 , sizeof(cbpara));
				cbpara.event = MBNET_CARD_PLUGOUT;
				cbpara.card = inst->dev.CardNo;
				cbpara.data = NULL;
				g_cb(&cbpara);
			}

		}

		pthread_mutex_unlock(&active_devices.mutex);
		/************************************************移除结束************************************************************/

		/*检查是否在active_devices list中已经存在？*/
		list_for_each_safe( pos, n, &scanned_devices){

			match = 0;

			inst = list_entry( pos, cardinst_t, node );

			pthread_mutex_lock(&active_devices.mutex);
			list_for_each(ipos , &active_devices.head){

				iinst = list_entry(ipos, cardinst_t , node);
				if(inst->dev.Pid == iinst->dev.Pid && \
						inst->dev.Vid == iinst->dev.Vid && \
						! strcmp(inst->dev.PhySlot,iinst->dev.PhySlot)){
					match = 1;
					break;
				}
			}
			pthread_mutex_unlock(&active_devices.mutex);

			/*删除scanned_devices list中的设备,因为此设备已经存在*/
			if(match ){

				list_del( pos);	
				thread = &inst->thread_info;
				pthread_mutex_destroy( &inst->dev_mutex);
				pthread_mutex_destroy( &thread->thread_mutex);
				pthread_mutex_destroy( &thread->fd_mutex);
				free(inst);
			}

		}

		/*检查是否在inactive_devices list中已经存在*/

		if( !list_empty( &scanned_devices) && !list_empty( &inactive_devices) ){

			list_for_each_safe( pos, n, &scanned_devices){

				match = 0;

				inst = list_entry( pos, cardinst_t, node );

				list_for_each(ipos , &inactive_devices){

					iinst = list_entry(ipos, cardinst_t , node);
					if(inst->dev.Pid == iinst->dev.Pid && \
							inst->dev.Vid == iinst->dev.Vid && \
							! strcmp(inst->dev.PhySlot,iinst->dev.PhySlot)){
						match = 1;
						break;
					}
				}

				/*删除scanned_devices list中的设备,因为此设备已经存在*/
				if(match ){

					list_del( pos);	
					thread = &inst->thread_info;
					pthread_mutex_destroy( &inst->dev_mutex);
					pthread_mutex_destroy( &thread->thread_mutex);
					pthread_mutex_destroy( &thread->fd_mutex);
					free(inst);
				}

			}

		}

		/*剩下来的为新增设备*/
		if( !list_empty( &scanned_devices)){

			list_for_each_safe( pos , n , &scanned_devices){
				inst = list_entry( pos , cardinst_t ,node );
				driver = get_driver(inst->dev.Vid, inst->dev.Pid);
				if( driver ){
					/*有驱动就加到active_devices list中去,以便后续处理*/
					inst->driver = driver;
					list_del( pos );		

					pthread_mutex_lock( &inst->dev_mutex );
					strcpy(inst->dev.sVendor, inst->driver->sVendor);
					strcpy(inst->dev.sProduct, inst->driver->sProduct);
					inst->dev.ttyUSBAt = inst->dev.ttyUSBBase + inst->driver->atpath;
					inst->dev.ttyUSBData = inst->dev.ttyUSBBase + inst->driver->datapath;
					inst->dev.Capability = inst->driver->Capability;
					pthread_mutex_unlock( &inst->dev_mutex );

					thread = &inst->thread_info;
					memset(ttyusb, 0, sizeof(ttyusb));
					sprintf(ttyusb,"/dev/ttyUSB%d", inst->dev.ttyUSBAt);
					fd = OpenTTY(ttyusb);
					if(fd < 0){
						LOGE("can't open ttyUSB%d",inst->dev.ttyUSBAt);
						pthread_mutex_destroy( &inst->dev_mutex);
						pthread_mutex_destroy( &thread->thread_mutex);
						pthread_mutex_destroy( &thread->fd_mutex);
						free(inst);
						continue;
					}
					SetOpt(fd, 115200, 8, 'N', 1);
					fcntl(fd, F_SETFL, O_NDELAY);

					pthread_mutex_lock( &thread->thread_mutex);
					thread->at_fd = fd;
					pthread_mutex_unlock(&thread->thread_mutex);

					/*此处CardNo与第一次初始化是不同的，
					  第一次时，bitmap中一个都没有，所以以
					  扫描的顺序就可以了，后续就要按照bitmap中的占位

					  来计算CardNo*/
					pthread_mutex_lock( &active_devices.mutex);
					CardNo = find_empty_location( active_devices.bitmap);
					pthread_mutex_unlock( &active_devices.mutex);

					if(CardNo < 0){

						LOGE("can't find empty cardno!");
						close(thread->at_fd);
						pthread_mutex_destroy( &inst->dev_mutex);
						pthread_mutex_destroy( &thread->thread_mutex);
						pthread_mutex_destroy( &thread->fd_mutex);
						free(inst);
						continue;
					}


					/*为模块启动服务吧*/
					if( create_service_thread( inst) ){

						LOGE("can't create service thread");
						close(thread->at_fd);
						pthread_mutex_destroy( &inst->dev_mutex);
						pthread_mutex_destroy( &thread->thread_mutex);
						pthread_mutex_destroy( &thread->fd_mutex);
						free(inst);
						continue;
					}

					pthread_mutex_lock( &active_devices.mutex);
					active_devices.cardnums ++;
					inst->dev.CardNo = CardNo;
					active_devices.bitmap |= (1 << inst->dev.CardNo);
					list_add_tail ( pos , &active_devices.head);
					pthread_mutex_unlock( &active_devices.mutex);

					/*回调通知有一个模块上来了*/
					memset(&cbpara , 0 , sizeof(cbpara));
					cbpara.event = MBNET_CARD_PLUGIN;
					cbpara.card = inst->dev.CardNo;
					cbpara.data = (void *)&(inst->dev);
					g_cb(&cbpara);

				}else{
					/*没有驱动，就把加到inactive_devices list中去*/
					list_del( pos );		

					list_add_tail ( pos , &inactive_devices);

				}
			}

		}

		/**规避由于电源异常设备瞬间热插拔，inst->dev.ttyUSBAt变化问题**/
		list_for_each_safe(pos, n, &active_devices.head){
			inst = list_entry( pos , cardinst_t , node);
			fp = fopen(USB2SERIALFILE, "r");
			if(!fp)
			{
				perror("Open" USB2SERIALFILE "Failed:");
				return MBNET_NO_DEVICE;
			}
			i = 0;
			memset(ttynum,8,0);
			while((nbytes = getline(&line, &len, fp)) != -1)
			{
				p = strstr(line,inst->dev.PhySlot);
				if (!p){
					free(line);
					line = NULL;
					continue;
				}
				ttynum[i++] = atoi(line);

			}
			if (inst->dev.ttyUSBAt != ttynum[inst->driver->atpath])
			{
				pthread_mutex_lock( &inst->dev_mutex );
				inst->dev.ttyUSBAt = ttynum[inst->driver->atpath];
				pthread_mutex_unlock( &inst->dev_mutex );

				thread = &inst->thread_info;
				close(thread->at_fd);
				memset(ttyusb, 0, sizeof(ttyusb));
				sprintf(ttyusb,"/dev/ttyUSB%d", inst->dev.ttyUSBAt);
				fd = OpenTTY(ttyusb);
				if(fd < 0){
					LOGE("can't open ttyUSB%d",inst->dev.ttyUSBAt);
					pthread_mutex_destroy( &inst->dev_mutex);
					pthread_mutex_destroy( &thread->thread_mutex);
					pthread_mutex_destroy( &thread->fd_mutex);
					free(inst);
					fclose(fp);
					continue;
				}
				SetOpt(fd, 115200, 8, 'N', 1);
				fcntl(fd, F_SETFL, O_NDELAY);

				pthread_mutex_lock( &thread->thread_mutex);
				thread->at_fd = fd;
				pthread_mutex_unlock(&thread->thread_mutex);
				LOGE("new dev.ttyUSBAt = %d open sucess\n ",inst->dev.ttyUSBAt);
			}
			if (inst->dev.ttyUSBData != ttynum[inst->driver->datapath])
			{
				pthread_mutex_lock( &inst->dev_mutex );
				inst->dev.ttyUSBData = ttynum[inst->driver->datapath];
				pthread_mutex_unlock( &inst->dev_mutex );
				LOGE("new dev.ttyUSBData = %d open sucess\n ",inst->dev.ttyUSBData);
			}
			fclose(fp);
		}
		sleep(3);
	}

}

static int create_hotplug_thread ( void )
{
	int ret = -1;
	pthread_t tid;
	ret = pthread_create(&tid, NULL, scan_thread , NULL);
	if(ret)
		return MBNET_ERROR;

	pthread_detach(tid);

	g_scan_tid = tid;

	return MBNET_OK;

}

static int debug_server_init( void )
{
	int ret = -1;
	pthread_t tid;
	ret = pthread_create(&tid, NULL, debug_server, NULL);
	if(ret)
		return MBNET_ERROR;
	pthread_detach(tid);
	g_debug_tid = tid;
	return MBNET_OK;
}


#if 0
static void SigchldHandler(int signal)
{
	pid_t pid;
	int i;

	for(i = 0; i < MAX_CARDS_SUPPORT; i++){
		if(g_cards[i].pid > 0){
			pid = waitpid(g_cards[i].pid, NULL,  WNOHANG);

		}
	}

}

#endif

static int devices_scan( void )
{
	FILE *fp = NULL;
	char *line = NULL;
	char *p, *p1;
	size_t len = 0;
	ssize_t nbytes;
	char buf[32];
	int cardnums = 0;
	cardinst_t *card = NULL;


	fp = fopen(USB2SERIALFILE, "r");
	if(!fp)
	{
		perror("Open" USB2SERIALFILE "Failed:");
		return MBNET_NO_DEVICE;
	}

	memset( buf , 0 , sizeof(buf));

	while((nbytes = getline(&line, &len, fp)) != -1)
	{
		p = strstr(line, "modem");

		if ( !p ){

			free(line);
			line = NULL;
			continue;
		}

		p = strstr(line, "path:");

		if(strcmp(buf, p+5)) 
		{
			/*发列一个新的模块*/
			card = malloc(sizeof(cardinst_t));
			if(! card){
				LOGE("no memory!\n");
				return MBNET_ERROR;
			}
			memset( card, 0, sizeof(cardinst_t));
			cardnums++;
			card->dev.CardNo = cardnums - 1;
			card->dev.ttyUSBBase = atoi(line);
			p1 = strstr(line, "vendor:");
			card->dev.Vid = strtol(p1+7, NULL, 16);
			p1 = strstr(line, "product:");

			card->dev.Pid = strtol(p1+8, NULL, 16);
			memcpy(card->dev.PhySlot, p+5, strlen(p+5));

			pthread_mutex_init(&card->dev_mutex , NULL);
			pthread_mutex_init(&card->thread_info.thread_mutex , NULL);
			pthread_mutex_init(&card->thread_info.fd_mutex , NULL);
			list_head_init( &card->thread_info.req);
			card->thread_info.at_fd = -1;
			card->thread_info.dial_pid = -1;
			card->thread_info.server_tid = -1;
			/*加到链表中去*/	
			list_add_tail( &card->node, &scanned_devices);
		}
		memcpy(buf, p+5, strlen(p+5));
	}

	if(line)
		free(line);
	fclose(fp);

	return cardnums;
}

static int instance_init( void )
{

	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	cardinst_t *inst = NULL;
	const driver_t *driver = NULL;
	thread_t *thread = NULL;
	char ttyusb[32];
	int fd = -1;


	callback_t	cbpara;

	memset(&active_devices, 0, sizeof(active_devices));
	pthread_mutex_init(&active_devices.mutex, NULL);
	list_head_init(&active_devices.head);

	/*第一步：扫描*/
	if( devices_scan() <= 0){

		return MBNET_NO_DEVICE;
	}

	/*第二步，是否有驱动?,有驱动就启动服务、并且移入active_devices链中*/
	list_for_each_safe(pos, n,  &scanned_devices){

		inst = list_entry(pos, cardinst_t, node); 
		driver = get_driver(inst->dev.Vid, inst->dev.Pid);
		if( driver ){
			/*有驱动就加到active_devices list中去,以便后续处理*/
			inst->driver = driver;
			list_del( pos );		

			pthread_mutex_lock( &inst->dev_mutex );
			strcpy(inst->dev.sVendor, inst->driver->sVendor);
			strcpy(inst->dev.sProduct, inst->driver->sProduct);
			inst->dev.ttyUSBAt = inst->dev.ttyUSBBase + inst->driver->atpath;
			inst->dev.ttyUSBData = inst->dev.ttyUSBBase + inst->driver->datapath;
			inst->dev.Capability = inst->driver->Capability;
			pthread_mutex_unlock( &inst->dev_mutex );

			thread = &inst->thread_info;
			memset(ttyusb, 0, sizeof(ttyusb));
			sprintf(ttyusb,"/dev/ttyUSB%d", inst->dev.ttyUSBAt);
			fd = OpenTTY(ttyusb);
			if(fd < 0){
				LOGE("can't open ttyUSB%d",inst->dev.ttyUSBAt);
				pthread_mutex_destroy( &inst->dev_mutex);
				pthread_mutex_destroy( &thread->thread_mutex);
				pthread_mutex_destroy( &thread->fd_mutex);
				free(inst);
				continue;
			}
			SetOpt(fd, 115200, 8, 'N', 1);
			fcntl(fd, F_SETFL, O_NDELAY);

			pthread_mutex_lock( &thread->thread_mutex);
			thread->at_fd = fd;
			pthread_mutex_unlock( &thread->thread_mutex);

			/*为模块启动服务吧*/
			if( create_service_thread( inst) ){

				LOGE("can't create service thread");
				close(thread->at_fd);
				pthread_mutex_destroy( &inst->dev_mutex);
				pthread_mutex_destroy( &thread->thread_mutex);
				pthread_mutex_destroy( &thread->fd_mutex);
				free(inst);
				continue;
			}

			/*第一次扫描，直接用CardNo为bitmap*/
			pthread_mutex_lock( &active_devices.mutex);
			active_devices.cardnums ++;
			active_devices.bitmap |= (1 << inst->dev.CardNo);
			list_add_tail ( pos , &active_devices.head);
			pthread_mutex_unlock( &active_devices.mutex);

			/*回调通知有一个模块上来了*/
			memset(&cbpara , 0 , sizeof(cbpara));
			cbpara.event = MBNET_CARD_PLUGIN;
			cbpara.card = inst->dev.CardNo;
			cbpara.data = (void *)&(inst->dev);
			g_cb(&cbpara);

		}else{
			/*没有驱动，就把加到inactive_devices list中去*/
			list_del( pos );		

			list_add_tail ( pos , &inactive_devices);

		}

	}

	return active_devices.cardnums;
}

int MbnetInit( CardCB cb)
{
	static int ginit = 0;

	int ret = -1;
#if 0
	struct sigaction act; 

#endif
	if( ginit > 0)
		return MBNET_ERROR;

	if( !cb )
		return MBNET_INVALID_PA;

	g_cb = cb;
#if 0
	/*注册信号处理函数*/
	memset(&act, 0, sizeof(act));
	act.sa_handler = SigchldHandler;
	sigaction(SIGCHLD, &act, NULL);
#endif

	/*把所有支持的模块都加入系统、并对
	  执行相关初始化*/
	drivers_init();

	/*扫描目前系统上存在的模块,并建立实例*/
	ret = instance_init();

	if(create_hotplug_thread()){
		LOGE("hotplug service create failed!\n");
		return MBNET_ERROR;
	}

	/*close telnet port, will be open in other ways in future versions*/
	debug_server_init();

	return ret;
}


int MbnetStart(int CardNo, dial_t *cfg)
{
	cardinst_t *inst = NULL;
	struct list_head *pos = NULL;
	thread_t *thread_info = NULL;
	int thread_flag = 0;
	request_t *request = NULL;
	int try_cnt = 5;

	LOGD("call MbnetStart card %d\n", CardNo);
	if( CardNo < 0 || CardNo > 32){
		LOGE("invalid param!\n");
		return MBNET_INVALID_PA;
	}

	pthread_mutex_lock(&active_devices.mutex);
	list_for_each(pos, &active_devices.head){
		inst = list_entry(pos, cardinst_t , node );
		/*放置在此锁中操作非常关键,与扫描中的移除互斥，从而能够达到记算效果
		  这个确保一定是发生在移除之前已经被引用,具体参看设计文档*/
		get_device( inst);
		if( CardNo == inst->dev.CardNo)
			break;
		put_device(inst);
		inst = NULL;
	}

	pthread_mutex_unlock(&active_devices.mutex);

	if( !inst ){
		LOGE("Can't find the CardNo inst when exec mbnet start\n");
		return MBNET_ERROR;
	}

	thread_info = &inst->thread_info;
	pthread_mutex_lock( &thread_info->thread_mutex);
	thread_flag = thread_info->flag;
	pthread_mutex_unlock( &thread_info->thread_mutex);
	if(thread_flag < CARD_STATUS_VENDOR){
		put_device(inst);
		LOGE("can't start dial because of invalid simcard!\n");
		return MBNET_ERROR;
	}

	/*设置APN*/
	if(cfg){
		memcpy( &inst->cfg , cfg , sizeof(dial_t));
	}else{
		/*必须拿到万能APN,不然无法拨号，所以必须尽力尝试*/
		for( try_cnt = 5; try_cnt > 0; try_cnt--){

			if( !get_general_apn( inst) ){
				break;
			}
			sleep(1);
		}
		if( try_cnt <= 0){

			put_device(inst);
			LOGE("get general apn error!\n");
			return MBNET_ERROR;
		}
	}


	request = (request_t *)malloc(sizeof(request_t));
	if(!request){
		put_device(inst);
		LOGE("no memory\n");
		return MBNET_ERROR;
	}

	request->request = CARD_STATUS_UPPING;
	pthread_mutex_lock( &thread_info->thread_mutex);
	list_add_tail(&request->node , &thread_info->req);
	pthread_mutex_unlock( &thread_info->thread_mutex);

	put_device(inst);

	LOGD("call MbnetStart card %d return OK\n", CardNo);
	return MBNET_OK;
}

int MbnetStop(int CardNo)
{
	cardinst_t *inst = NULL;
	struct list_head *pos = NULL;
	request_t *request = NULL;
	thread_t *thread = NULL;

	LOGD("call MbnetStop card %d\n", CardNo);
	if( CardNo < 0 || CardNo > 32 ){
		LOGE("invalid param!\n");
		return MBNET_INVALID_PA;
	}
	pthread_mutex_lock(&active_devices.mutex);
	list_for_each(pos, &active_devices.head){

		inst = list_entry(pos, cardinst_t , node );
		/*放置在此锁中操作非常关键,与扫描中的移除互斥，从而能够达到记算效果

		  这个确保一定是发生在移除之前已经被引用,具体参看设计文档*/
		get_device(inst);
		if( CardNo == inst->dev.CardNo)
			break;
		inst = NULL;
		put_device(inst);
	}

	pthread_mutex_unlock(&active_devices.mutex);

	if( !inst ){

		LOGE("Can't find the card %d inst when exec mbnet stop\n",CardNo);
		return MBNET_ERROR;
	}

	request = (request_t *)malloc(sizeof(request_t));
	if(!request){
		put_device(inst);
		LOGE("no memory\n");
		return MBNET_ERROR;
	}

	thread = &inst->thread_info;
	request->request = CARD_STATUS_DOWNING;
	pthread_mutex_lock( &thread->thread_mutex);
	list_add_tail(&request->node , &thread->req);
	pthread_mutex_unlock( &thread->thread_mutex);

	put_device(inst);

	LOGD("call MbnetStop card %d return OK\n", CardNo);
	return MBNET_OK;
}

int MbnetNetScanMode(int CardNo, dial_t *cfg)
{
	cardinst_t *inst = NULL;
	struct list_head *pos = NULL;
	thread_t *thread_info = NULL;

	if( CardNo < 0 || CardNo > 32){
		LOGE("invalid param!\n");
		return MBNET_INVALID_PA;
	}

	pthread_mutex_lock(&active_devices.mutex);
	list_for_each(pos, &active_devices.head){

		inst = list_entry(pos, cardinst_t , node );
		/*放置在此锁中操作非常关键,与扫描中的移除互斥，从而能够达到记算效果
		  这个确保一定是发生在移除之前已经被引用,具体参看设计文档*/
		get_device( inst);
		if( CardNo == inst->dev.CardNo)
			break;
		put_device(inst);
		inst = NULL;
	}

	pthread_mutex_unlock(&active_devices.mutex);

	if( !inst ){
		LOGE("Can't find the CardNo inst when call mbnet net scanmode\n");
		return MBNET_ERROR;
	}

	thread_info = &inst->thread_info;
	/*设置netscan*/
	if(cfg){
		if(inst->driver->netscanmode)
			inst->driver->netscanmode( thread_info, &inst->dev, cfg );
	}

	put_device(inst);

	return MBNET_OK;
}

int MbnetReset(int CardNo)
{
	cardinst_t *inst = NULL;
	struct list_head *pos = NULL;
	thread_t *thread_info = NULL;

	if( CardNo < 0 || CardNo > 32){
		LOGE("invalid param!\n");
		return MBNET_INVALID_PA;
	}

	pthread_mutex_lock(&active_devices.mutex);
	list_for_each(pos, &active_devices.head){

		inst = list_entry(pos, cardinst_t , node );
		/*放置在此锁中操作非常关键,与扫描中的移除互斥，从而能够达到记算效果
		  这个确保一定是发生在移除之前已经被引用,具体参看设计文档*/
		get_device( inst);
		if( CardNo == inst->dev.CardNo)
			break;
		put_device(inst);
		inst = NULL;
	}

	pthread_mutex_unlock(&active_devices.mutex);

	if( !inst ){
		LOGE("Can't find the CardNo inst when call mbnet net scanmode\n");
		return MBNET_ERROR;
	}

	thread_info = &inst->thread_info;
	/*设置netscan*/
	if(inst->driver->reset)
		inst->driver->reset( thread_info, &inst->dev );

	put_device(inst);

	return MBNET_OK;
}



int MbnetGetCardInfo(int CardNo, card_t *card)
{
	cardinst_t *inst = NULL;
	struct list_head *pos = NULL;
	card_t *dev = NULL;
	const driver_t *driver = NULL;
	thread_t *thread = NULL;
	card_t tmp_dev;
	unsigned int thread_flag;
	struct sysinfo sys_info;
	long on_line_time = 0;

	if( CardNo < 0 || CardNo > 32 || !card){
		LOGE("invalid param!\n");
		return MBNET_INVALID_PA;
	}

	pthread_mutex_lock(&active_devices.mutex);
	list_for_each(pos, &active_devices.head){

		inst = list_entry(pos, cardinst_t , node );
		/*放置在此锁中操作非常关键,与扫描中的移除互斥，从而能够达到记算效果

		  这个确保一定是发生在移除之前已经被引用,具体参看设计文档*/
		get_device( inst);
		if( CardNo == inst->dev.CardNo)
			break;
		put_device(inst);
		inst = NULL;
	}

	pthread_mutex_unlock(&active_devices.mutex);

	if( !inst ){

		LOGE("Can't find the CardNo inst when exec GetCardInfo\n");
		return MBNET_ERROR;
	}

	dev = &inst->dev;
	driver = inst->driver;
	thread = &inst->thread_info;
	pthread_mutex_lock(&thread->thread_mutex);
	thread_flag = thread->flag;
	pthread_mutex_unlock(&thread->thread_mutex);

	if(thread_flag == CARD_STATUS_DIALED){
		memset(&sys_info, 0, sizeof(sys_info));
		memset(&tmp_dev, 0, sizeof(tmp_dev));
		sysinfo(&sys_info);

		pthread_mutex_lock(&thread->thread_mutex);
		on_line_time = thread->onLine;
		pthread_mutex_unlock(&thread->thread_mutex);

		if(!get_iface_info( dev->iface, driver->DialType, &tmp_dev)){

			pthread_mutex_lock(&inst->dev_mutex);
			dev->iOnLineTime =  sys_info.uptime - on_line_time ;
			dev->iInDataCount = tmp_dev.iInDataCount ;
			dev->iOutDataCount = tmp_dev.iOutDataCount ;
			dev->local_ipaddr = tmp_dev.local_ipaddr ;
			dev->peer_ipaddr = tmp_dev.peer_ipaddr ;
			dev->gateway = tmp_dev.gateway ;
			dev->netmask = tmp_dev.netmask ;
			dev->dns1_addr = tmp_dev.dns1_addr ;
			dev->dns2_addr = tmp_dev.dns2_addr ;
			memcpy( card , dev ,sizeof(card_t));
			pthread_mutex_unlock(&inst->dev_mutex);
		}else{
			/*use old value*/

			pthread_mutex_lock(&inst->dev_mutex);
			memcpy( card , dev ,sizeof(card_t));
			pthread_mutex_unlock(&inst->dev_mutex);
		}

	}else{

		pthread_mutex_lock(&inst->dev_mutex);
		dev->iOnLineTime = 0;
		dev->iInDataCount = 0;
		dev->iOutDataCount = 0;
		dev->local_ipaddr = 0;
		dev->peer_ipaddr = 0;
		dev->gateway = 0;
		dev->netmask = 0;
		dev->dns1_addr = 0;
		dev->dns2_addr = 0;
		memcpy( card , dev ,sizeof(card_t));
		pthread_mutex_unlock(&inst->dev_mutex);
	}


	put_device( inst);
	return MBNET_OK;
}


char *MbnetGetVersion( void )
{
	//sprintf(version, "mbnet %s %s commit:%x",__DATE__, __TIME__ , MBNET_MOD_SUBVERSION );
	sprintf(version, "mbnet v%x.%x.%x %s %s commit:%x",MAIN_VERSION,MAJOR_VERSION,MINOR_VERSION,__DATE__, __TIME__ , MBNET_MOD_SUBVERSION );
	return version;
}

void MbnetDebug(int enable)
{

	/*开到最大*/
	if(enable){
		SetConsoleLevel(CON_LOG_LVL_DEBUG );
	}
	else
		SetConsoleLevel(CON_LOG_LVL_NONE );
}
