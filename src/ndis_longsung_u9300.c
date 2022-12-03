/*
 * 此模块已知有bug
 * 1.PCI-E上飞行模式引脚上电时候电平一定要稳定，否则
 *   可能会导致模块进入飞行模式无法退出!
 * 2.模块要确保关闭自动连接，否则qcrmcall命令关闭网络时
 *   会报错，所以每次最后都写入关闭自动连接命令
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "list.h"
#include "mbnet.h"
#include "debug.h"
#include "tty.h"

static driver_t longsung_u9300c_driver;

static void longsung_u9300c_init( const thread_t * thread, const card_t *dev)
{

	/*set mododr*/
	SendCmd(thread, "AT+MODODR=2");
	/*psrat , find net mode*/
	SendCmd(thread, "AT+PSRAT");
	/*set default setting*/
	SendCmd(thread, "AT&F");
	/*Set MSG text mode*/
	SendCmd(thread, "AT+CMGF=1");
	/*Select MSG storage*/
	SendCmd(thread, "AT+CPMS=\"ME\",\"ME\",\"ME\"");
	/*Set MSG mode*/
	SendCmd(thread, "AT+CNMI=1,1,0,1,0");
	/*Set CallIn mode*/
	SendCmd(thread, "AT+CLIP=1");

	/*link status report*/
	SendCmd(thread, "AT+DATASTATUSEN=1");
}

static void longsung_u9300c_sim( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CPIN?");
	SendCmd(thread, "AT+QCPIN?");
	SendCmd(thread, "AT+GSN");
	sleep(1);
}

static void longsung_u9300c_imsi( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CIMI");
	SendCmd(thread, "AT+QCIMI");
	sleep(1);
}

static void longsung_u9300c_signal ( const thread_t *thread, const card_t *dev )
{
	SendCmd(thread ,"AT+CSQ");
	sleep(1);

}
static void longsung_u9300c_netmode( const thread_t *thread, const card_t *dev )
{
	SendCmd(thread, "AT+PSRAT");
	sleep(1);
}

static void longsung_u9300c_netscanmode( const thread_t *thread, const card_t *dev, const dial_t *cfg)
{
	if(strstr(dev->IMSI,"46000") || strstr(dev->IMSI,"46002") || strstr(dev->IMSI,"46007") || \
			strstr(dev->IMSI,"46004")){ //移动
		switch(cfg->iNetScanMode){
			case MBNET_NETSCANMODE_AUTO:
				SendCmd(thread, "AT+MODODR=2");
				break;
			case MBNET_NETSCANMODE_2G_ONLY:
				SendCmd(thread, "AT+MODODR=3");
				break;
			case MBNET_NETSCANMODE_3G_ONLY:
				SendCmd(thread, "AT+MODODR=6");
				break;
			case MBNET_NETSCANMODE_4G_ONLY:
				SendCmd(thread, "AT+MODODR=5");
				break;
			default:
				break;
		}
	}else if(strstr(dev->IMSI,"46000")){	//联通
		switch(cfg->iNetScanMode){
			case MBNET_NETSCANMODE_AUTO:
				SendCmd(thread, "AT+MODODR=2");
				break;
			case MBNET_NETSCANMODE_2G_ONLY:
				SendCmd(thread, "AT+MODODR=3");
				break;
			case MBNET_NETSCANMODE_3G_ONLY:
				SendCmd(thread, "AT+MODODR=1");
				break;
			case MBNET_NETSCANMODE_4G_ONLY:
				SendCmd(thread, "AT+MODODR=5");
				break;
			default:
				break;
		}
	}else if(strstr(dev->IMSI,"46003") || strstr(dev->IMSI,"46005") || strstr(dev->IMSI,"46011")){	//电信
		switch(cfg->iNetScanMode){
			case MBNET_NETSCANMODE_AUTO:
				SendCmd(thread, "AT+MODODR=2");
				break;
			case MBNET_NETSCANMODE_2G_ONLY:
				SendCmd(thread, "AT+MODODREX=15");
				break;
			case MBNET_NETSCANMODE_3G_ONLY:
				SendCmd(thread, "AT+MODODREX=14");
				break;
			case MBNET_NETSCANMODE_4G_ONLY:
				SendCmd(thread, "AT+MODODR=5");
				break;
			default:
				break;
		}
	}
}

static void longsung_u9300c_fix (const thread_t *thread, const card_t *dev , const dial_t *cfg)
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
	sprintf(cmd, "AT+CGDCONT=1,\"IP\",\"%s\"", cfg->sAPN);
	SendCmd(thread, cmd);
	sleep(2);

	if(dev->CurrentNetMode == MBNET_3G_MODE_CDMA2000){
		/*telecom 3g is not available now*/
		/*SendCmd(thread, "at+ehrpdinfo=1,101,\"APN_String:ctlte;PDN_Label:internet;PDN_IP_Version_Type:V4_V6;RAN_Type:HRPD_eHRPD; \
		    PDN_Level_Auth_Protocol:PAP;PDN_Level_Auth_User_ID:ctnet@mycdma.cn;PDN_Level_Auth_Password:vnet.mobi;\"");*/
		/*SendCmd(thread, "AT$QCRMCALL=1,1,1,1,,101");*/
		SendCmd(thread, "AT^PPPCFG=\"ctnet@mycdma.cn\",\"vnet.mobi\"");
		SendCmd(thread, "AT$QCRMCALL=1,1,1,1,1,1");
	}
	else{
		SendCmd(thread, "at$qcrmcall=1,1,1,2,1");
	}
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

static int MsgIn(char *buf, sms_t *sms,const thread_t *thread)
{
	int i, j, flags, index1, nbytes;
	int timeout = 30;		//30条指令超时
	char tmpbuf[6], cmd[20], msg[400];
	char *bufptr = NULL, *p = NULL;

	memset(cmd, 0, sizeof(cmd));
	j = 0;
	flags = 0;

	//获取索引，读取信息
	for (i = 0; buf[i] != '\r'; i++){
		if (buf[i] == ',')
			flags = 1;
		if (flags == 1)
			tmpbuf[j++] = buf[i + 1];
		if(j >= 5)
			break;
	}
	tmpbuf[j] = '\0';
	index1 = atoi(tmpbuf);
	sprintf(cmd, "AT+CMGR=%d", index1);
	SendCmd(thread, cmd);

	/*一直读AT口，直到读到CMGR命令，或者超时为止*/
	while(timeout--){
		nbytes = 399;
		bufptr = msg;
		memset(msg, 0, sizeof(msg));
		while(nbytes--){
			read(thread->at_fd, bufptr, 1);
			bufptr++;
			/*如果读到 OK 或者 ERROR，说明一条命令读完了*/
			if(strstr(msg,"OK\n") || strstr(msg,"ERROR\n"))
				break;
		}
		*bufptr = '\0';

		/*如果读到CMGR，就处理并退出while*/
		if(strstr(msg,"+CMGR:")){
			p = strstr(msg, "READ\"");  //^HCMGR
			if(p){
				p+=7;
				//Get phone num
				j = 0;
				for(i = 0; *(p + i) != ','; i++){
					if((*(p + i) >= '0') && (*(p + i) <= '9') && (j < 16))
						sms->phone[j++] = *(p + i);
					if(j >= 15)
						break;
				}
				//Get Msg
				p = strstr(p, "\n");  //^HCMGR
				p+=2;
			}
			break;
		}
	}

	//读完短信，删除
	sprintf(cmd, "AT+CMGD=%d", index1);
	SendCmd(thread, cmd);

	if(p != NULL){
		strncpy(sms->msg, p, MBNET_MAX_SMS_LEN);
	}else{	//no msg contents
		LOGE("p = NULL!!! msg=%s\n",msg);
		return MBNET_ERROR;
	}
	return MBNET_OK;
}

static int longsung_u9300c_process( const thread_t *thread, const card_t *dev,  msg_t *msg)
{
	char buf[256];
	char *pos = NULL;
	int nbytes = 0;
	int ret = -1;
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

		if(strlen(buf) > 2)// && !strstr(buf,"CSQ") && !strstr(buf,"COPS"))
			LOGE("Recv : %s",buf);
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
		/*signal?*/
		if( (pos = strstr(buf, "+CSQ:") ) != NULL){
			msg->mask |= SIGNAL_MASK;
			msg->signal = atoi(pos + 5);
			/*转换统一值*/
			if(msg->signal >= 100)  //some 3G may show 100~199
				msg->signal -=100;
			else if (msg->signal < 31 && msg->signal >= 0)
				msg->signal *= 3.3;
			else if (msg->signal == 31)
				msg->signal = 100;
			else
				msg->signal = 0;   //illegal signal
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

		/*netmod?*/
		if( (pos = strstr(buf ,"+PSRAT:")) != NULL){

			if(strstr(pos, "TDD") != NULL){
				msg->netmode = MBNET_4G_MODE_TDLTE;
			}else if(strstr(pos, "FDD") != NULL){
				if(strstr(dev->IMSI,"46001") != NULL){
					msg->netmode = MBNET_4G_MODE_UNICOM_FDDLTE;
				}
				else if((strstr(dev->IMSI,"46003") != NULL) || \
						(strstr(dev->IMSI,"46005") != NULL) || \
						(strstr(dev->IMSI,"46011") != NULL)){
					msg->netmode = MBNET_4G_MODE_TELECOM_FDDLTE;
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
			}else if((strstr(pos, "EVDO") != NULL) || (strstr(pos, "CDMA") != NULL) || \
					(strstr(pos, "EHRPD") != NULL)){
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
			if( !MsgIn( pos, &msg->sms,thread)){
				msg->mask |= SMS_MASK;
			}
		}

		/* link down*/
		if( (pos = strstr(buf , "DATADISCONN")) != NULL){
			msg->mask |= NDIS_DISCON_MASK;
		}
	}

	return 0;
}

static void longsung_u9300c_reset( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CFUN=0");
	sleep(6);
	SendCmd(thread, "AT+CFUN=1");
}

static void longsung_u9300c_stop( const thread_t *thread , const card_t *dev)
{
	if(dev->CurrentNetMode == MBNET_3G_MODE_CDMA2000){
		SendCmd(thread, "at$qcrmcall=0,1,1,1,1");
	}else{
		SendCmd(thread, "at$qcrmcall=0,1,1,2,1");
	}
}

static int longsung_u9300c_get_one_msg( const thread_t *thread, char *msg , unsigned int len)
{
	return read( thread->at_fd, msg, len);
}

static void longsung_u9300c_send_cmd( const thread_t *thread, char *cmd)
{
	SendCmd(thread , cmd);
}

driver_t * longsung_u9300c_driver_init( void )
{
	static int longsung_u9300c_inited = 0;

	if( longsung_u9300c_inited > 0)
		return &longsung_u9300c_driver;
	longsung_u9300c_inited++;
	longsung_u9300c_driver.Vid = 0x1c9e;
	longsung_u9300c_driver.Pid = 0x9b3c;
	strcpy(longsung_u9300c_driver.sProduct, "u9300c");
	strcpy(longsung_u9300c_driver.sVendor, "longsung");
	longsung_u9300c_driver.datapath = 3;
	longsung_u9300c_driver.atpath = 2;
	longsung_u9300c_driver.DialType = NDIS_DIAL_TYPE;
	longsung_u9300c_driver.Capability = MBNET_4G_MODE_TDLTE | MBNET_4G_MODE_TELECOM_FDDLTE | MBNET_4G_MODE_UNICOM_FDDLTE | \
									MBNET_3G_MODE_TDSCDMA |MBNET_3G_MODE_CDMA2000|MBNET_3G_MODE_WCDMA;
	longsung_u9300c_driver.init = longsung_u9300c_init;
	longsung_u9300c_driver.sim = longsung_u9300c_sim;
	longsung_u9300c_driver.imsi = longsung_u9300c_imsi;
	longsung_u9300c_driver.signal = longsung_u9300c_signal;
	longsung_u9300c_driver.netmode = longsung_u9300c_netmode;
	longsung_u9300c_driver.netscanmode = longsung_u9300c_netscanmode;
	longsung_u9300c_driver.fix = longsung_u9300c_fix;
	longsung_u9300c_driver.process = longsung_u9300c_process;
	longsung_u9300c_driver.stop = longsung_u9300c_stop;
	longsung_u9300c_driver.reset = longsung_u9300c_reset;
	longsung_u9300c_driver.send_cmd = longsung_u9300c_send_cmd;
	longsung_u9300c_driver.get_one_msg = longsung_u9300c_get_one_msg;
	longsung_u9300c_driver.data =NULL;
	return &longsung_u9300c_driver;
}
