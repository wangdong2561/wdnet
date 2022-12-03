
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "list.h"
#include "mbnet.h"
#include "debug.h"
#include "tty.h"

static driver_t broadmobi_bm806c_driver;

static void broadmobi_bm806c_init( const thread_t * thread, const card_t *dev)
{
	//!!! EHRPD not support in telecom 3g with pppd !!!
	//SendCmd(thread, "AT+EHRPDENABLE=0");
	//psrat , find net mode
	SendCmd(thread, "AT+BMRAT");
	//set default setting
	SendCmd(thread, "AT&F");
	/*Set MSG text mode*/
	SendCmd(thread, "AT+CMGF=1");
	/*Select MSG storage*/
	SendCmd(thread, "AT+CPMS=\"ME\",\"ME\",\"ME\"");
	/*Set MSG mode*/
	SendCmd(thread, "AT+CNMI=1,1,0,1,0");
	/*Set CallIn mode*/
	SendCmd(thread, "AT+CLIP=1");
	/*open datalink statu*/
	SendCmd(thread, "AT+BMDATASTATUSEN=1");
}

static void broadmobi_bm806c_sim( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CPIN?");
	SendCmd(thread, "AT+QCPIN?");
	SendCmd(thread, "AT+GSN");
}

static void broadmobi_bm806c_imsi( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CIMI");
	SendCmd(thread, "AT+QCIMI");
}

static void broadmobi_bm806c_signal ( const thread_t *thread, const card_t *dev )
{
	if(dev->CurrentNetMode == MBNET_3G_MODE_CDMA2000)
		SendCmd(thread ,"AT+CCSQ");
	else
		SendCmd(thread ,"AT+CSQ");

}
static void broadmobi_bm806c_netmode( const thread_t *thread, const card_t *dev )
{
	SendCmd(thread, "AT+BMRAT");
}

static void broadmobi_bm806c_fix (const thread_t *thread, const card_t *dev , const dial_t *cfg)
{
	char cmd[256];

	memset(cmd, 0, sizeof(cmd));
	if(cfg->iAuthentication){
		sprintf(cmd,"AT$QCPDPP=1,%d,\"%s\",\"%s\"", \
				cfg->iAuthentication, cfg->sPassWord, cfg->sUserName);
	}else{
		sprintf(cmd,"AT$QCPDPP=1,0");
	}
	SendCmd(thread, cmd);

	memset(cmd, 0, sizeof(cmd));
	if(dev->CurrentNetMode == MBNET_3G_MODE_CDMA2000){
		sprintf(cmd, "AT+BM3GPP2CGDCONT=0,3,%s,%s,ctlte,2,3,0", cfg->sUserName, cfg->sPassWord);
		SendCmd(thread, cmd);
		sleep(1);
		sprintf(cmd, "AT+BM3GPP2AUTHINFO=\"%s\",\"%s\"", cfg->sUserName, cfg->sPassWord);
		SendCmd(thread, cmd);
		sleep(1);
		SendCmd(thread, "at$qcrmcall=1,1,1,1,1");
	}else{
		sprintf(cmd, "AT+CGDCONT=1,\"IP\",\"%s\"", cfg->sAPN);
		SendCmd(thread, cmd);
		sleep(1);
		SendCmd(thread, "at$qcrmcall=1,1,1,2,1");
	}

	sleep(4);

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

static int broadmobi_bm806c_process( const thread_t *thread, const card_t *dev,  msg_t *msg)
{
	char buf[256];
	char *pos = NULL;
	int nbytes = 0;
	int ret = -1;
	int flag = 0;
	int try_cnt = 5;
	char imsi[MBNET_MAX_IMSI_LEN];
	char imei[MBNET_MAX_IMEI_LEN];
	/*读取信息进行处理，读不到信息后，退出*/
	while(1){
		try_cnt--;
		if( try_cnt <= 0)
			break;

		/*读取一条AT指令回复*/
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
			/*如果读到 OK or ERROR，说明一条命令读完了*/
			if(strstr(buf,"OK\n") || strstr(buf,"ERROR\n")){
				break;
			}
		}
		*pos = '\0';




		if(strlen(buf) > 2)
			LOGE("Recv %d: %s", dev->CardNo, buf);
		/*simed?*/
		if( strstr(buf, "CPIN:")){
			if(strstr(buf, "READY")){
				msg->simcard =1;
				msg->mask |= SIMCARD_MASK;
			}
		}

		/*imsi?*/
		if( (pos = strstr(buf, "CIMI")) != NULL && !strstr(buf, "ERROR") && strstr(buf,"OK")){

			int i = 0;
			int j = 0;
			pos += strlen("CIMI");
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

			if(strlen(imsi) > 10){
				msg->mask |= IMSI_MASK;
				strcpy( msg->imsi, imsi);
			}
			LOGE("imsi: %s\n",msg->imsi);

		}

		/*IMEI?*/
		if( (pos = strstr(buf, "+GSN") ) != NULL){
			int i = 0;
			int j = 0;
			pos += strlen("+GSN");
			while( pos[i]){
				if( pos[i] >= '0' && pos[i] <= '9'){
					imei[j]= pos[i];
					j++;
					if(j >=15)
						break;
				}
				i++;
			}
			imei[j] ='\0';

			msg->mask |= IMEI_MASK;
			strcpy( msg->imei, imei);
			LOGE("imei: %s\n",msg->imei);
		}

		/*signal?*/
		if( (pos = strstr(buf, "^HDRCSQ:") ) != NULL){
			msg->mask |= SIGNAL_MASK;
			msg->signal = atoi(pos + 8);
			/*转换统一值*/
			if(msg->signal >= 100)  //some 3G may show 100~199
				msg->signal -=100;
			else if (msg->signal < 31 && msg->signal >= 0)
				msg->signal *= 3.3;
			else if (msg->signal == 31)
				msg->signal = 100;
			else
				msg->signal = 0;   //illegal signal
		}else if((pos = strstr(buf, "CSQ:") ) != NULL){
			msg->mask |= SIGNAL_MASK;
			msg->signal = atoi(pos + 4);
			/*转换统一值*/
			if(msg->signal >= 100)	//some 3G may show 100~199
				msg->signal -=100;
			else if (msg->signal < 31 && msg->signal >= 0)
				msg->signal *= 3.3;
			else if (msg->signal == 31)
				msg->signal = 100;
			else
				msg->signal = 0;   //illegal signal
		}

		/*netmod?*/
		if( (pos = strstr(buf ,"+BMRAT:")) != NULL){
			if(strstr(pos, "LTE") != NULL){
				if(strstr(dev->IMSI,"46001") != NULL){
					msg->netmode = MBNET_4G_MODE_UNICOM_FDDLTE;
				}
				else if((strstr(dev->IMSI,"46003") != NULL) || \
						(strstr(dev->IMSI,"46005") != NULL) || \
						(strstr(dev->IMSI,"46011") != NULL)){
					msg->netmode = MBNET_4G_MODE_TELECOM_FDDLTE;
				}
				else if((strstr(dev->IMSI,"46000") != NULL) || \
						(strstr(dev->IMSI,"46002") != NULL) || \
						(strstr(dev->IMSI,"46007") != NULL)){
					msg->netmode = MBNET_4G_MODE_TDLTE;
				}else{
					msg->netmode = MBNET_4G_MODE_OPERATORS;
				}
			}else if(strstr(pos, "TDSCDMA") != NULL){
				msg->netmode = MBNET_3G_MODE_TDSCDMA;
			}else if((strstr(pos, "WCDMA") != NULL) || (strstr(pos, "UMTS") != NULL) || \
					 (strstr(pos, "HSPA+") != NULL) || (strstr(pos, "HSUPA") != NULL) || \
					 (strstr(pos, "HSDPA") != NULL)){
				msg->netmode = MBNET_3G_MODE_WCDMA;
			}else if((strstr(pos, "GPRS") != NULL) || (strstr(pos, "EDGE") != NULL)){
				msg->netmode = MBNET_2G_MODE_GSM;
			}else if((strstr(pos, "HDR RevA") != NULL) || (strstr(pos, "HDR RevB") != NULL) || \
					 (strstr(pos, "HDR Rev0") != NULL) || (strstr(pos, "1x") != NULL)){
				msg->netmode = MBNET_3G_MODE_CDMA2000;
			}else{
				msg->netmode = MBNET_MODE_UNKNOWN;
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

		/*link down*/
		if( (pos = strstr(buf , "DATADISCONNECT")) != NULL){
			msg->mask |= NDIS_DISCON_MASK;
		}
	}

	return 0;
}

static void broadmobi_bm806c_reset( const thread_t *thread , const card_t *dev)
{
	LOGD( "not support now!");
}

static void broadmobi_bm806c_stop( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "at$qcrmcall=0,1");
}

static int broadmobi_bm806c_get_one_msg( const thread_t *thread, char *msg , unsigned int len)
{

	return read( thread->at_fd, msg, len);

}

static void broadmobi_bm806c_send_cmd( const thread_t *thread, char *cmd)
{

	SendCmd(thread , cmd);
}

driver_t * broadmobi_bm806c_driver_init( void )
{
	static int broadmobi_bm806c_inited = 0;

	if( broadmobi_bm806c_inited > 0)
		return &broadmobi_bm806c_driver;
	broadmobi_bm806c_inited++;
	broadmobi_bm806c_driver.Vid = 0x2020;
	broadmobi_bm806c_driver.Pid = 0x2033;
	strcpy(broadmobi_bm806c_driver.sProduct, "bm806c");
	strcpy(broadmobi_bm806c_driver.sVendor, "BROADMOBI");
	broadmobi_bm806c_driver.datapath = 1;
	broadmobi_bm806c_driver.atpath = 2;
	broadmobi_bm806c_driver.DialType = NDIS_DIAL_TYPE;
	broadmobi_bm806c_driver.Capability = MBNET_4G_MODE_TDLTE | MBNET_4G_MODE_TELECOM_FDDLTE | MBNET_4G_MODE_UNICOM_FDDLTE | \
										MBNET_3G_MODE_TDSCDMA | MBNET_3G_MODE_WCDMA | MBNET_3G_MODE_CDMA2000 | \
										MBNET_2G_MODE_CDMA | MBNET_2G_MODE_GSM;
	broadmobi_bm806c_driver.init = broadmobi_bm806c_init;
	broadmobi_bm806c_driver.sim = broadmobi_bm806c_sim;
	broadmobi_bm806c_driver.imsi = broadmobi_bm806c_imsi;
	broadmobi_bm806c_driver.signal = broadmobi_bm806c_signal;
	broadmobi_bm806c_driver.netmode = broadmobi_bm806c_netmode;
	broadmobi_bm806c_driver.fix = broadmobi_bm806c_fix;
	broadmobi_bm806c_driver.process = broadmobi_bm806c_process;
	broadmobi_bm806c_driver.stop = broadmobi_bm806c_stop;
	broadmobi_bm806c_driver.reset = broadmobi_bm806c_reset;
	broadmobi_bm806c_driver.send_cmd = broadmobi_bm806c_send_cmd;
	broadmobi_bm806c_driver.get_one_msg = broadmobi_bm806c_get_one_msg;
	broadmobi_bm806c_driver.data =NULL;
	return &broadmobi_bm806c_driver;
}
