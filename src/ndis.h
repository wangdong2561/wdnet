#ifndef __MBNET_NDIS_H
#define __MBNET_NDIS_H
int ndis_stop( cardinst_t *inst );
int ndis_start( cardinst_t * inst );
int ndis_get_dns(const char *iface, unsigned int *dns1, unsigned int *dns2);
#endif
