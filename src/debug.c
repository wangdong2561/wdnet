#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>
#include <ctype.h>

#include "debug.h"
#include "mbnet.h"
#include "tty.h"
#include "list.h"

extern struct{
	pthread_mutex_t mutex;
	unsigned int bitmap;
	unsigned int cardnums;
	struct list_head head;
}active_devices;

unsigned int mbnet_logmode = LOGMODE_SYSLOG;

static struct{
	int tid;
	int msgid_s; /* server send, client recive */
	int msgid_c; /* client send, server recive */
	int at_fd;
	int CardNo;
}at_args;

const static char *helper="usage:\r\n \
	help     -----show this msg\r\n \
	enable   -----enable the console debug\r\n \
	disable  -----disable the console debug\r\n \
	devices  -----list all scaned devices\r\n \
	drivers  -----list all support drivers\r\n \
	open     -----\"open :CardNo\", The CardNo is show in devices cmd\r\n \
	close    -----\"close :CardNo\", The CardNo is show in devices cmd\r\n \
	status   -----\"status :CardNo\", The CardNo is show in devices cmd\r\n \
	at***    -----send at cmd to module\r\n \
	exit     -----exit from debug\r\n" ;

extern int get_device( cardinst_t *inst);
extern int  put_device( cardinst_t *inst);

/* 将字符串转化为小写 */
static char *strlowr(char *str){
	char *orign=str;
	for (; *str!='\0'; str++)
		*str = tolower(*str);
	return orign;
}


/*
 */
static void *at_echo_back(void *args)
{
	char response[RESPONSE_MAX_LEN];
	char tmpbuf[512];
	char *pos = NULL;
	int nbytes = 0;
	fd_set rdfds;
	struct timeval tv;
	int ret = -1;

	long mtype = (int)(*(int*)args);

	tv.tv_sec = 0;
	tv.tv_usec = 100*1000;
	while(1){
		FD_ZERO(&rdfds);
		FD_SET(at_args.at_fd, &rdfds);

		memset( tmpbuf, 0, sizeof(tmpbuf));
		nbytes = sizeof(tmpbuf) - 1;
		pos = tmpbuf;
		while(nbytes--){
			ret = select(at_args.at_fd+1, &rdfds, NULL, NULL, &tv);
			if(ret <= 0){
				continue;
			}
			ret = read(at_args.at_fd, pos, 1);
			if(ret == 0){
				return 0;
			}
			if(ret < 0){
				continue;
			}
			pos++;
			if(pos[-1] == '\n') {
				*pos = '\r';
				pos++;
				break;
			}
		}
		*pos = '\0';

		if(strlen(tmpbuf) > 2){
			memset(response, 0, sizeof(response));
			sprintf(response, "=> %s",tmpbuf);
			mbnet_ipc_send(at_args.msgid_s, mtype, response);
		}
	}
}

static int get_msgid(void)
{
	/*try to creat server msg queue*/
	at_args.msgid_s = msgget((key_t)MBNET_IPC_KEY_S, IPC_CREAT | IPC_EXCL | 0666);

	if(at_args.msgid_s == -1 && errno == EEXIST){
		at_args.msgid_s = msgget((key_t)MBNET_IPC_KEY_S, IPC_EXCL|0666);
		LOGE("MSGIPC_S exist!\n");
		if (at_args.msgid_s == -1) {
			LOGE("MSGIPC_S error\n");
			return -1;
		}

		/* destory and recreater ipc */
		msgctl(at_args.msgid_s, IPC_RMID, NULL);

		at_args.msgid_s = msgget((key_t)MBNET_IPC_KEY_S, IPC_CREAT|IPC_EXCL|0666);
		if (at_args.msgid_s == -1) {
			LOGE("MSGIPC_S recreate err\n");
			return -1;
		}
	}
	LOGE("MSGIPC_S create success!\n");

	/*try to creat client msg queue*/
	at_args.msgid_c = msgget((key_t)MBNET_IPC_KEY_C, IPC_CREAT | IPC_EXCL | 0666);

	if(at_args.msgid_c == -1 && errno == EEXIST){
		at_args.msgid_c = msgget((key_t)MBNET_IPC_KEY_C, IPC_EXCL|0666);
		LOGE("MSGIPC_C exist!\n");
		if (at_args.msgid_c == -1) {
			LOGE("MSGIPC_C error\n");
			return -1;
		}

		/* destory and recreater ipc */
		msgctl(at_args.msgid_c, IPC_RMID, NULL);

		at_args.msgid_c = msgget((key_t)MBNET_IPC_KEY_C, IPC_CREAT|IPC_EXCL|0666);
		if (at_args.msgid_c == -1) {
			LOGE("MSGIPC_C recreate err\n");
			return -1;
		}
	}
	LOGE("MSGIPC_C create success!\n");

	return 0;
}
/*
   thread for debug, this func used for recv and phrase debug CMDs.
   use variables "curfd" and "at_args" to make sure debug in the same device at once.
   It means that if you want to debug device N, you should open N. If you want to
   debug another device, you should close N first.
 */
void *debug_server( void *arg)
{
	int curfd = 0;
	long mtype = 0;
	int ret = -1;
	char * pos=NULL;
	struct list_head *posl = NULL;
	pthread_t id_debug = 0;
	thread_t *thread_info = NULL;
	cardinst_t *inst = NULL;

	char response[RESPONSE_MAX_LEN];

	struct msg_buf msgbuff;

	if(get_msgid() < 0)
		return NULL;

	while(1){
		/* handle one msg from client in a while */
		memset(&msgbuff, 0, sizeof(struct msg_buf));
		ret = msgrcv(at_args.msgid_c, &msgbuff, sizeof(msgbuff.msg), 0, 0);
		if (ret == -1) {
			usleep(1000*1000);
			continue;
		}

		mtype = msgbuff.mtype;
		printf("DebugRecv: mtpye=%ld, cmd=%d, data=%s\n",msgbuff.mtype, msgbuff.msg.cmd, msgbuff.msg.data);

		/* start to phrase cmd, translate to lowercase letters */
		strlowr(msgbuff.msg.data);

		switch(msgbuff.msg.cmd){
			case MBNET_DEBUG_CMD_ENABLE:
				SetConsoleLevel(CON_LOG_LVL_DEBUG );
				mbnet_ipc_send(at_args.msgid_s, mtype, "Console Enable!\r\n");
				break;

			case MBNET_DEBUG_CMD_DISABLE:
				SetConsoleLevel(CON_LOG_LVL_DEBUG );
				mbnet_ipc_send(at_args.msgid_s, mtype, "Console Disable\r\n");
				break;

			case MBNET_DEBUG_CMD_DEVICES:
				pthread_mutex_lock(&active_devices.mutex);
				list_for_each(posl, &active_devices.head){
					inst = list_entry(posl, cardinst_t , node );
					if(inst != NULL){
						get_device( inst);

						memset(response, 0, sizeof(response));
						sprintf(response, "DeviceNo:%d	Product:%s\r\n", \
								inst->dev.CardNo, inst->dev.sProduct);
						mbnet_ipc_send(at_args.msgid_s, mtype, response);

						put_device(inst);
						inst = NULL;
					}
				}
				pthread_mutex_unlock(&active_devices.mutex);
				break;

			case MBNET_DEBUG_CMD_DRIVERS:
				pthread_mutex_lock(&active_devices.mutex);
				list_for_each(posl, &active_devices.head){
					inst = list_entry(posl, cardinst_t , node );
					if(inst != NULL){
						get_device( inst);

						memset(response, 0, sizeof(response));
						if(inst->driver == NULL){
							sprintf(response, "DeviceNo:%d -- Not found driver\r\n ", \
									inst->dev.CardNo);
						}else{
							sprintf(response, "DeviceNo:%d	\r\nVid:0x%x\r\nPid:0x%x\r\n", \
									inst->dev.CardNo, inst->driver->Vid,inst->driver->Pid);
						}
						mbnet_ipc_send(at_args.msgid_s, mtype, response);

						put_device(inst);
						inst = NULL;
					}
				}
				pthread_mutex_unlock(&active_devices.mutex);
				break;

			case MBNET_DEBUG_CMD_OPEN:
				if(curfd){
					memset(response, 0, sizeof(response));
					sprintf(response, "Card %d is openning,can't open another\r\n",at_args.CardNo);
					mbnet_ipc_send(at_args.msgid_s, mtype, response);
				}else{
					if ( (pos = strstr( msgbuff.msg.data , ":") ) != NULL){
						/*此处可查找到要打开的模块序号，找到序号之后，先将模块置于debug模式*/
						int card = atoi( pos+1 );
						request_t *request = NULL;

						pthread_mutex_lock(&active_devices.mutex);
						list_for_each(posl, &active_devices.head){
							inst = list_entry(posl, cardinst_t , node );
							if(inst != NULL){
								get_device( inst);
								if( card == inst->dev.CardNo){
									curfd = inst->thread_info.at_fd; //将文件fd设为模块ttyfd
									thread_info = &inst->thread_info;
									request = (request_t *)malloc(sizeof(request_t));
									if(!request){
										LOGE("malloc err\n");
										put_device(inst);
										inst = NULL;
										break;
									}
									request->request = CARD_STATUS_DEBUG;
									pthread_mutex_lock( &thread_info->thread_mutex);
									list_add_tail(&request->node , &thread_info->req);
									pthread_mutex_unlock( &thread_info->thread_mutex);
									while(1){
										pthread_mutex_lock( &thread_info->thread_mutex);
										if(thread_info->flag == CARD_STATUS_DEBUG){
											pthread_mutex_unlock( &thread_info->thread_mutex);
											break;
										}
										pthread_mutex_unlock( &thread_info->thread_mutex);
										sleep(1);
									}
								}
								put_device(inst);
								inst = NULL;
							}
						}
						pthread_mutex_unlock(&active_devices.mutex);

						memset(response, 0, sizeof(response));
						if(curfd){
							at_args.at_fd=curfd;
							at_args.CardNo=card;
							ret = pthread_create(&id_debug, NULL, at_echo_back, &mtype);
							if(ret){
								LOGD("pthread create err");
								curfd = 0;
								at_args.at_fd = 0;
								at_args.CardNo= 0xFF;
							}
							pthread_detach(id_debug);
							sprintf(response, "Open card %d success\r\n", card);
						}else{
							sprintf(response, "Can't find card %d\r\n", card);
						}
						mbnet_ipc_send(at_args.msgid_s, mtype, response);

					}else{
						mbnet_ipc_send(at_args.msgid_s, mtype, helper);
					}
				}

				break;
			case MBNET_DEBUG_CMD_CLOSE:
				if ( (pos = strstr( msgbuff.msg.data , ":")) != NULL){
					int card = atoi( pos+1 );
					void *status;

					if(card == at_args.CardNo && curfd){
						pthread_cancel(id_debug);
						pthread_join(id_debug,&status);

						curfd = 0;
						at_args.at_fd = 0;
						at_args.CardNo= 0xFF;

						request_t *request = NULL;

						pthread_mutex_lock(&active_devices.mutex);
						list_for_each(posl, &active_devices.head){
							inst = list_entry(posl, cardinst_t , node );
							if(inst != NULL){
								get_device( inst);
								if( card == inst->dev.CardNo){
									thread_info = &inst->thread_info;
									request = (request_t *)malloc(sizeof(request_t));
									if(!request){
										LOGE("malloc err\n");
										break;
									}
									request->request = CARD_STATUS_DEBUG_EXIT;

									pthread_mutex_lock( &thread_info->thread_mutex);
									list_add_tail(&request->node , &thread_info->req);
									pthread_mutex_unlock( &thread_info->thread_mutex);
									while(1){
										pthread_mutex_lock( &thread_info->thread_mutex);
										if(thread_info->flag != CARD_STATUS_DEBUG){
											pthread_mutex_unlock( &thread_info->thread_mutex);
											break;
										}
										pthread_mutex_unlock( &thread_info->thread_mutex);
										sleep(1);
									}
								}
								put_device(inst);
								inst = NULL;
							}
						}
						pthread_mutex_unlock(&active_devices.mutex);

						memset(response, 0, sizeof(response));
						sprintf(response, "Close card %d\r\n", card);
						mbnet_ipc_send(at_args.msgid_s, mtype, response);
					}else{
						memset(response, 0, sizeof(response));
						sprintf(response, "Card No err\r\n");
						mbnet_ipc_send(at_args.msgid_s, mtype, response);
					}
				}else{
					mbnet_ipc_send(at_args.msgid_s, mtype, helper);
				}
				break;

			case MBNET_DEBUG_CMD_STATUS:
				if ( (pos = strstr( msgbuff.msg.data , ":")) != NULL){
					int card = atoi( pos+1 );

					pthread_mutex_lock(&active_devices.mutex);
					list_for_each(posl, &active_devices.head){
						inst = list_entry(posl, cardinst_t , node );
						get_device( inst);
						if( card == inst->dev.CardNo){
							memset(response, 0, sizeof(response));
							sprintf(response, "DeviceNo:%d\r\n \
									Product:%s\r\n \
									IMEI:%s\r\n \
									NetVendor:%s\r\n \
									CurrentNetMode:0x%x\r\n \
									OnLineTime:%ds\r\n \
									local_ipaddr:%d.%d.%d.%d\r\n \
									InDataCount:%dk\r\n \
									OutDataCount:%dk\r\n", \
									inst->dev.CardNo, inst->dev.sProduct, inst->dev.IMEI, \
									inst->dev.NetVendor, inst->dev.CurrentNetMode, inst->dev.iOnLineTime, \
									(inst->dev.local_ipaddr&0x000000FF), (inst->dev.local_ipaddr&0x0000FFFF)>>8, \
									(inst->dev.local_ipaddr&0x00FFFFFF)>>16, inst->dev.local_ipaddr>>24, \
									inst->dev.iInDataCount, inst->dev.iOutDataCount);
							mbnet_ipc_send(at_args.msgid_s, mtype, response);
						}
						put_device(inst);
						inst = NULL;
					}
					pthread_mutex_unlock(&active_devices.mutex);

					memset(response, 0, sizeof(response));
					sprintf(response, "	\r\nNetVendor list:\r\n \
							MBNET_MODE_UNKNOWN		%x\r\n \
							MBNET_2G_MODE_GSM		%x\r\n \
							MBNET_2G_MODE_CDMA		%x\r\n \
							MBNET_3G_MODE_WCDMA		%x\r\n \
							MBNET_3G_MODE_CDMA2000		%x\r\n \
							MBNET_3G_MODE_TDSCDMA		%x\r\n \
							MBNET_4G_MODE_LTE		%x\r\n \
							MBNET_4G_MODE_TDLTE		%x\r\n \
							MBNET_4G_MODE_TELECOM_FDDLTE	%x\r\n \
							MBNET_4G_MODE_UNICOM_FDDLTE	%x\r\n", \
							MBNET_MODE_UNKNOWN, MBNET_2G_MODE_GSM, MBNET_2G_MODE_CDMA, \
							MBNET_3G_MODE_WCDMA, MBNET_3G_MODE_CDMA2000, MBNET_3G_MODE_TDSCDMA, \
							MBNET_4G_MODE_LTE, MBNET_4G_MODE_TDLTE, MBNET_4G_MODE_TELECOM_FDDLTE, \
							MBNET_4G_MODE_UNICOM_FDDLTE);
					mbnet_ipc_send(at_args.msgid_s, mtype, response);
				}else{
					mbnet_ipc_send(at_args.msgid_s, mtype, helper);
				}
				break;

			case MBNET_DEBUG_CMD_AT:
				/* send AT to module, but not reply in here.
				   reply by at_echo_back thread.*/
				if(curfd){
					SendCmd(thread_info,msgbuff.msg.data);
				}else{
					memset(response, 0, sizeof(response));
					sprintf(response, "Should open card first\r\n");
					mbnet_ipc_send(at_args.msgid_s, mtype, response);
				}
				break;

			case MBNET_DEBUG_CMD_EXIT:
				if(at_args.CardNo != 0xFF){
					void *status;
					pthread_cancel(id_debug);
					pthread_join(id_debug,&status);

					request_t *request = NULL;

					pthread_mutex_lock(&active_devices.mutex);
					list_for_each(posl, &active_devices.head){
						inst = list_entry(posl, cardinst_t , node );
						if(inst != NULL){
							get_device( inst);
							if( at_args.CardNo == inst->dev.CardNo){
								thread_info = &inst->thread_info;
								request = (request_t *)malloc(sizeof(request_t));
								if(!request){
									LOGE("malloc err\n");
									break;
								}
								request->request = CARD_STATUS_DEBUG_EXIT;

								pthread_mutex_lock( &thread_info->thread_mutex);
								list_add_tail(&request->node , &thread_info->req);
								pthread_mutex_unlock( &thread_info->thread_mutex);
								while(1){
									pthread_mutex_lock( &thread_info->thread_mutex);
									if(thread_info->flag != CARD_STATUS_DEBUG){
										pthread_mutex_unlock( &thread_info->thread_mutex);
										break;
									}
									pthread_mutex_unlock( &thread_info->thread_mutex);
									sleep(1);
								}
							}
							put_device(inst);
							inst = NULL;
						}
					}
					pthread_mutex_unlock(&active_devices.mutex);
				}

				curfd = 0;
				at_args.at_fd = 0;
				at_args.CardNo= 0xFF;
				break;
			default:
				mbnet_ipc_send(at_args.msgid_s, mtype, helper);
				break;
		}
	}
}








/*设置是否启开启console调试，默认情况是输到syslog中的，

  如果设置的话，可以同时在console与syslog中看到*/

void SetConsoleLevel(int level)
{
	if (level)
	{
		mbnet_logmode &= ~LOG_LEVEL;
		mbnet_logmode |= level & LOG_LEVEL;
		mbnet_logmode |= LOGMODE_CONSOLE;
	}
	else
	{
		mbnet_logmode &= ~LOG_LEVEL;
		mbnet_logmode &= ~LOGMODE_CONSOLE;
	}
}
