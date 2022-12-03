#ifndef __MBNET_DRIVER_H
#define __MBNET_DRIVER_H

void drivers_init( void );
const driver_t * get_driver(unsigned int Vid,unsigned int Pid);
#endif
