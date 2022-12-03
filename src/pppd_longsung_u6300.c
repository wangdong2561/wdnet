#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "debug.h"
#include "list.h"
#include "mbnet.h"
#include "pppd.h"
#include "tty.h"

static driver_t longsung_u6300_driver;

static pppd_script_t longsung_u6300_script =
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
connect \'/bin/chat  -v -s -S -f  --CHATFILE\'\n\
disconnect \'/bin/chat  -v -s -S -f  --DISCHATFILE\'\n\
user--USERNAME\n\
password--PASSWORD\n\
\n",

		.pppd_chat_script =
"TIMEOUT  10\n\
ABORT  ERROR\n\
ABORT  BUSY\n\
ABORT  DELAYED\n\
\'\'   AT\n\
OK  AT+MODODR=2\n\
OK  AT+CNMI=2,1\n\
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

static void longsung_u6300_init( const thread_t *thread , const card_t *dev)
{
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


}

static void longsung_u6300_sim( const thread_t *thread , const card_t *dev)
{
	SendCmd(thread, "AT+CPIN?");
}

static void longsung_u6300_imsi( const thread_t *thread, const card_t *dev)
{
	SendCmd(thread,"AT+CIMI");
}

static void longsung_u6300_signal ( const thread_t *thread , const card_t *dev )
{
	SendCmd(thread, "AT+CSQ");

}
static void longsung_u6300_netmode( const thread_t *thread, const card_t *dev)
{
	SendCmd(thread, "AT+CREG?");
	SendCmd(thread, "AT+MODODR?");
}



static void longsung_u6300_fix (const thread_t *thread , const card_t *dev , const dial_t *cfg)
{
	char cmd[256];
	memset( cmd , 0, sizeof(cmd));
	sprintf(cmd , "AT+CGDCONT=1,\"IP\",\"%s\"", cfg->sAPN);
	SendCmd(thread, cmd);
	sleep(1);
}

static int longsung_u6300_process(   const thread_t *thread, const card_t *dev, msg_t *msg)
{
	char buf[256];	
	char *pos = NULL;
	int nbytes = 0;
	int ret = -1;
	int flag = 0;
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
		if( (pos = strstr(buf, "+CIMI")) != NULL){
			
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
					
			msg->mask |= IMSI_MASK;
			strcpy( msg->imsi, imsi);

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
		if( (pos = strstr(buf, "+MODODR:")) != NULL){

			if( *(pos + 9) > '0' && *(pos + 9) < '5'){
				switch( atoi(pos +9)){
					case 1:
					case 2:
						msg->netmode = MBNET_3G_MODE_WCDMA;
						break;
					case 3:
					case 4:
						msg->netmode = MBNET_2G_MODE_GSM;
						break;
					default:
						msg->netmode = MBNET_MODE_UNKNOWN;
						break;

				}

				msg->mask |= NETMODE_MASK;
			}	


		}


	}
	return 0;
}

static void longsung_u6300_reset( const thread_t *thread, const card_t *dev)
{
	//TODO
}

static void longsung_u6300_stop( const thread_t *thread, const card_t *dev)
{
	SendCmd(thread, "ATH");
}

static int longsung_u6300_get_one_msg( const thread_t *thread , char *msg, unsigned int len)
{
	return read( thread->at_fd, msg, len);
}

static void longsung_u6300_send_cmd( const thread_t *thread , char *cmd)
{
	SendCmd(thread , cmd);
}

driver_t * longsung_u6300_driver_init( void )
{
	static int longsung_u6300_inited = 0;

	if( longsung_u6300_inited )
		return &longsung_u6300_driver;

	longsung_u6300_inited++;
	longsung_u6300_driver.Pid = 0x9603;
	longsung_u6300_driver.Vid = 0x1c9e;
	strcpy(longsung_u6300_driver.sProduct, "6300");
	strcpy(longsung_u6300_driver.sVendor, "LONGSUNG");
	longsung_u6300_driver.datapath = 2;
	longsung_u6300_driver.atpath = 1;
	longsung_u6300_driver.DialType = PPPD_DIAL_TYPE;
	longsung_u6300_driver.Capability = MBNET_3G_MODE_WCDMA;
	longsung_u6300_driver.init = longsung_u6300_init;
	longsung_u6300_driver.sim = longsung_u6300_sim;
	longsung_u6300_driver.imsi = longsung_u6300_imsi;
	longsung_u6300_driver.signal = longsung_u6300_signal;
	longsung_u6300_driver.netmode = longsung_u6300_netmode;
	longsung_u6300_driver.fix = longsung_u6300_fix;
	longsung_u6300_driver.process = longsung_u6300_process;
	longsung_u6300_driver.stop = longsung_u6300_stop;
	longsung_u6300_driver.reset = longsung_u6300_reset;
	longsung_u6300_driver.send_cmd = longsung_u6300_send_cmd;
	longsung_u6300_driver.get_one_msg = longsung_u6300_get_one_msg;
	longsung_u6300_driver.data =(void *)&longsung_u6300_script;
	return &longsung_u6300_driver;
}
