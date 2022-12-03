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

static driver_t quectel_ec20_driver;

static void quectel_ec20_init( const thread_t * thread, const card_t *dev)
{
	SendCmd(thread, "AT");
	SendCmd(thread, "AT+CFUN=1");
	SendCmd(thread, "AT&F");
	/*Set MSG text mode*/
	SendCmd(thread, "AT+CMGF=1");
	/*Select MSG storage*/
	SendCmd(thread, "AT+CPMS=\"ME\",\"ME\",\"ME\"");
	/*Set MSG mode*/
	SendCmd(thread, "AT+CNMI=1,1,0,1,0");
	/*Set CallIn mode*/
	SendCmd(thread, "AT+CLIP=1");
	/*close auto connect*/
	SendCmd(thread, "at+qcfg=\"rmnet/autoconnect\",0,0");
	/*close mbn*/
	SendCmd(thread, "at+qcfg=\"ModemRstLevel\",0");
}

static void quectel_ec20_sim( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CPIN?");
	SendCmd(thread, "AT+GSN");
}

static void quectel_ec20_imsi( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CIMI");
	sleep(1);
}

static void quectel_ec20_signal ( const thread_t *thread, const card_t *dev )
{
	SendCmd(thread ,"AT+CSQ");
	sleep(1);

}
static void quectel_ec20_netmode( const thread_t *thread, const card_t *dev )
{
	SendCmd(thread, "AT+COPS?");
	sleep(1);
}

static void quectel_ec20_netscanmode( const thread_t *thread, const card_t *dev, const dial_t *cfg)
{
	if(strstr(dev->IMSI,"46000") || strstr(dev->IMSI,"46002") || strstr(dev->IMSI,"46007") || \
			strstr(dev->IMSI,"46004")){ //移动
		switch(cfg->iNetScanMode){
			case MBNET_NETSCANMODE_AUTO:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",0");
				break;
			case MBNET_NETSCANMODE_2G_ONLY:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",1");
				break;
			case MBNET_NETSCANMODE_3G_ONLY:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",4");
				break;
			case MBNET_NETSCANMODE_4G_ONLY:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",3");
				break;
			default:
				break;
		}
	}else if(strstr(dev->IMSI,"46000")){	//联通
		switch(cfg->iNetScanMode){
			case MBNET_NETSCANMODE_AUTO:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",0");
				break;
			case MBNET_NETSCANMODE_2G_ONLY:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",1");
				break;
			case MBNET_NETSCANMODE_3G_ONLY:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",5");
				break;
			case MBNET_NETSCANMODE_4G_ONLY:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",3");
				break;
			default:
				break;
		}
	}else if(strstr(dev->IMSI,"46003") || strstr(dev->IMSI,"46005") || strstr(dev->IMSI,"46011")){	//电信
		switch(cfg->iNetScanMode){
			case MBNET_NETSCANMODE_AUTO:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",0");
				break;
			case MBNET_NETSCANMODE_2G_ONLY:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",6");
				break;
			case MBNET_NETSCANMODE_3G_ONLY:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",8");
				break;
			case MBNET_NETSCANMODE_4G_ONLY:
				SendCmd(thread, "AT+QCFG=\"nwscanmode\",3");
				break;
			default:
				break;
		}
	}
}

static void quectel_ec20_fix (const thread_t *thread, const card_t *dev , const dial_t *cfg)
{
	char cmd[256];

	memset( cmd , 0, sizeof(cmd));
	sprintf(cmd, "AT+QICSGP=1,1,\"%s\",\"%s\",\"%s\",%d", \
			cfg->sAPN,cfg->sUserName,cfg->sPassWord,cfg->iAuthentication);
	SendCmd(thread, cmd);

	if(quectel_ec20_driver.DialType != QMANAGE_DIAL_TYPE){
		if(dev->CurrentNetMode == MBNET_3G_MODE_CDMA2000){
			SendCmd(thread, "at$qcrmcall=1,1,1,1,1");
			sleep(3); //wait 3 secends for connect
		}else{
			memset( cmd , 0, sizeof(cmd));
			sprintf(cmd, "AT+CGDCONT=1,\"IP\",\"%s\"", cfg->sAPN);
			SendCmd(thread, cmd);
			SendCmd(thread, "at$qcrmcall=1,1,1,2,1");
			sleep(1);
		}
	}
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

static int quectel_ec20_process( const thread_t *thread, const card_t *dev,  msg_t *msg)
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

		if(strlen(buf) > 2 && !strstr(buf,"CSQ") && !strstr(buf,"COPS"))
			LOGE("Recv : %s",buf);
		/*simed?*/
		if( strstr(buf, "CPIN:")){
			if(strstr(buf, "READY")){
				msg->simcard =1;	
				msg->mask |= SIMCARD_MASK;
			}
		}
		/*imsi?*/
		if( (pos = strstr(buf, "+CIMI")) != NULL && !strstr(buf, "ERROR") && strstr(buf,"OK")){
			
			int i = 0;
			int j = 0;
			pos += strlen("+CIMI");
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

		}
		/*signal?*/
		if( (pos = strstr(buf, "+CSQ:") ) != NULL){
			msg->mask |= SIGNAL_MASK;	
			msg->signal = atoi(pos + 5);
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
		if( (pos = strstr(buf ,"+COPS:")) != NULL){

			int tel=0;
			int mobile=0;
			int unicom=0;
			int operators=0;
			if( strstr( pos, "MOBILE"))
				mobile = 1;
			else if ( strstr( pos, "UNICOM"))
				unicom =1;
			else if ( strstr( pos, "46011"))
				tel = 1;
			else if (strstr( pos, "CT"))
				tel = 1;
			else if (strstr( pos, "\""))
				operators = 1;

			pos = strstr((pos + 12), ",");

			if(pos){
				pos++;
				switch(atoi( pos )){
					case 0:
						msg->netmode = MBNET_2G_MODE_GSM;
						if( tel )
							msg->netmode = MBNET_2G_MODE_CDMA;
						break;
					case 2:
					case 3:
					case 4:
					case 5:
					case 6:
						if(mobile)
							msg->netmode = MBNET_3G_MODE_TDSCDMA;
						else if(unicom)
							msg->netmode = MBNET_3G_MODE_WCDMA;
						else if(tel)
							msg->netmode = MBNET_3G_MODE_CDMA2000;
						else if(operators)
							msg->netmode = MBNET_3G_MODE_OPERATORS;
						break;
					case 7:
						if(mobile)
							msg->netmode = MBNET_4G_MODE_TDLTE;
						else if (unicom)
							msg->netmode = MBNET_4G_MODE_UNICOM_FDDLTE;
						else if (tel)
							msg->netmode = MBNET_4G_MODE_TELECOM_FDDLTE;
						else if (operators)
							msg->netmode = MBNET_4G_MODE_OPERATORS;
						break;
					case 100:
						if (tel)
							msg->netmode = MBNET_3G_MODE_CDMA2000;
						else if (operators)
							msg->netmode = MBNET_3G_MODE_OPERATORS;

						break;
					default:
						msg->netmode = MBNET_MODE_UNKNOWN;
						break;
				}
			}


			if(msg->netmode != MBNET_MODE_UNKNOWN)
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

	}

	return 0;
}

static void quectel_ec20_reset( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CFUN=0");
	sleep(6);
	SendCmd(thread, "AT+CFUN=1");
}

static void quectel_ec20_stop( const thread_t *thread , const card_t *dev)
{
	if(quectel_ec20_driver.DialType != QMANAGE_DIAL_TYPE){
		if(dev->CurrentNetMode == MBNET_3G_MODE_CDMA2000){
			SendCmd(thread, "at$qcrmcall=0,1,1,1,1");
		}else{
			SendCmd(thread, "at$qcrmcall=0,1,1,2,1");
		}
	}
}

static int quectel_ec20_get_one_msg( const thread_t *thread, char *msg , unsigned int len)
{
	return read( thread->at_fd, msg, len);
}

static void quectel_ec20_send_cmd( const thread_t *thread, char *cmd)
{
	SendCmd(thread , cmd);
}

driver_t * quectel_ec20_driver_init( void )
{
	static int quectel_ec20_inited = 0;

	if( quectel_ec20_inited > 0)
		return &quectel_ec20_driver;
	quectel_ec20_inited++;
	quectel_ec20_driver.Vid = 0x05c6;
	quectel_ec20_driver.Pid = 0x9215;
	strcpy(quectel_ec20_driver.sProduct, "quectel_ec20");
	strcpy(quectel_ec20_driver.sVendor, "Quectel");
	quectel_ec20_driver.datapath = 3;
	quectel_ec20_driver.atpath = 2;
	quectel_ec20_driver.DialType = QMANAGE_DIAL_TYPE;
	quectel_ec20_driver.Capability = MBNET_4G_MODE_TDLTE | MBNET_4G_MODE_TELECOM_FDDLTE | MBNET_4G_MODE_UNICOM_FDDLTE | \
									MBNET_3G_MODE_TDSCDMA |MBNET_3G_MODE_CDMA2000|MBNET_3G_MODE_WCDMA;
	quectel_ec20_driver.init = quectel_ec20_init;
	quectel_ec20_driver.sim = quectel_ec20_sim;
	quectel_ec20_driver.imsi = quectel_ec20_imsi;
	quectel_ec20_driver.signal = quectel_ec20_signal;
	quectel_ec20_driver.netmode = quectel_ec20_netmode;
	quectel_ec20_driver.netscanmode = quectel_ec20_netscanmode;
	quectel_ec20_driver.fix = quectel_ec20_fix;
	quectel_ec20_driver.process = quectel_ec20_process;
	quectel_ec20_driver.stop = quectel_ec20_stop;
	quectel_ec20_driver.reset = quectel_ec20_reset;
	quectel_ec20_driver.send_cmd = quectel_ec20_send_cmd;
	quectel_ec20_driver.get_one_msg = quectel_ec20_get_one_msg;
	quectel_ec20_driver.data =NULL;
	return &quectel_ec20_driver;
}
