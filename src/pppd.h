#ifndef __PPPD_H
#define __PPPD_H

typedef struct {
	char *pppd_connect_script;
	char *pppd_disconnect_script;
	char *pppd_chat_script;
}script_t;

/*pppd拨号模块应该使用MAX_NETMODE定义一个pppd_script_t类型的拨号脚本数组
数组中每个元素按模块支持的网络类型进行设置*/
enum{
	/*代表了中国电信的拨类型*/
	TELECOM_NETMODE = 0,	
	/*代表了中国移动的拨号类型*/
	MOBILE_NETMODE,
	/*代表了中国联通的拨号类型*/
	UNICOM_NETMODE,
	MAX_NETMODE,
};

/*模块中用以下结构去定义模块的拨号脚本*/
typedef struct {
	script_t script[MAX_NETMODE];
}pppd_script_t;

int pppd_get_dns(const char* iface, unsigned int *dns1, unsigned int *dns2);
int pppd_dial( cardinst_t *inst , dial_t *cfg );
int pppd_stop( cardinst_t *inst);
int pppd_start( cardinst_t *inst );
#endif
