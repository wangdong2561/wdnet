
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "list.h"
#include "mbnet.h"
#include "debug.h"
#include "tty.h"

static driver_t zte_me3760_driver;

static void zte_me3760_init( const thread_t * thread, const card_t *dev)
{
	SendCmd(thread, "AT");
	sleep(1);
	SendCmd(thread, "AT+CIMI");
	sleep(1);
	SendCmd(thread,"AT+CMEE=1");
	sleep(1);
	SendCmd(thread, "ATE0");
	sleep(1);
	SendCmd(thread, "AT+CGDCONT=1,\"IP\"");
	sleep(1);
	SendCmd(thread, "AT+CFUN?");
	sleep(1);
	SendCmd(thread, "AT+CFUN=1");
	sleep(1);
	SendCmd(thread, "AT+CLCK=\"SC\",2");
	sleep(1);
	SendCmd(thread, "AT+CPIN?");
	sleep(1);
	SendCmd(thread, "AT+CLIP=1");
	sleep(1);
	SendCmd(thread, "AT+CREG=1");
	sleep(1);
	SendCmd(thread, "AT+CGREG=1");
	sleep(1);
	SendCmd(thread, "AT+CNMI=2,1,2,2,0");
	sleep(1);
	SendCmd(thread, "AT+CPMS=\"SM\",\"SM\",\"SM\"");
	sleep(1);
}

static void zte_me3760_sim( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CPIN?");

}

static void zte_me3760_imsi( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CIMI");
}

static void zte_me3760_signal ( const thread_t *thread, const card_t *dev )
{
	SendCmd(thread ,"AT+CSQ");

}
static void zte_me3760_netmode( const thread_t *thread, const card_t *dev )
{
	SendCmd(thread, "AT^SYSINFO");
}


static void zte_me3760_fix (const thread_t *thread, const card_t *dev , const dial_t *cfg)
{
	SendCmd(thread, "AT+CEREG=1");
	sleep(1);
	SendCmd(thread, "AT+CGDCONT=1,\"IP\"");
	sleep(1);
	SendCmd(thread, "AT+CGACT=1,1");
	sleep(1);
	SendCmd(thread, "AT+ZGACT=1,1");
	sleep(1);

	/*usrname?passwd?callnum?*/

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

static int zte_me3760_process( const thread_t *thread, const card_t *dev,  msg_t *msg)
{
	char buf[256];
	char *pos = NULL;
	int nbytes = 0;
	int ret = -1;
	int flag = 0;
	int try_cnt = 50;
	char imsi[MBNET_MAX_IMSI_LEN];
	LOGD("zte_me3760_process");
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
		if((pos = strstr(buf, "CSQ:") ) != NULL){
			if(dev->CurrentNetMode == MBNET_2G_MODE_GSM){
				if(atoi(pos + 4) == 99)
					msg->signal = 0;
				else
					msg->signal = atoi(pos + 4)*3+6;
			}else if(dev->CurrentNetMode == MBNET_3G_MODE_TDSCDMA){
				if(atoi(pos + 4) == 199)
					msg->signal = 0;
				else
					msg->signal = atoi(pos + 4)-92;
			}else if(dev->CurrentNetMode == MBNET_4G_MODE_TDLTE){
				if(atoi(pos + 4) == 199)
					msg->signal = 0;
				else
					msg->signal = atoi(pos + 4)-92;
			}
			msg->mask |= SIGNAL_MASK;
		}

		/*netmod?*/
		if( (pos = strstr(buf ,"^SYSINFO:")) != NULL){
			pos=strstr(pos,",");
			pos=strstr(pos+1,",");
			pos=strstr(pos+1,",");
			pos=pos+1;

			switch(atoi(pos)){
				case 3:
					msg->netmode = MBNET_2G_MODE_GSM;
					break;
				case 5:
				case 15:
					msg->netmode = MBNET_3G_MODE_TDSCDMA;
					break;
				case 17:
					msg->netmode = MBNET_4G_MODE_TDLTE;
					break;
				default:
					msg->netmode = MBNET_MODE_UNKNOWN;
					break;
			}

			msg->mask |= NETMODE_MASK;
		}
		/*call in */
		if( (pos = strstr( buf , "+CLIP:")) != NULL){

			SendCmd(thread , "AT+CHUP");

			if( !CallIn( pos, msg->sms.phone)){
				msg->mask |= SMS_MASK;
			}

		}

		/*msg in*/
		if( (pos = strstr(buf , "+CMTI:")) != NULL){

			//TODO

		}

	}

	return 0;
}

static void zte_me3760_reset( const thread_t *thread , const card_t *dev)
{
	LOGD( "not support now!");
}

static void zte_me3760_stop( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+ZGACT=0,1");
	sleep(1);
	SendCmd(thread, "AT+ZGACT=0,1");
	sleep(1);
}

static int zte_me3760_get_one_msg( const thread_t *thread , char *msg , unsigned int len)
{
	return read( thread->at_fd, msg, len);
}

static void zte_me3760_send_cmd( const thread_t *thread , char *cmd)
{
	SendCmd(thread , cmd);
}

driver_t * zte_me3760_driver_init( void )
{
	static int zte_me3760_inited = 0;

	if( zte_me3760_inited > 0)
		return &zte_me3760_driver;
	zte_me3760_inited++;
	zte_me3760_driver.Vid = 0x19d2;
	zte_me3760_driver.Pid = 0x0199;
	strcpy(zte_me3760_driver.sProduct, "me3760");
	strcpy(zte_me3760_driver.sVendor, "ZTE");
	zte_me3760_driver.datapath = 1;
	zte_me3760_driver.atpath = 1;
	zte_me3760_driver.DialType = NDIS_DIAL_TYPE;
	zte_me3760_driver.Capability = MBNET_4G_MODE_TDLTE | MBNET_3G_MODE_TDSCDMA | MBNET_2G_MODE_GSM;
	zte_me3760_driver.init = zte_me3760_init;
	zte_me3760_driver.sim = zte_me3760_sim;
	zte_me3760_driver.imsi = zte_me3760_imsi;
	zte_me3760_driver.signal = zte_me3760_signal;
	zte_me3760_driver.netmode = zte_me3760_netmode;
	zte_me3760_driver.fix = zte_me3760_fix;
	zte_me3760_driver.process = zte_me3760_process;
	zte_me3760_driver.stop = zte_me3760_stop;
	zte_me3760_driver.reset = zte_me3760_reset;
	zte_me3760_driver.send_cmd = zte_me3760_send_cmd;
	zte_me3760_driver.get_one_msg = zte_me3760_get_one_msg;
	zte_me3760_driver.data =NULL;
	return &zte_me3760_driver;
}
