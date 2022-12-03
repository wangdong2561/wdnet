#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include "mbnet.h"
#include "mbnet_api.h"
#include "debug.h"
#define UDHCPC_PATH    "/sbin/udhcpc"

/*
	physslot = usb-%s-%s 其实是记录了path:后面内容。
		the first %s is bus name
		the second %s is devpath

0: name:"GSM modem (1-port)" vendor:2020 product:2033 num_ports:1 port:1 path:usb-musb-hdrc.1-1.4
1: name:"GSM modem (1-port)" vendor:2020 product:2033 num_ports:1 port:1 path:usb-musb-hdrc.1-1.4
2: name:"GSM modem (1-port)" vendor:2020 product:2033 num_ports:1 port:1 path:usb-musb-hdrc.1-1.4
3: name:"GSM modem (1-port)" vendor:2020 product:2033 num_ports:1 port:1 path:usb-musb-hdrc.1-1.4

/sys/devices/platform/omap/ti81xx-usbss/musb-hdrc.1/usb2/2-1/2-1.4/2-1.4:1.4/net/usb0
1.通过bus name 先找到总线号如，musb-hdrc.1对应usb2总线，2为总线号。
2.通过总线号与physlot中的devpath构建最终的usb设备地址路径2-1.4
3.如果usb设备地址路径与某一网口的设备路径匹配，并且总线号也是匹配的，则为同一个设备，从而找到网络接口

*/

static int get_netdev_name( char *nic , char *physlot)
{
	char usb_busname[128]; 
	int  usb_busno = -1;
	char usb_devpath[16];
	char usb_dev_addr[20];
	char *first_pos = NULL;
	char *last_pos = NULL;
	DIR *dir = NULL;
	struct dirent *dentry = NULL;
	char *iface_dir = "/sys/class/net/";
	char  *path = NULL;
	char *softlink = NULL;
	int ret = -1;
	char *ptr = NULL;
	

	if(!nic || !physlot)
		return -1;	


	first_pos = strchr( physlot, '-');
	last_pos = strrchr( physlot, '-');
	if(!first_pos || !last_pos)
		return -1;
	if(first_pos +1 >= last_pos)
		return -1;
	memset(usb_busname, 0, sizeof(usb_busname));
	memset(usb_devpath, 0, sizeof(usb_devpath));
	strncpy(usb_busname, (first_pos +1) ,(last_pos - first_pos -1));
	/*delete the \n char at the end of physlot
		the '\n' is from seq file system in driver/usb/usb-serial.c*/
	strncpy(usb_devpath, last_pos+1, strlen(last_pos+1)-1);
	if(strlen(usb_devpath) <= 0){
		LOGE("physlot error!\n");
		return -1;
	}

	softlink = malloc(1024);
	if(!softlink){
		LOGE("no memory\n");
		return -1;
	}

	memset(softlink, 0, 1024);

	dir = opendir(iface_dir);
	if(dir == NULL){
		LOGE("open %s dir failed!\n",iface_dir);
	
	}

	while((dentry = readdir(dir)) != NULL){
		/*顺序读取每一个目录项；跳过“..”和“.”两个目录*/
		if(strcmp(dentry->d_name,".") == 0 || strcmp(dentry->d_name,"..") == 0)
			continue;

		if(strcmp(dentry->d_name,"lo") == 0)
			continue;

		path = malloc(strlen(iface_dir) + strlen(dentry->d_name) +1);

		if (NULL == path){
			LOGE("no memory!\n");
			goto out;
		}

		memset(path, 0, (strlen(iface_dir)+strlen(dentry->d_name)+1));
		sprintf(path, "%s%s", iface_dir, dentry->d_name);

		memset(softlink, 0, 1023);
		/*readlink() does not append a null byte to buf.*/ 
		if( readlink(path, softlink, (1023))  < 0){
			LOGE("readlink failed\n");
			free(path);
			continue;
		}
		free(path);

		/*检查softlink中是否包含usb_bus*/	
		if( !strstr(softlink, usb_busname)){
			continue;	
		} 

		ptr = strstr(softlink,"/usb");
		if( !ptr ){
		
			LOGE("usb bus error,please check usb sys fs!\n");
		}
		ptr = ptr + 4;
		usb_busno = strtol(ptr,NULL,10);
		memset(usb_dev_addr, 0 , sizeof(usb_dev_addr));
		sprintf(usb_dev_addr , "%d-%s",usb_busno, usb_devpath);

		/*match*/
		/*检查softlink中是否包含bus-devpath,ex:2-1.0*/
		if(strstr(softlink, usb_dev_addr)){
			strcpy(nic, dentry->d_name);
			ret = 0;
			goto out;
		}


	}
out:
	free(softlink);
	closedir(dir);
	return ret;
}

static pid_t get_pid_by_ifname( char *ifname )
{
	pid_t pid = -1;
	char pid_file[128];
	char buf[32];
	int fd = -1;
	int ret = -1;

	memset(pid_file, 0 ,sizeof(pid_file));
	sprintf( pid_file , "/var/run/udhcpc.%s.pid", ifname);
	if( access (pid_file , F_OK))
		return -1;

	memset(buf , 0 ,sizeof(buf));
	
	fd = open( pid_file , O_RDONLY);
	if(fd < 0)
		return -1;
	
	ret = read (fd ,buf ,sizeof(buf));
	if(ret > 0){
		pid = atoi(buf);
		close(fd);
		return pid;
	}
	close(fd);
	return -1;
}

static int ifdown(char *nic)
{
	struct ifreq ifr;
	int fd = -1;
	int ret = -1;
	strncpy(ifr.ifr_name, nic , sizeof(ifr.ifr_name));
	
	fd = socket(AF_INET, SOCK_DGRAM , 0);
	if(fd < 0){
		LOGE("socket create fail!\n");
		return -1;
	}

	ret = ioctl(fd , SIOCGIFFLAGS, &ifr);
	if(ret < 0){
		LOGE("socket ioctl fail!\n");
		close(fd);
		return -1;
	}
	
	ifr.ifr_flags &= ~IFF_UP;

	ret = ioctl(fd , SIOCSIFFLAGS, &ifr);
	if(ret < 0){
		LOGE("socket ioctl fail!\n");
		close(fd);
		return -1;
	}
	close(fd);

	return 0;
	//TODO
}

/*用户直接使用*/
int ndis_get_dns(const char *iface, unsigned int *dns1, unsigned int *dns2)
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


int ndis_start( cardinst_t * inst )
{
	card_t *card = NULL;
	thread_t *thread_info = NULL;
	pid_t pid = -1;
	char nic[MBNET_MAX_INTERFACE_NAME ];
	char pid_file[128];
	

	if(  !inst)
		return MBNET_INVALID_PA;
	card = &inst->dev;
	thread_info = &inst->thread_info;
	
	if( !strlen(card->iface) ){
		memset( nic , 0 , sizeof(nic));
		if( get_netdev_name(nic, card->PhySlot)){
			LOGE("Cant find the PhySlot:%s device",card->PhySlot);
			return MBNET_ERROR;
		}

		pthread_mutex_lock( &inst->dev_mutex);
		strcpy(card->iface, nic);
		pthread_mutex_unlock( &inst->dev_mutex)	;
	}

	pid = vfork();
	if(pid < 0){
		LOGE("vfork for ndis failed\n");
		return -1;
	}else if(pid == 0){
		/*child*/
		int ret;
		int tmpfd;
		/*Save fork.......*/
		for(tmpfd=3; tmpfd<4096; tmpfd++)
			close(tmpfd);
		setpgid(0,0);

		memset( pid_file , 0, sizeof(pid_file));
		sprintf( pid_file , "/var/run/udhcpc.%s.pid", card->iface);

		ret = execl(UDHCPC_PATH, "udhcpc", "-i", card->iface,"-t","3","-T","2","-n", "-s", "/etc/scripts/udhcpc_ndis.script",  "-p" , pid_file, NULL);
	}else{
		/*father*/
		/*match with udhcpc*/
		wait(NULL);

		pthread_mutex_lock(&thread_info->thread_mutex);
		thread_info->dial_pid = pid;
		pthread_mutex_unlock(&thread_info->thread_mutex);
	}

	return 0;
}



int ndis_stop( cardinst_t *inst )
{
	thread_t *thread_info = NULL;	
	card_t *dev = NULL;
	pid_t pid = -1;
	if( !inst)
		return MBNET_ERROR;
	thread_info = &inst->thread_info;	
	dev = &inst->dev;

	pthread_mutex_lock( &thread_info->thread_mutex);
	if( thread_info->dial_pid > 0)
		killpg(thread_info->dial_pid, SIGKILL);
	thread_info->dial_pid = -1;
	pthread_mutex_unlock( &thread_info->thread_mutex);
	/*stop udhcpc*/
	pid = get_pid_by_ifname(dev->iface);	
	if(pid > 0)
		killpg(pid, SIGKILL);
	
	waitpid( pid , NULL, WNOHANG);
	ifdown(dev->iface);
	return MBNET_OK;
}
