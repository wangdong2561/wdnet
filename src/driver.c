#include "list.h"
#include "mbnet.h"
#include "mbnet_api.h"
#include "debug.h"

static LIST_HEAD(drivers_list);

typedef driver_t * (*driver_init)(void);

/*所有驱动程序必须在此记录初始化指针*/
extern driver_t * longsung_u6300_driver_init( void );
extern driver_t * longsung_u8300c_driver_init( void );
extern driver_t * longsung_u9300c_driver_init( void );
extern driver_t * quectel_ec20_driver_init( void );
extern driver_t * quectel_ec20v2_driver_init( void );
extern driver_t * quectel_uc20_driver_init( void );
extern driver_t * broadmobi_bm806c_driver_init( void );
extern driver_t * zte_me3760_driver_init( void );
extern driver_t * huawei_mu709s_driver_init( void );
extern driver_t * huawei_me909s_driver_init( void );
extern driver_t * youfang_n720_driver_init( void );
extern driver_t * simcom_7600e_driver_init( void );
extern driver_t * nodecom_nl660_driver_init( void );

static driver_init gdrivers[]={

	longsung_u8300c_driver_init,
	longsung_u9300c_driver_init,
	longsung_u6300_driver_init,
	quectel_ec20_driver_init,
	quectel_ec20v2_driver_init,
	quectel_uc20_driver_init,
	broadmobi_bm806c_driver_init,
	zte_me3760_driver_init,
	huawei_mu709s_driver_init,
	huawei_me909s_driver_init,
	youfang_n720_driver_init,
	simcom_7600e_driver_init,
	nodecom_nl660_driver_init,

};


static int is_valid(driver_t *d)
{
	if( ! d)
		return -1;	
	if( !d->Pid || !d->Vid)
		return -1;
	if( !d->init || !d->sim || !d->imsi  || \
			!d->process || !d->signal || !d->netmode || \
			!d->send_cmd ||!d->get_one_msg || !d->fix)
		return -1;
	return 0;
}

static int register_driver( driver_t *driver)
{
	driver_t *d = NULL;	
	struct list_head *pos = NULL;

	if(is_valid(driver))
		return MBNET_INVALID_PA;

	/*是否已经注册过*/	

	list_for_each(pos, &drivers_list){

		d = list_entry(pos, driver_t, node);
		if(d->Pid == driver->Pid && d->Vid == driver->Vid){
			LOGI("The driver haved register!\n");
			return MBNET_ERROR;
		}
	}

	/*加入尾*/
	list_add_tail(&driver->node,&drivers_list);
	return 0;
}

/*初始化时调用*/

void drivers_init( void )
{

	int i;
	int array_cnt = 0;


	array_cnt = sizeof(gdrivers)/sizeof(driver_init);

	for( i = 0 ; i< array_cnt ; i++)
	{   
		register_driver(gdrivers[i]());
	}   

}

const driver_t * get_driver(unsigned int Vid,unsigned int Pid)
{
	const driver_t *d = NULL;
	struct list_head *pos = NULL;

	if(!Pid || !Vid){
		LOGE("INVALID PARAM!");
		return (driver_t *)NULL;	
	}

	list_for_each(pos,&drivers_list){
		d = list_entry(pos, driver_t ,node);
		if(d->Pid == Pid && Vid == d->Vid)
			return d;
	}
	return (driver_t *) NULL;
}
