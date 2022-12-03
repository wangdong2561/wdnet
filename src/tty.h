#ifndef __MBNET_TTY_H__
#define __MBNET_TTY_H__
int OpenTTY(char *dev);
int SetOpt(int fd,int nSpeed, int nBits, char nEvent, int nStop);
int SendCmd(const thread_t *thread,char *buf);
#endif

