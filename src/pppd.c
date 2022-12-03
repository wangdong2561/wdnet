#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include "mbnet.h"
#include "mbnet_api.h"
#include "debug.h"
#include "pppd.h"
#include "list.h"
#define PPPD_EXEC      "/bin/pppd"
#define CHAT_EXEC      "/bin/chat"


static int replace_tok(char *str, unsigned int size, const char *old_tok, const char *new_tok)
{
	char *ptr = NULL;
	char *pos = NULL;
	char *buf = NULL;
	int i = 0;

	buf = malloc(size);

	if(buf == NULL || str==NULL )
	{
		free(buf);
		return MBNET_ERROR;
	}

	memset(buf, 0, size);

	pos = str;

	while((ptr = strstr( pos , old_tok)) !=0)
	{
		if(ptr - pos != 0) 
			memcpy(&buf[i], pos, ptr - pos);
		memcpy(&buf[i + ptr - pos], new_tok, strlen(new_tok));
		i += ptr - pos + strlen( new_tok );
		pos = ptr + strlen(old_tok);
	}

	strcat(buf, pos);
	memset( str , 0, size);
	strcpy(str, buf);

	free(buf);
	return MBNET_OK;

}

static int build_connect_script_file(const char *script ,const card_t *card, const dial_t *cfg)
{
	char file[32];
	char backfile[32];
	char *file_content = NULL;
	char new_tok[64];
	int fd = -1;
	unsigned int len = 0;
	memset( file , 0, sizeof(file));
	sprintf( file , "/tmp/%s_%d.ppp", card->sProduct, card->CardNo);
	memset ( backfile, 0 ,sizeof(backfile));
	sprintf(backfile, "/tmp/%s_%d.ppp.bak", card->sProduct, card->CardNo);
	if( !access( file, F_OK) ){
		if( !access(backfile, F_OK))
			remove( backfile);
		rename(file , backfile);
	}	
	
	fd = open(file ,O_RDWR|O_CREAT|O_TRUNC|O_SYNC,\
			S_IRWXO|S_IRWXU|S_IRWXG);
	if( fd < 0){
		LOGE("open %s failed\n", file);
		return MBNET_ERROR;
	}

	/*有些字符要被换掉，更换后最多多出256byte*/	
	len = strlen(script) + 256;
	file_content = malloc( len);
	if( !file_content){
		
		LOGE("no memory!");
		close(fd);
		return MBNET_ERROR;
	}

	memset( file_content, 0, len);
	memcpy( file_content, script, strlen(script));

	/*replace --DATACOM,--LOGFILE, --CHATFILE, --DISCHATFILE,--USERNAME, --PASSWORD*/ 
	memset( new_tok, 0 , sizeof(new_tok));	
	sprintf( new_tok, "/dev/ttyUSB%d", card->ttyUSBData);
	replace_tok( file_content, len, "--DATACOM", new_tok);

	/*--LOGFILE*/
	memset( new_tok , 0, sizeof(new_tok));
	memset( file , 0, sizeof(file));
	sprintf( file , "/tmp/%s_%d.log", card->sProduct, card->CardNo);
	memset ( backfile, 0 ,sizeof(backfile));
	sprintf(backfile, "/tmp/%s_%d.log.bak", card->sProduct, card->CardNo);
	if( !access( file, F_OK) ){
		if( !access(backfile, F_OK))
			remove( backfile);
		rename(file , backfile);
	}	
	
	strcpy(new_tok, file);
	replace_tok( file_content, len, "--LOGFILE", new_tok);

	/*--CHATFILE*/
	
	memset( new_tok, 0, sizeof(new_tok));
	sprintf(new_tok , "/tmp/%s_%d.chat", card->sProduct, card->CardNo);
	replace_tok( file_content, len, "--CHATFILE", new_tok);
	/*--DISCHATFILE*/	
	
	memset( new_tok, 0, sizeof(new_tok));
	sprintf(new_tok , "/tmp/%s_%d.dischat", card->sProduct, card->CardNo);
	replace_tok( file_content, len, "--DISCHATFILE", new_tok);
	/*--USERNAME*/
	if( strlen(cfg->sUserName))
		replace_tok( file_content, len, "--USERNAME", cfg->sUserName);
	else
		replace_tok( file_content, len, "user--USERNAME", cfg->sUserName);
	/*--PASSWORD*/
	if( strlen(cfg->sPassWord))
		replace_tok( file_content, len, "--PASSWORD", cfg->sPassWord);
	else
		replace_tok( file_content, len, "password--PASSWORD", cfg->sPassWord);

	if( write(fd, file_content, strlen(file_content)) < 0){
		free(file_content);
		close(fd);
		LOGE("build dial scripte file failed!\n");
		return MBNET_ERROR;
	}

	free(file_content);
	close(fd);

	return MBNET_OK;

}

static int build_chat_script_file( const char *script, const card_t *card, const dial_t *cfg)
{
	char file[32];
	char backfile[32];
	char *file_content = NULL;
	char new_tok[64];
	int fd = -1;
	unsigned int len = 0;
	memset( new_tok , 0, sizeof(new_tok));
	memset( file , 0, sizeof(file));
	sprintf( file , "/tmp/%s_%d.chat", card->sProduct, card->CardNo);
	memset ( backfile, 0 ,sizeof(backfile));
	sprintf(backfile, "/tmp/%s_%d.chat.bak", card->sProduct, card->CardNo);
	if( !access( file, F_OK) ){
		if( !access(backfile, F_OK))
			remove( backfile);
		rename(file , backfile);
	}	

	fd = open(file ,O_RDWR|O_CREAT|O_TRUNC|O_SYNC,\
			S_IRWXO|S_IRWXU|S_IRWXG);
	if( fd < 0){
		LOGE("open %s failed\n", file);
		return MBNET_ERROR;
	}

	/*有些字符要被换掉，更换后最多多出256byte*/	
	len = strlen(script) + 256;
	file_content = malloc( len);
	if( !file_content){
		
		LOGE("no memory!");
		close(fd);
		return MBNET_ERROR;
	}

	memset( file_content, 0, len);
	memcpy( file_content, script, strlen(script));
	/* replace --APNNAME --CALLPHONENUM*/
	replace_tok( file_content, len, "--APNNAME" , cfg->sAPN);
	replace_tok( file_content, len, "--CALLPHONENUM", cfg->sCallNum);

	if( write(fd, file_content, strlen(file_content)) < 0){
		free(file_content);
		close(fd);
		LOGE("build dial scripte file failed!\n");
		return MBNET_ERROR;
	}

	free(file_content);
	close(fd);
	return MBNET_OK;
}

static int build_disconnect_script_file(const char *script, const card_t *card, const dial_t *cfg)
{
	char file[32];
	char backfile[32];
	int fd = -1;
	memset( file , 0, sizeof(file));
	sprintf( file , "/tmp/%s_%d.dischat", card->sProduct, card->CardNo);
	memset ( backfile, 0 ,sizeof(backfile));
	sprintf(backfile, "/tmp/%s_%d.dischat.bak", card->sProduct, card->CardNo);
	if( !access( file, F_OK) ){
		if( !access(backfile, F_OK))
			remove( backfile);
		rename(file , backfile);
	}	

	fd = open(file ,O_RDWR|O_CREAT|O_TRUNC|O_SYNC,\
			S_IRWXO|S_IRWXU|S_IRWXG);
	if( fd < 0){
		LOGE("open %s failed\n", file);
		return MBNET_ERROR;
	}
	
	if( write(fd, script, strlen(script)) < 0){
		close(fd);
		LOGE("build dial scripte file failed!\n");
		return MBNET_ERROR;
	}
	close(fd);
	return MBNET_OK;

}

int pppd_get_dns(const char* iface, unsigned int *dns1, unsigned int *dns2)
{
    char FileName[30], DnsName[20];
    char *tmp = NULL, *line = NULL;
    int nbytes, DnsNum, i;
    FILE *fd;
    size_t len;
    struct in_addr inp;

    memset(FileName, 0, sizeof(FileName));

     /*get DNS*/
    sprintf(FileName, "/etc/ppp/resolv.conf%s",iface);
    DnsNum = 0;

    fd = fopen(FileName, "r");
    if(fd <= 0) {
	LOGE("can't find resolv.conf file!\n");
        return -1;
    }

    while((nbytes = getline(&line, &len, fd)) != -1)
    {
        tmp = strstr(line, "nameserver");
        if(tmp == NULL)
            goto retry;

        tmp += 11;
        if (tmp > (len + line))
            goto retry;

	memset(DnsName, 0, sizeof(DnsName));
        i = 0;
        while((*tmp == '.') || (*tmp >= '0' && *tmp <= '9'))
        {
            DnsName[i++] = *tmp;
            tmp++;
            if(i >= 19)
                break;
        }
        DnsName[i] = '\0';

        memset(&inp, 0, sizeof(struct in_addr));
        inet_aton(DnsName, &inp);
        if(DnsNum == 0)
            *dns1 = (unsigned int)inp.s_addr;
        else
            *dns2 = (unsigned int)inp.s_addr;

        DnsNum++;
        if(DnsNum >= 2)
        break;

retry:
        free(line);
        line = NULL;
    }

    if(line)
        free(line);
    fclose(fd);
    return DnsNum;

}

int pppd_start( cardinst_t *inst )
{

	card_t  *dev = NULL;
	const driver_t *driver = NULL;
	script_t *script = NULL;
	dial_t *cfg = NULL;
	pid_t pid = -1;
	thread_t *thread_info = NULL;
	char file[32];
	char interface[16];

	int vendor = -1;


	if(NULL == inst )
		return MBNET_INVALID_PA;

	dev = &inst->dev;
	cfg = &inst->cfg;

	thread_info = &inst->thread_info;

	driver = inst->driver;

	if( !driver->data ){
		LOGE("%s driver->data is null\n",dev->sProduct);
		return MBNET_ERROR;
	}

	if( !strlen ( dev->iface) ){
		sprintf( interface ,"ppp%d" , dev->CardNo);
		pthread_mutex_lock( &inst->dev_mutex);
		strcpy(dev->iface , interface);
		pthread_mutex_unlock( &inst->dev_mutex);
	}

	if( strcmp(dev->NetVendor, "unicom")){
	
		vendor = MBNET_CHINA_UNICOM;

	}else if ( strstr( dev->NetVendor, "mobile")){
		vendor = MBNET_CHINA_MOBILE;

	}else if ( strstr( dev->NetVendor, "telecom")){
		vendor = MBNET_CHINA_TELECOM;
	}

	switch(vendor){
		/*使用wcdmas格式*/
		case MBNET_CHINA_UNICOM:
			script =(script_t *) (((pppd_script_t *)(driver->data))->script) +UNICOM_NETMODE;
			break;
			/*使用cdma2000格式*/
		case MBNET_CHINA_TELECOM:
			script =(script_t *) (((pppd_script_t *)(driver->data))->script) +TELECOM_NETMODE;
			break;
			/*使用TDSCDMA格式*/
		case MBNET_CHINA_MOBILE:
			script =(script_t *) (((pppd_script_t *)(driver->data))->script) +MOBILE_NETMODE;
			break;
		default:
			return MBNET_ERROR;
			break;

	}


	if( !script->pppd_connect_script || !script->pppd_chat_script || !script->pppd_disconnect_script){

		LOGE("%s driver->data->pppd_connect_script is null", dev->sProduct);
		return MBNET_ERROR;
	}

	if( build_connect_script_file(script->pppd_connect_script, dev , cfg ) ){
		LOGE("build connect script file for %s_%d fail!\n", dev->sProduct, dev->CardNo);
		return MBNET_ERROR;
	}
		
	
	if( build_chat_script_file(script->pppd_chat_script, dev , cfg )){
		LOGE("build chat script file for %s_%d fail!\n", dev->sProduct, dev->CardNo);
		return MBNET_ERROR;

	}
	if( build_disconnect_script_file(script->pppd_disconnect_script, dev , cfg ) ){

		LOGE("build dischat script file for %s_%d fail!\n", dev->sProduct, dev->CardNo);
		return MBNET_ERROR;
	}


	memset( interface , 0, sizeof(interface));
	sprintf(interface, "%d", dev->CardNo);
	memset(file, 0, sizeof(file));
	sprintf(file, "/tmp/%s_%d.ppp", dev->sProduct, dev->CardNo);
	pid = vfork();
	if(pid < 0){

		LOGE("vfork for pppd failed\n");
		return -1;
	}else if(pid == 0){   
		int ret;
		int tmpfd;
		/*Save fork.......*/ 
		for(tmpfd=3; tmpfd<4096; tmpfd++)
			close(tmpfd);
		setpgid(0,0);
		ret = execl(PPPD_EXEC, "pppd", "file", file, "unit", interface, NULL);
		perror("execl pppd failed:");
		exit(-1);
	}else{
		pthread_mutex_lock( &thread_info->thread_mutex);
		thread_info->dial_pid =pid;
		pthread_mutex_unlock( &thread_info->thread_mutex);
	}
	return 0;
}

int pppd_stop( cardinst_t *inst)
{
	thread_t *thread_info = NULL;
	if ( !inst)
		return MBNET_INVALID_PA;


	thread_info = &inst->thread_info;

	pthread_mutex_lock(&thread_info->thread_mutex);
	if( thread_info->dial_pid ) {
		killpg(thread_info->dial_pid, SIGTERM);
		sleep(1);
		killpg(thread_info->dial_pid, SIGKILL);
	}
	thread_info->dial_pid = -1;
	pthread_mutex_unlock(&thread_info->thread_mutex);

	return MBNET_OK;
}

