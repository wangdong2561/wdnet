#ifndef __MBNET_QMANAGE_H
#define __MBNET_QMANAGE_H
int qmanage_stop( cardinst_t *inst );
int qmanage_start( cardinst_t * inst );
int qmanage_get_dns(const char *iface, unsigned int *dns1, unsigned int *dns2);
#endif
