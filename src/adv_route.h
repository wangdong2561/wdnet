#ifndef __LIB_ADV_ROUTE_H__
#define __LIB_ADV_ROUTE_H__


/***************        API    ***************/
/**
    notify����������ṩ�Ĺ��ܲ��������ṩ����·�ɵķ��񣬽����������������˫����·�ɲ�������
**/

/**
 ��Ӧ���� ip route add/delete [A.B.C.D/M | default] dev [ifname] src [A.B.C.D] table [N]  
 ����cmd�ĸ�ʽΪ����add/delete [A.B.C.D/M | default] dev [ifname] src [A.B.C.D] table [N]��
 return��0   - OK     
         < 0 - failed
**/

int advrt_do_route_cmd(char *cmd);

/**
 ��Ӧ���� ip rule add/delete from [A.B.C.D] table [N]
 ����cmd�ĸ�ʽΪ����add/delete from [A.B.C.D] table [N]��
 return��0   - OK     
         < 0 - failed
**/
int advrt_do_rule_cmd(char *cmd);


#endif
