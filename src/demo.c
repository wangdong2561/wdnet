/*
	zhaoxin@kedacom.com
	2016.4.11
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "mbnet_api.h"

#define  PLUGIN		0
#define	 VENDOR		1
#define  DOWN		2
#define  UP		3

struct list_head{
	struct list_head *next,*prev;
};

typedef struct {
	card_t card;
	int status;
	struct timeval start_time;
	struct list_head node;
}module_t;

#define DBG( fmt, ...) \
	do { \
		printf("[%s, %d]=> " fmt "\n", \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); \
	} while (0)

#define list_entry(link, type, member) ((type *)((char *)(link)-(unsigned long)(&((type *)0)->member)))

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

#define list_for_each(pos, head) for(pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n, head) for(pos = (head)->next, n = (pos)->next; pos != (head); pos = n, n = pos->next )

static inline void __list_add(struct list_head *new,
		struct list_head *prev, struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void __list_del(struct list_head * prev, struct list_head * next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void list_head_init(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}

static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = entry;
	entry->prev = entry;
}

static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}


pthread_mutex_t  list_lock;
static  LIST_HEAD( all_cards );


static void event_handler(const callback_t *cb)
{

	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	module_t *module = NULL;
	sms_t  *sms = NULL;
	struct timeval now;

	gettimeofday( &now, NULL);
	switch (cb->event){
		case MBNET_CARD_MSG_IN:
		case MBNET_CARD_CALL_IN:
			DBG(" Card %d msg/call in\n", cb->card);
			sms =(sms_t *) cb->data;
			DBG("tel:%s\n", sms->phone);
			if(strlen(sms->msg))
				DBG("msg:%s\n", sms->msg);
			break;
		case MBNET_CARD_LINK_UP:
			DBG("Card %d LINK up\n",cb->card);
			pthread_mutex_lock(&list_lock);
			list_for_each( pos , &all_cards){
				module = list_entry( pos , module_t, node);
				if(module->card.CardNo == cb->card){
					module->status = UP;
					module->start_time.tv_sec = now.tv_sec;
					module->start_time.tv_usec = now.tv_usec;
					break;
				}	
			}
			
			pthread_mutex_unlock(&list_lock);
			break;
		case MBNET_CARD_LINK_DOWN:
			DBG("Card %d Link Down\n",cb->card);
			pthread_mutex_lock(&list_lock);
			list_for_each( pos , &all_cards){
				module = list_entry( pos , module_t, node);
				if(module->card.CardNo == cb->card){
					module->status = DOWN;
					module->start_time.tv_sec = 0;
					module->start_time.tv_usec = 0;
					break;
				}	
			}
			pthread_mutex_unlock(&list_lock);
			break;
		case MBNET_CARD_SIM_EVENT:
			DBG("Find sim card\n");
			break;
		case MBNET_CARD_NET_VENDOR:
			DBG("find vendor: %s\n",(char *)(cb->data));
			pthread_mutex_lock(&list_lock);
			list_for_each( pos , &all_cards){
				module = list_entry( pos , module_t , node);
				if( module->card.CardNo == cb->card){
					module->status = VENDOR;
				}
			}
			pthread_mutex_unlock(&list_lock);
			break;
		case MBNET_CARD_PLUGIN:
			DBG("find a card !");
			module = malloc (sizeof(module_t));	
			if(!module){
				DBG("no memory!\n");
				break;
			}

			memset( module , 0, sizeof(module_t));
			memcpy( &module->card , (card_t *)cb->data, sizeof(card_t));
			module->status = PLUGIN;
			module->start_time.tv_sec = 0;
			module->start_time.tv_usec = 0;
			pthread_mutex_lock( &list_lock);
			list_add_tail( &module->node, &all_cards);	
			pthread_mutex_unlock( &list_lock);
			break;

		case MBNET_CARD_PLUGOUT:
			DBG(" PLUTOUT......\n");
			pthread_mutex_lock( &list_lock);
			list_for_each_safe( pos ,n , &all_cards){
				module = list_entry( pos , module_t , node);
				if( module->card.CardNo == cb->card){
					list_del( pos);	
					free(module);
					break;
				}
			}
			pthread_mutex_unlock( &list_lock);
			break;

		case MBNET_CARD_NET_CHANGED:
			DBG("The net have changed!");
			DBG("net netmode is %x\n", *(int*)cb->data);
			break;
	}


}


static int show_card_info( card_t *card)
{
	struct in_addr addr;
	if( !card){
		DBG("param error\n");
		return -1;
	}

	DBG(" Card %d %s Msg:\n", card->CardNo, card->sProduct);
	DBG("inteface:%s", card->iface);
	DBG( "signal:%d" , card->SigStrength);
	DBG("netmode:%d\n", card->CurrentNetMode);
	DBG("LineTime:%d\n" , card->iOnLineTime);
	DBG("InDataCount:%d\n",card->iInDataCount);
	DBG("OutDataCount:%d\n",card->iOutDataCount);
	addr.s_addr = card->local_ipaddr;
	DBG("ipaddr:%s\n",  inet_ntoa(addr));
	addr.s_addr = card->peer_ipaddr;
	DBG("peer:%s\n",  inet_ntoa(addr));
	addr.s_addr = card->gateway;
	DBG("gateway:%s\n",  inet_ntoa(addr));
	addr.s_addr = card->netmask;
	DBG("netmask:%s\n",  inet_ntoa(addr));
	addr.s_addr = card->dns1_addr;
	DBG("dns1:%s\n",  inet_ntoa(addr));
	addr.s_addr = card->dns2_addr;
	DBG("dns2:%s\n",  inet_ntoa(addr));
	DBG("IMSI:%s\n",card->IMSI);
	DBG("IMEI:%s\n",card->IMEI);
	return 0;

}

int main(int argc, char *argv[])
{
	card_t card;
	module_t *module = NULL;
	struct list_head *pos = NULL;
	struct timeval now;
	struct timeval tv;
	dial_t cfg;
	int status;
	int cardno;
	
	DBG("%s\n", MbnetGetVersion());

	MbnetDebug( 1); // 同时在console和syslog（默认）中输出

	pthread_mutex_init(&list_lock, NULL);

	MbnetInit( event_handler);

	while(1){
		gettimeofday( &now, NULL);
		pthread_mutex_lock(&list_lock);
		list_for_each( pos , &all_cards){

			module = list_entry( pos , module_t , node);

			status = module->status;
			cardno = module->card.CardNo;
			tv.tv_sec = module->start_time.tv_sec;
			tv.tv_usec = module->start_time.tv_usec;
			pthread_mutex_unlock(&list_lock);

			if(status == VENDOR || status == DOWN){
				if(tv.tv_usec == 0){

					pthread_mutex_lock(&list_lock);
					module->start_time.tv_sec = now.tv_sec;
					module->start_time.tv_usec = now.tv_usec;
					pthread_mutex_unlock(&list_lock);
#if 0	//APN
					sprintf(cfg.sAPN,"test");
					sprintf(cfg.sUserName,"usen");
					sprintf(cfg.sPassWord,"passw");
					cfg.iAuthentication = 2;
					MbnetStart( cardno ,&cfg);
#else
					MbnetStart( cardno ,NULL);
#endif

				}
				else if ( (now.tv_sec - tv.tv_sec)  > 30){ /*10s*/
					MbnetStop( cardno );
					MbnetStart( cardno, NULL);

					pthread_mutex_lock(&list_lock);
					module->start_time.tv_sec = now.tv_sec;
					module->start_time.tv_usec = now.tv_usec;
					pthread_mutex_unlock(&list_lock);
				}else{
					sleep(5);	

					DBG(" The Card %d is dialing!\n", cardno);
				}
			}else if (status == UP){
				memset( &card , 0 , sizeof(card_t));
				if( !MbnetGetCardInfo( cardno ,&card)){
					show_card_info( &card);
				}
				sleep(5);

				/*	拨上号后，100s后，主动断开，重新拨号，验证断开功能*/
				DBG("now: %ld, start:%ld", now.tv_sec, tv.tv_sec);

				if ( (now.tv_sec - tv.tv_sec)  > 60){ /*100s*/
					int i;
					MbnetStop( cardno );
					DBG("called mbnet stop\n");
					for(i=0;i<8;i++)
						sleep(i);
					DBG("sleep 30s done\n");
					MbnetStart( cardno, NULL);
					DBG("called mbnet start\n");
					pthread_mutex_lock(&list_lock);
					module->start_time.tv_sec = now.tv_sec;
					module->start_time.tv_usec = now.tv_usec;
					pthread_mutex_unlock(&list_lock);
				}
			}else {

				DBG("The Card %d isn't ready!\n", cardno);
			}

			pthread_mutex_lock(&list_lock);
		}
		pthread_mutex_unlock(&list_lock);
		usleep(500*1000);
	}
}
