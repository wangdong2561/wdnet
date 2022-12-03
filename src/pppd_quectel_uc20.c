#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "debug.h"
#include "list.h"
#include "mbnet.h"
#include "pppd.h"
#include "tty.h"

static driver_t quectel_uc20_driver;

static pppd_script_t quectel_uc20_script =
{

	.script[UNICOM_NETMODE]=
	{
		.pppd_connect_script=
"--DATACOM\n\
lcp-echo-interval 30\n\
lcp-echo-failure 100\n\
115200\n\
debug\n\
logfile  --LOGFILE\n\
nodetach\n\
noauth\n\
noccp\n\
nobsdcomp\n\
nodeflate\n\
nopcomp\n\
novj\n\
novjccomp\n\
usepeerdns\n\
ipcp-accept-local\n\
ipcp-accept-remote\n\
defaultroute\n\
connect \'/bin/chat-netra  -v -s -S -f  --CHATFILE\'\n\
disconnect \'/bin/chat-netra  -v -s -S -f  --DISCHATFILE\'\n\
user--USERNAME\n\
password--PASSWORD\n\
\n",

		.pppd_chat_script =
"TIMEOUT  10\n\
ABORT  ERROR\n\
ABORT  BUSY\n\
ABORT  DELAYED\n\
\'\'   AT\n\
OK  \'AT+CGDCONT=1,\"IP\",\"--APNNAME\"\'\n\
OK  ATDT--CALLPHONENUM\n\
CONNECT  \'\'\n",

		.pppd_disconnect_script =
"ABORT  \"ERROR\"\n\
ABORT  \"NO DIALTONE\"\n\
SAY    \"\nSending break to the modem\n\"\n \
\"\"\\K\"\n \
\"\"+++ATH\"\n \
SAY \"\nGood Bye\n\" \n \
",
	},
};

static void quectel_uc20_init( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT");
	sleep(1);
	SendCmd(thread, "AT&F");
	/*Set MSG text mode*/
	SendCmd(thread, "AT+CMGF=1");
	/*Select MSG storage*/
	SendCmd(thread, "AT+CPMS=\"ME\",\"ME\",\"ME\"");
	/*Set MSG mode*/
	SendCmd(thread, "AT+CNMI=1,1,0,1,0");
	/*Set CallIn mode*/
	SendCmd(thread, "AT+CLIP=1");
}

static void quectel_uc20_sim( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CPIN?");
}

static void quectel_uc20_imsi( const thread_t *thread, const card_t *dev)
{
	SendCmd(thread,"AT+CIMI");
}

static void quectel_uc20_signal ( const thread_t *thread , const card_t *dev )
{
	SendCmd(thread, "AT+CSQ");

}
static void quectel_uc20_netmode( const thread_t *thread, const card_t *dev)
{
	SendCmd(thread, "AT+COPS?");
}



static void quectel_uc20_fix (const thread_t *thread , const card_t *dev , const dial_t *cfg)
{

}

static int quectel_uc20_process(   const thread_t *thread, const card_t *dev, msg_t *msg)
{
	char buf[256];
	char *pos = NULL;
	int nbytes = 0;
	int ret = -1;
	int try_cnt = 5;
	char imsi[MBNET_MAX_IMSI_LEN];

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

		}
		*pos = '\0';

		/*simed?*/
		if( strstr(buf, "CPIN:")){
			if(strstr(buf, "READY"))
				msg->simcard =1;
				msg->mask |= SIMCARD_MASK;
		}
		/*imsi?*/
		if( (pos = strstr(buf, "+CIMI")) != NULL){
			if((pos = strstr(buf, "460")) != NULL){
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

		}

		/*signal?*/
		if( (pos = strstr(buf, "+CSQ:") ) != NULL){
			msg->mask |= SIGNAL_MASK;
			msg->signal = atoi(pos + 5);
			if(msg->signal == 31)
				msg->signal = 100;
			else
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

static void quectel_uc20_reset( const thread_t *thread, const card_t *dev)
{
	//TODO
}

static void quectel_uc20_stop( const thread_t *thread, const card_t *dev)
{
	SendCmd(thread, "ATH");
}

static int quectel_uc20_get_one_msg( const thread_t *thread , char *msg, unsigned int len)
{
	return read( thread->at_fd, msg, len);
}

static void quectel_uc20_send_cmd( const thread_t *thread , char *cmd)
{
	SendCmd(thread , cmd);
}

driver_t * quectel_uc20_driver_init( void )
{
	static int quectel_uc20_inited = 0;

	if( quectel_uc20_inited )
		return &quectel_uc20_driver;

	quectel_uc20_inited++;
	quectel_uc20_driver.Pid = 0x9003;
	quectel_uc20_driver.Vid = 0x05c6;
	strcpy(quectel_uc20_driver.sProduct, "uc20");
	strcpy(quectel_uc20_driver.sVendor, "quectel");
	quectel_uc20_driver.datapath = 3;
	quectel_uc20_driver.atpath = 2;
	quectel_uc20_driver.DialType = PPPD_DIAL_TYPE;
	quectel_uc20_driver.Capability = MBNET_3G_MODE_WCDMA;
	quectel_uc20_driver.init = quectel_uc20_init;
	quectel_uc20_driver.sim = quectel_uc20_sim;
	quectel_uc20_driver.imsi = quectel_uc20_imsi;
	quectel_uc20_driver.signal = quectel_uc20_signal;
	quectel_uc20_driver.netmode = quectel_uc20_netmode;
	quectel_uc20_driver.fix = quectel_uc20_fix;
	quectel_uc20_driver.process = quectel_uc20_process;
	quectel_uc20_driver.stop = quectel_uc20_stop;
	quectel_uc20_driver.reset = quectel_uc20_reset;
	quectel_uc20_driver.send_cmd = quectel_uc20_send_cmd;
	quectel_uc20_driver.get_one_msg = quectel_uc20_get_one_msg;
	quectel_uc20_driver.data =(void *)&quectel_uc20_script;
	return &quectel_uc20_driver;
}
