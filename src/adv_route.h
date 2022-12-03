#ifndef __LIB_ADV_ROUTE_H__
#define __LIB_ADV_ROUTE_H__


/***************        API    ***************/
/**
    notify：这个库所提供的功能不是用来提供策略路由的服务，仅仅是用来解决无线双卡的路由策略问题
**/

/**
 对应命令 ip route add/delete [A.B.C.D/M | default] dev [ifname] src [A.B.C.D] table [N]  
 参数cmd的格式为：“add/delete [A.B.C.D/M | default] dev [ifname] src [A.B.C.D] table [N]”
 return：0   - OK     
         < 0 - failed
**/

int advrt_do_route_cmd(char *cmd);

/**
 对应命令 ip rule add/delete from [A.B.C.D] table [N]
 参数cmd的格式为：“add/delete from [A.B.C.D] table [N]”
 return：0   - OK     
         < 0 - failed
**/
int advrt_do_rule_cmd(char *cmd);


#endif
