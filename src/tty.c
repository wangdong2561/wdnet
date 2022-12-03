#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <errno.h>
#include "mbnet.h"

int OpenTTY(char *dev)
{
    int fd;

    if(!dev)
    {
        printf("Invalid dev\n");
        return -1;
    }

    fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) 
    {
        printf("Open %s\n", dev);
        perror("Unable to open ");
    }
    else 
    {
        fcntl(fd, F_SETFL, 0);
    }

    return fd;
}

int SendCmd(thread_t *thread,char *buf)
{
	int i, buflen;
	int fd = thread->at_fd;
	int ret;

	pthread_mutex_lock( &thread->fd_mutex);
	buflen = strlen(buf);
	for(i = 0; i < buflen; i++)
	{
		ret = write(fd, &buf[i], 1);
		if(ret <= 0) 
		{
			pthread_mutex_unlock( &thread->fd_mutex);
			return ret;
		}
		usleep(10000);
	}
	ret = write(fd, "\r", 1);
	if(ret <= 0)
	{
		pthread_mutex_unlock( &thread->fd_mutex);
		return ret;
	}
	usleep(10000);

	tcdrain(fd);
	pthread_mutex_unlock( &thread->fd_mutex);

	return 0;
}

int SetOpt(int fd,int nSpeed, int nBits, char nEvent, int nStop)
{
	struct termios newtio;

	bzero(&newtio, sizeof(newtio));

	newtio.c_cflag  |=  CLOCAL | CREAD;
	newtio.c_iflag  |=  IGNPAR | ICRNL;
	newtio.c_lflag  = ICANON ;
	newtio.c_oflag = 0;
	newtio.c_cflag &= ~CSIZE;

	switch(nBits)
	{
		case 7:
			newtio.c_cflag |= CS7;
			break;
		case 8:
			newtio.c_cflag |= CS8;
			break;
	}

	switch(nEvent)
	{
		case 'O':
			newtio.c_cflag |= PARENB;
			newtio.c_cflag |= PARODD;
			newtio.c_iflag |= (INPCK | ISTRIP);
			break;
		case 'E':
			newtio.c_iflag |= (INPCK | ISTRIP);
			newtio.c_cflag |= PARENB;
			newtio.c_cflag &= ~PARODD;
			break;
		case 'N': 
			newtio.c_cflag &= ~PARENB;
			break;
	}

	switch(nSpeed)
	{
		case 2400:
			cfsetispeed(&newtio, B2400);
			cfsetospeed(&newtio, B2400);
			break;
        case 4800:
            cfsetispeed(&newtio, B4800);
            cfsetospeed(&newtio, B4800);
            break;
        case 9600:
            cfsetispeed(&newtio, B9600);
            cfsetospeed(&newtio, B9600);
            break;
        case 115200:
            cfsetispeed(&newtio, B115200);
            cfsetospeed(&newtio, B115200);
            break;
        case 460800:
            cfsetispeed(&newtio, B460800);
            cfsetospeed(&newtio, B460800);
            break;
        default:
            cfsetispeed(&newtio, B9600);
            cfsetospeed(&newtio, B9600);
            break;
    }

    if(nStop == 1)
        newtio.c_cflag &=  ~CSTOPB;
    else if (nStop == 2)
        newtio.c_cflag |=  CSTOPB;

    newtio.c_cc[VTIME]  = 0;
    newtio.c_cc[VMIN] = 0;

    tcflush(fd,TCIFLUSH);

    if((tcsetattr(fd,TCSANOW,&newtio))!=0)
    {
        return -1;
    }

    tcflush(fd,TCIFLUSH); 

    return 0;
}

