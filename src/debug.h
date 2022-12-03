/*
	2016/3/14 by zhaoxin
*/
#ifndef __MBNET_DEBUG_H
#define __MBNET_DEBUG_H

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define CON_LOG_LVL_NONE     0x00
#define CON_LOG_LVL_ERR      0x01
#define CON_LOG_LVL_WARNING  0x02
#define CON_LOG_LVL_INFO     0x03
#define CON_LOG_LVL_DEBUG    0x04

#define LOGMODE_SYSLOG  (1 << 16)
#define LOGMODE_CONSOLE (2 << 16)
#define LOG_LEVEL (0xf)

#define RESPONSE_MAX_LEN      520

#define MBNET_IPC_KEY_S (0x20170726)
#define MBNET_IPC_KEY_C (0x20170727)

struct msg_buf {
	long mtype; //client pid
	struct{
		unsigned int cmd;
		char data[512];
	}msg;
};

/*debug CMDs*/
enum{
	MBNET_DEBUG_CMD_HELP = 0,
	MBNET_DEBUG_CMD_ENABLE,
	MBNET_DEBUG_CMD_DISABLE,
	MBNET_DEBUG_CMD_DEVICES,
	MBNET_DEBUG_CMD_DRIVERS,
	MBNET_DEBUG_CMD_OPEN,
	MBNET_DEBUG_CMD_CLOSE,
	MBNET_DEBUG_CMD_STATUS,
	MBNET_DEBUG_CMD_AT,
	MBNET_DEBUG_CMD_EXIT,
};

extern unsigned int mbnet_logmode;

#define LOG(lvl, fmt, ...) \
	do { \
		if (mbnet_logmode & LOGMODE_SYSLOG) \
			syslog(LOG_##lvl | LOG_LOCAL7, "[%s, %d]=> " fmt "\n", \
			       __FUNCTION__, __LINE__, ##__VA_ARGS__); \
		if ((mbnet_logmode & LOGMODE_CONSOLE) && \
			((mbnet_logmode & LOG_LEVEL) >= CON_LOG_LVL_##lvl)) \
			printf("[%s, %d]=> " fmt "\n", \
			       __FUNCTION__, __LINE__, ##__VA_ARGS__); \
	} while (0)

#define LOGE(fmt, ...) \
	LOG(ERR, fmt, ##__VA_ARGS__)

#define LOGW(fmt, ...) \
	LOG(WARNING, fmt, ##__VA_ARGS__)

#define LOGI(fmt, ...) \
	LOG(INFO, fmt, ##__VA_ARGS__)

#define LOGD(fmt, ...) \
	LOG(DEBUG, fmt, ##__VA_ARGS__)

static inline unsigned int convert_to_cmd(const char *data)
{
	if(strncmp(data, "help", 4) == 0 && strlen(data) >= 4)
		return MBNET_DEBUG_CMD_HELP;
	if(strncmp(data, "enable", 6) == 0 && strlen(data) >= 6)
		return MBNET_DEBUG_CMD_ENABLE;
	if(strncmp(data, "disable", 7) == 0 && strlen(data) >= 7)
		return MBNET_DEBUG_CMD_DISABLE;
	if(strncmp(data, "devices", 7) == 0 && strlen(data) >= 7)
		return MBNET_DEBUG_CMD_DEVICES;
	if(strncmp(data, "drivers", 7) == 0 && strlen(data) >= 7)
		return MBNET_DEBUG_CMD_DRIVERS;
	if(strncmp(data, "open", 4) == 0 && strlen(data) >= 4)
		return MBNET_DEBUG_CMD_OPEN;
	if(strncmp(data, "close", 5) == 0 && strlen(data) >= 5)
		return MBNET_DEBUG_CMD_CLOSE;
	if(strncmp(data, "status", 6) == 0 && strlen(data) >= 6)
		return MBNET_DEBUG_CMD_STATUS;
	if(strncmp(data, "at", 2) == 0 && strlen(data) >= 2)
		return MBNET_DEBUG_CMD_AT;
	if(strncmp(data, "exit", 4) == 0 && strlen(data) >= 4)
		return MBNET_DEBUG_CMD_EXIT;

	return MBNET_DEBUG_CMD_HELP;
}


/*
   Send one message to specific IPC channel(msgid). It will be recived by
   client process(mtype stands for client pid).
 */
static inline int mbnet_ipc_send(int msgid, long mtype, const char *p)
{
	struct msg_buf msgbuff;
	int i = 0, ret = 0;

	memset(&msgbuff, 0, sizeof(struct msg_buf));
	msgbuff.mtype = mtype;
	sprintf(msgbuff.msg.data, "%s", p);
	msgbuff.msg.cmd = convert_to_cmd(p);

	for(i = 0; i < 5; i++){
		ret = msgsnd(msgid, &msgbuff, sizeof(msgbuff.msg),IPC_NOWAIT);
		if(ret == -1){
			usleep(10*1000);
			continue;
		}else{
			break;
		}
	}
	if( i >= 5){
		printf("IPC send pid%ld failed!\n",mtype);
		return -1;
	}
	return 0;
}
void *debug_server( void *arg);
void SetConsoleLevel(int level);

#endif
