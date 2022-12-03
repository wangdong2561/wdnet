#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "list.h"
#include "mbnet.h"
#include "debug.h"
#include "tty.h"

static driver_t huawei_mu709s_driver;

static void huawei_mu709s_init( const thread_t * thread, const card_t *dev)
{
	SendCmd(thread, "AT");
	sleep(1);
	SendCmd(thread, "ATE0");
	SendCmd(thread, "AT^CURC=0");
	SendCmd(thread, "AT^STSF=0");
	SendCmd(thread, "ATS0=0");
	/*Set MSG text mode*/
	SendCmd(thread, "AT+CMGF=1");
	/*Select MSG storage*/
	SendCmd(thread, "AT+CPMS=\"ME\",\"ME\",\"ME\"");
	/*Set MSG mode*/
	SendCmd(thread, "AT+CNMI=1,1,0,1,0");
	/*Set CallIn mode*/
	SendCmd(thread, "AT+CLIP=1");
	usleep(200000);
}

static void huawei_mu709s_sim( const thread_t *thread , const card_t *dev)
{

	SendCmd(thread, "AT+CPIN?");
	usleep(200000);
}

static void huawei_mu709s_imsi( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CIMI");
	usleep(200000);
}

static void huawei_mu709s_signal ( const thread_t *thread, const card_t *dev )
{
	SendCmd(thread ,"AT+CSQ");
	usleep(200000);
}
static void huawei_mu709s_netmode( const thread_t *thread, const card_t *dev )
{
	SendCmd(thread, "AT+COPS?");
	usleep(200000);
}


static void huawei_mu709s_fix (const thread_t *thread, const card_t *dev , const dial_t *cfg)
{
	char cmd[256];

	sprintf(cmd, "AT^NDISDUP=1,1,\"%s\"", cfg->sAPN);
	SendCmd(thread, cmd);

	sleep(4);
}

static int CallIn(char *buf, char *Phone)
{
	// \r\n+CLIP:02154072200,129,,,,0
	//u6300 +CLIP: "15800555799",129,,,,0
	int i, j, flags = 0;
	char *p1 = NULL;
	char *p2 = NULL;

	p1 = strstr(buf,"\"");
	if(NULL == p1)
		return MBNET_ERROR;

	p2 = strstr(p1 + 1,"\"");
	if(NULL == p2)
		return MBNET_ERROR;

	if ((p2 - p1) == 1)
	{
		printf("SIM Card can't support CLI!\n");
		return MBNET_ERROR;
	}

	for (i = 0, j = 0; buf[i] != ','; i++){
		if((buf[i] == ':') && (flags == 0)){
			flags = 1;
			continue;
		}
		if(flags == 1){
			if(buf[i] < 0x30 || buf[i] > 0x39)
				continue;
			Phone[j++] = buf[i];
			if(j >= 15)
				break;
		}
	}
	Phone[j] = '\0';
	return MBNET_OK;
}

static int huawei_mu709s_process( const thread_t *thread, const card_t *dev,  msg_t *msg)
{
	char buf[256];	
	char *pos = NULL;
	int nbytes = 0;
	int ret = -1;
	int flag = 0;
	int try_cnt = 5;
	char imsi[MBNET_MAX_IMSI_LEN];
	/*读取信息进行处理，读不到信息后，退出*/
	while(1){

		try_cnt--;

		if( try_cnt <= 0)
			break;


		memset( buf, 0, sizeof(buf));
		nbytes = sizeof(buf) - 1;
		pos = buf;
		while(nbytes--){

			ret = read(thread->at_fd, pos, 1);
			if(ret == 0){
				/*无数据了*/
				return 0;
			}
			pos++;
			if(pos[-1] == '\n' || pos[-1] == '\r') {
				if(3 == flag){//skip \r\n at the end of "AT+CIMI"
					flag = 0;
					break;
				}
					flag++;
					continue;
			}
		}
		*pos = '\0';

		/*simed?*/
		if( strstr(buf, "CPIN:")){
			if(strstr(buf, "READY"))
				msg->simcard =1;	
				msg->mask |= SIMCARD_MASK;
		}
		/*imsi?*/
		if( (pos = strstr(buf, "460")) != NULL){
			int i = 0;
			int j = 0;
			while( pos[i]){
				if( pos[i] >= '0' && pos[i] <= '9'){
					imsi[j]= pos[i];
					j++;
					if(j >=15)
						break;
				}
				i++;
			}
			imsi[j] ='\0';

			msg->mask |= IMSI_MASK;
			strcpy( msg->imsi, imsi);
		}
		/*signal?*/
		if( (pos = strstr(buf, "+CSQ:") ) != NULL){
			msg->mask |= SIGNAL_MASK;	
			msg->signal = atoi(pos + 5);
			/*转换统一值*/
			if(msg->signal >100)
				msg->signal -=100;
			else if (msg->signal <100)
				msg->signal *= 3.3;

		}

		/*netmode*/
		if( (pos = strstr(buf, "+COPS:")) != NULL){
			if((pos = strstr(buf, "CHN-UNICOM")) != NULL){
				if( *(pos + 12) > '0' && *(pos + 12) < '5'){
					switch( atoi(pos +12)){
						case 0:
							msg->netmode = MBNET_2G_MODE_GSM;
							break;
						case 2:
						case 3:
						case 4:
						case 5:
						case 6:
							msg->netmode = MBNET_3G_MODE_WCDMA;
							break;
						default:
							msg->netmode = MBNET_MODE_UNKNOWN;
							break;
					}
				}

				msg->mask |= NETMODE_MASK;

			}else{
				msg->netmode = MBNET_MODE_UNKNOWN;
				msg->mask |= NETMODE_MASK;
			}
		}


	}

	return 0;
}

static void huawei_mu709s_reset( const thread_t *thread , const card_t *dev)
{
	LOGD( "not support now!");
}

static void huawei_mu709s_stop( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT^NDISUP=1,0");
	usleep(200000);
}

static int huawei_mu709s_get_one_msg( const thread_t *thread , char *msg , unsigned int len)
{
	return read( thread->at_fd, msg, len);
}

static void huawei_mu709s_send_cmd( const thread_t *thread , char *cmd)
{
	SendCmd(thread , cmd);
}

driver_t * huawei_mu709s_driver_init( void )
{
	static int huawei_mu709s_inited = 0;

	if( huawei_mu709s_inited > 0)
		return &huawei_mu709s_driver;
	huawei_mu709s_inited++;
	huawei_mu709s_driver.Pid = 0x1c25;
	huawei_mu709s_driver.Vid = 0x12d1;
	strcpy(huawei_mu709s_driver.sProduct, "mu709s");
	strcpy(huawei_mu709s_driver.sVendor, "huawei");
	huawei_mu709s_driver.datapath = 0;
	huawei_mu709s_driver.atpath = 2;
	huawei_mu709s_driver.DialType = NDIS_DIAL_TYPE;
	huawei_mu709s_driver.Capability = MBNET_3G_MODE_WCDMA;
	huawei_mu709s_driver.init = huawei_mu709s_init;
	huawei_mu709s_driver.sim = huawei_mu709s_sim;
	huawei_mu709s_driver.imsi = huawei_mu709s_imsi;
	huawei_mu709s_driver.signal = huawei_mu709s_signal;
	huawei_mu709s_driver.netmode = huawei_mu709s_netmode;
	huawei_mu709s_driver.fix = huawei_mu709s_fix;
	huawei_mu709s_driver.process = huawei_mu709s_process;
	huawei_mu709s_driver.stop = huawei_mu709s_stop;
	huawei_mu709s_driver.reset = huawei_mu709s_reset;
	huawei_mu709s_driver.send_cmd = huawei_mu709s_send_cmd;
	huawei_mu709s_driver.get_one_msg = huawei_mu709s_get_one_msg;
	huawei_mu709s_driver.data =NULL;
	return &huawei_mu709s_driver;
}
