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
#include "list.h"

#define QMANAGE_EXEC "/bin/qmanage"

int qmanage_get_dns(const char *iface, unsigned int *dns1, unsigned int *dns2)
{
    char FileName[20], DnsName[20];
    char *tmp = NULL, *line = NULL;
    int nbytes, DnsNum, i;
    FILE *fd;
    size_t len;
    struct in_addr inp;

    memset(FileName, 0, sizeof(FileName));

     /*get DNS*/
    sprintf(FileName, "/tmp/udhcpc.conf%s",iface);
    DnsNum = 0;

    fd = fopen(FileName, "r");
    if(fd <= 0) {
	LOGE("can't find resolv.conf file!\n");
        return -1;
    }

    while((nbytes = getline(&line, &len, fd)) != -1)
    {
        tmp = strstr(line, "dns");
        if(tmp == NULL)
            goto retry;

        tmp += 4;
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

int qmanage_start( cardinst_t *inst )
{
	card_t  *dev = NULL;
	const driver_t *driver = NULL;
	dial_t *cfg = NULL;
	pid_t pid = -1;
	thread_t *thread_info = NULL;
	char file[32];
	char interface[16];
	char authentication[8];

	int vendor = -1;


	if(NULL == inst )
		return MBNET_INVALID_PA;

	dev = &inst->dev;
	cfg = &inst->cfg;

	thread_info = &inst->thread_info;

	driver = inst->driver;

	if( !strlen ( dev->iface) ){
		sprintf( interface ,"usb%d" , dev->CardNo);
		pthread_mutex_lock( &inst->dev_mutex);
		strcpy(dev->iface , interface);
		pthread_mutex_unlock( &inst->dev_mutex);
	}

	signal(SIGCHLD,SIG_IGN);
	pid = vfork();
	if(pid < 0){
		LOGE("vfork for qmanage failed\n");
		return -1;
	}else if(pid == 0){
		int ret;
		int tmpfd;
		/*Save fork.......*/
		for(tmpfd=3; tmpfd<4096; tmpfd++)
			close(tmpfd);
		setpgid(0,0);

		memset(file, 0, sizeof(file));
		sprintf(file, "/tmp/%s_%d.log", dev->sProduct, dev->CardNo);

		/* If parameter has a username, there must be a password and authentication
		 * only have both username and password, then we can pass the authentication to qmanage
		 */
		memset(authentication, 0, sizeof(authentication));
		if(strlen(cfg->sUserName) && strlen(cfg->sPassWord))
			sprintf(authentication, "%d",cfg->iAuthentication);

		if(strlen(cfg->sUserName) && strlen(cfg->sPassWord))
			ret = execl(QMANAGE_EXEC, "qmanage","-s",cfg->sAPN,cfg->sUserName,cfg->sPassWord,authentication, "-i", dev->iface, "-f", file, NULL);
		else
			ret = execl(QMANAGE_EXEC, "qmanage","-s",cfg->sAPN,"-i", dev->iface, "-f", file, NULL);
		perror("execl qmanage failed:");
		exit(-1);
	}else{
		pthread_mutex_lock( &thread_info->thread_mutex);
		thread_info->dial_pid =pid;
		printf("qmanage pid=%d\n",pid);
		pthread_mutex_unlock( &thread_info->thread_mutex);
	}
	return 0;
}

int qmanage_stop( cardinst_t *inst)
{
	int ret=0;
	thread_t *thread_info = NULL;
	if ( !inst)
		return MBNET_INVALID_PA;


	thread_info = &inst->thread_info;

	pthread_mutex_lock(&thread_info->thread_mutex);
	if( thread_info->dial_pid ){
		ret = killpg(thread_info->dial_pid, SIGTERM);
		printf("killed pid%d ret = %d\n",thread_info->dial_pid,ret);
	}
	thread_info->dial_pid = -1;
	pthread_mutex_unlock(&thread_info->thread_mutex);

	return MBNET_OK;
}
