#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <ctype.h>
#include "../src/debug.h"

#if 0
int parse_line (char *line, char *argv[])
{
	int nargs = 0;

	while (nargs < 16) {

		/* skip any white space */
		while ((*line == ' ') || (*line == '\t')) {
			++line;
		}

		if (*line == '\0') {	/* end of line, no more args	*/
			argv[nargs] = NULL;
			return (nargs);
		}

		argv[nargs++] = line;	/* begin of argument string	*/

		/* find end of string */
		while (*line && (*line != ' ') && (*line != '\t')) {
			++line;
		}

		if (*line == '\0') {	/* end of line, no more args	*/
			argv[nargs] = NULL;
			return (nargs);
		}

		*line++ = '\0';		/* terminate current arg	 */
	}

	printf ("** Too many args (max. %d) **\n", CONFIG_SYS_MAXARGS);

	return (nargs);
}
#endif

/* 将字符串转化为小写 */
static char *strlowr(char *str){
	char *orign=str;
	for (; *str!='\0'; str++)
		*str = tolower(*str);
	return orign;
}

int send_msg(char *str)
{
	int msgid_s; /* server send, client recive */
	int msgid_c; /* client send, server recive */
	int ret = -1, i;
	struct msg_buf msgbuff;

	msgid_c = msgget((key_t)MBNET_IPC_KEY_C, IPC_EXCL | 0666);
	if (msgid_c == -1) {
		printf("no MSGIPC_C\n");
		return -1;
	}

	msgid_s = msgget((key_t)MBNET_IPC_KEY_S, IPC_EXCL | 0666);
	if (msgid_s == -1) {
		printf("no MSGIPC_S\n");
		return -1;
	}

	strlowr(str);

	mbnet_ipc_send(msgid_c, syscall(SYS_gettid), str);

	for(i = 0; i< 10; i++){
		ret = msgrcv(msgid_s, &msgbuff, sizeof(msgbuff.msg), syscall(SYS_gettid), IPC_NOWAIT);
		if (ret == -1) {
			usleep(100*1000);
			continue;
		}

		printf("Recv:\n%s\n", msgbuff.msg.data);
	}
}

int main(void)
{
	char msg[512];
	while(1){
		memset(msg, 0, sizeof(msg));
		scanf("%s",msg);
		send_msg(msg);
	}

	return 0;


}
