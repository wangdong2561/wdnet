#include <stdio.h>
#include <stdlib.h>
#include "adv_route.h"
#include "errno.h"
#include "string.h"
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <limits.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <arpa/inet.h>

struct rtnl_handle
{
	int			fd;
	struct sockaddr_nl	local;
	struct sockaddr_nl	peer;
	__u32			seq;
	__u32			dump;
};

typedef struct
{
	__u8 family;
	__u8 bytelen;
	__s16 bitlen;
	__u32 data[4];
} inet_prefix;

#define NLMSG_TAIL(nmsg) \
	((struct rtattr *) (((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

typedef int (*rtnl_filter_t)(const struct sockaddr_nl *, 
			     struct nlmsghdr *n, void *);



#define NEXT_ARG() do { argv++; --argc; } while(0)

struct idxmap
{
	struct idxmap * next;
	int		index;
	int		type;
	int		alen;
	unsigned	flags;
	unsigned char	addr[8];
	char		name[16];
};

static struct idxmap *idxmap[16];
static int rtnl_rttable_init;


//////////////////          lib         //////////////////
static int parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, int len)
{
	while (RTA_OK(rta, len)) {
		if (rta->rta_type <= max)
			tb[rta->rta_type] = rta;
		rta = RTA_NEXT(rta,len);
	}
	if (len)
		fprintf(stderr, "!!!Deficit %d, rta_len=%d\n", len, rta->rta_len);
	return 0;
}

static int get_addr_1(inet_prefix *addr, const char *name, int family)
{
	const char *cp;
	unsigned char *ap = (unsigned char*)addr->data;
	int i;

	memset(addr, 0, sizeof(*addr));

	if (strcmp(name, "default") == 0 ||
	    strcmp(name, "all") == 0 ||
	    strcmp(name, "any") == 0) {
		if (family == AF_DECnet)
			return -1;
		addr->family = family;
		addr->bytelen = (family == AF_INET6 ? 16 : 4);
		addr->bitlen = -1;
		return 0;
	}

	if (strchr(name, ':')) {
		addr->family = AF_INET6;
		if (family != AF_UNSPEC && family != AF_INET6)
			return -1;
		if (inet_pton(AF_INET6, name, addr->data) <= 0)
			return -1;
		addr->bytelen = 16;
		addr->bitlen = -1;
		return 0;
	}

	addr->family = AF_INET;
	if (family != AF_UNSPEC && family != AF_INET)
		return -1;
	addr->bytelen = 4;
	addr->bitlen = -1;
	for (cp=name, i=0; *cp; cp++) {
		if (*cp <= '9' && *cp >= '0') {
			ap[i] = 10*ap[i] + (*cp-'0');
			continue;
		}
		if (*cp == '.' && ++i <= 3)
			continue;
		return -1;
	}
	return 0;
}

static int get_unsigned(unsigned *val, const char *arg, int base)
{
	unsigned long res;
	char *ptr;

	if (!arg || !*arg)
		return -1;
	res = strtoul(arg, &ptr, base);
	if (!ptr || ptr == arg || *ptr || res > UINT_MAX)
		return -1;
	*val = res;
	return 0;
}

static __u32 get_addr32(const char *name)
{
	inet_prefix addr;
	if (get_addr_1(&addr, name, AF_INET)) {
		fprintf(stderr, "Error: an IP address is expected rather than \"%s\"\n", name);
		return -1;
	}
	return addr.data[0];
}

static int get_integer(int *val, const char *arg, int base)
{
	long res;
	char *ptr;

	if (!arg || !*arg)
		return -1;
	res = strtol(arg, &ptr, base);
	if (!ptr || ptr == arg || *ptr || res > INT_MAX || res < INT_MIN)
		return -1;
	*val = res;
	return 0;
}

static int ll_name_to_index(const char *name)
{
	struct idxmap *im;
	int i;

	if (name == NULL)
		return 0;
	for (i=0; i<16; i++) {
		for (im = idxmap[i]; im; im = im->next) {
			if (strcmp(im->name, name) == 0) {
				return im->index;
			}
		}
	}
	return 0;
}

static int addattr_l(struct nlmsghdr *n, int maxlen, int type, const void *data, 
	      int alen)
{
	int len = RTA_LENGTH(alen);
	struct rtattr *rta;

	if (NLMSG_ALIGN(n->nlmsg_len) + len > maxlen) {
		fprintf(stderr, "addattr_l ERROR: message exceeded bound of %d\n",maxlen);
		return -1;
	}
	rta = (struct rtattr*)(((char*)n) + NLMSG_ALIGN(n->nlmsg_len));
	rta->rta_type = type;
	rta->rta_len = len;
	memcpy(RTA_DATA(rta), data, alen);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + len;
	return 0;
}

static int addattr32(struct nlmsghdr *n, int maxlen, int type, __u32 data)
{
	int len = RTA_LENGTH(4);
	struct rtattr *rta;
	if (NLMSG_ALIGN(n->nlmsg_len) + len > maxlen) {
		fprintf(stderr,"addattr32: Error! max allowed bound %d exceeded\n",maxlen);
		return -1;
	}
	rta = (struct rtattr*)(((char*)n) + NLMSG_ALIGN(n->nlmsg_len));
	rta->rta_type = type;
	rta->rta_len = len;
	memcpy(RTA_DATA(rta), &data, 4);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + len;
	return 0;
}

static int rta_addattr32(struct rtattr *rta, int maxlen, int type, __u32 data)
{
	int len = RTA_LENGTH(4);
	struct rtattr *subrta;

	if (RTA_ALIGN(rta->rta_len) + len > maxlen) {
		fprintf(stderr,"rta_addattr32: Error! max allowed bound %d exceeded\n",maxlen);
		return -1;
	}
	subrta = (struct rtattr*)(((char*)rta) + RTA_ALIGN(rta->rta_len));
	subrta->rta_type = type;
	subrta->rta_len = len;
	memcpy(RTA_DATA(subrta), &data, 4);
	rta->rta_len = NLMSG_ALIGN(rta->rta_len) + len;
	return 0;
}

static int parse_one_nh(struct rtattr *rta, struct rtnexthop *rtnh, int *argcp, char ***argvp)
{
	int argc = *argcp;
	char **argv = *argvp;

	while (++argv, --argc > 0) {
		if (strcmp(*argv, "via") == 0) {
			NEXT_ARG();
			rta_addattr32(rta, 4096, RTA_GATEWAY, get_addr32(*argv));
			rtnh->rtnh_len += sizeof(struct rtattr) + 4;
		} else if (strcmp(*argv, "dev") == 0) {
			NEXT_ARG();
			if ((rtnh->rtnh_ifindex = ll_name_to_index(*argv)) == 0) {
				fprintf(stderr, "Cannot find device \"%s\"\n", *argv);
				return -1;
			}
		} else if (strcmp(*argv, "weight") == 0) {
			unsigned w;
			NEXT_ARG();
			if (get_unsigned(&w, *argv, 0) || w == 0 || w > 256)
				fprintf(stderr, "\"weight\" is invalid\n");
			rtnh->rtnh_hops = w - 1;
		} else if (strcmp(*argv, "onlink") == 0) {
			rtnh->rtnh_flags |= RTNH_F_ONLINK;
		} else
			break;
	}
	*argcp = argc;
	*argvp = argv;
	return 0;
}

static int parse_nexthops(struct nlmsghdr *n, struct rtmsg *r, int argc, char **argv)
{
	char buf[1024];
	struct rtattr *rta = (void*)buf;
	struct rtnexthop *rtnh;

	rta->rta_type = RTA_MULTIPATH;
	rta->rta_len = RTA_LENGTH(0);
	rtnh = RTA_DATA(rta);

	while (argc > 0) {
		if (strcmp(*argv, "nexthop") != 0) {
			fprintf(stderr, "Error: \"nexthop\" or end of line is expected instead of \"%s\"\n", *argv);
			return -1;
		}
		if (argc <= 1) {
			fprintf(stderr, "Error: unexpected end of line after \"nexthop\"\n");
			return -1;
		}
		memset(rtnh, 0, sizeof(*rtnh));
		rtnh->rtnh_len = sizeof(*rtnh);
		rtnh->rtnh_ifindex = 0;
		rtnh->rtnh_flags = 0;
		rtnh->rtnh_hops = 0;
		rta->rta_len += rtnh->rtnh_len;
		parse_one_nh(rta, rtnh, &argc, &argv);
		rtnh = RTNH_NEXT(rtnh);
	}

	if (rta->rta_len > RTA_LENGTH(0))
		addattr_l(n, 1024, RTA_MULTIPATH, RTA_DATA(rta), RTA_PAYLOAD(rta));
	return 0;
}



static int get_addr(inet_prefix *dst, const char *arg, int family)
{
	if (family == AF_PACKET) {
		fprintf(stderr, "Error: \"%s\" may be inet address, but it is not allowed in this context.\n", arg);
		return -1;
	}
	if (get_addr_1(dst, arg, family)) {
		fprintf(stderr, "Error: an inet address is expected rather than \"%s\".\n", arg);
		return -1;
	}
	return 0;
}

static int get_prefix_1(inet_prefix *dst, const char *arg, int family)
{
	int err;
	unsigned plen;
	char *slash;
	int len = strlen(arg);
	char *p = (char *)malloc(len+1);
	if(p == NULL)
		return 0;

	memset(dst, 0, sizeof(*dst));
	strcpy(p,arg);

	if (strcmp(arg, "default") == 0 ||
	    strcmp(arg, "any") == 0 ||
	    strcmp(arg, "all") == 0) {
		if (family == AF_DECnet)
		{
			if(p != NULL)
				free(p);
			p = NULL;
			return -1;
		}
		dst->family = family;
		dst->bytelen = 0;
		dst->bitlen = 0;
		if(p != NULL)
			free(p);
		p = NULL;
		return 0;
	}

	slash = strchr(p, '/');
	if (slash)
		*slash = '\0';
	err = get_addr_1(dst, p, family);
	if (err == 0) {
		switch(dst->family) {
			case AF_INET6:
				dst->bitlen = 128;
				break;
			case AF_DECnet:
				dst->bitlen = 16;
				break;
			default:
			case AF_INET:
				dst->bitlen = 32;
		}
		if (slash) {
			if (get_integer((int *)&plen, (char *)(slash+1), 0) || plen > dst->bitlen) {
				err = -1;
				goto done;
			}
			dst->bitlen = plen;
		}
	}
done:
	if (slash)
		*slash = '/';
	if(p != NULL)
		free(p);
	p = NULL;
	return err;
}

static int get_prefix(inet_prefix *dst, const char *arg, int family)
{
	if (family == AF_PACKET) {
		fprintf(stderr, "Error: \"%s\" may be inet prefix, but it is not allowed in this context.\n", arg);
		return -1;
	}
	if (get_prefix_1(dst, arg, family)) {
		fprintf(stderr, "Error: an inet prefix is expected rather than \"%s\".\n", arg);
		return -1;
	}
	return 0;
}



static int matches(const char *cmd, const char *pattern)
{
	int len = strlen(cmd);
	if (len > strlen(pattern))
		return -1;
	return memcmp(pattern, cmd, len);
}

///////////////////   rtnl table   //////////////////////////


static char * rtnl_rttable_tab[256] = {
	"unspec",
};

static void rtnl_tab_initialize(char *file, char **tab, int size)
{
	char buf[512];
	FILE *fp;

	fp = fopen(file, "r");
	if (!fp)
		return;
	while (fgets(buf, sizeof(buf), fp)) {
		char *p = buf;
		int id;
		char namebuf[512];

		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || *p == '\n' || *p == 0)
			continue;
		if (sscanf(p, "0x%x %s\n", &id, namebuf) != 2 &&
		    sscanf(p, "0x%x %s #", &id, namebuf) != 2 &&
		    sscanf(p, "%d %s\n", &id, namebuf) != 2 &&
		    sscanf(p, "%d %s #", &id, namebuf) != 2) {
			fprintf(stderr, "Database %s is corrupted at %s\n",
				file, p);
			fclose(fp);
			return;
		}

		if (id<0 || id>size)
			continue;

		tab[id] = strdup(namebuf);
	}
	fclose(fp);
}

static void rtnl_rttable_initialize(void)
{
	rtnl_rttable_init = 1;
	rtnl_rttable_tab[255] = "local";
	rtnl_rttable_tab[254] = "main";
	rtnl_rttable_tab[253] = "default";
    rtnl_tab_initialize("/etc/iproute2/rt_tables",
			    rtnl_rttable_tab, 256);
}

static int rtnl_rttable_a2n(__u32 *id, char *arg)
{
	char *end;
	int i;

	if (!rtnl_rttable_init)
		rtnl_rttable_initialize();

	for (i=0; i<256; i++) {
		if (rtnl_rttable_tab[i] &&
		    strcmp(rtnl_rttable_tab[i], arg) == 0) {
			*id = i;
			return 0;
		}
	}

	i = strtoul(arg, &end, 0);
	if (!end || end == arg || *end || i > 255)
		return -1;
	*id = i;
	return 0;
}

static int rtnl_rtntype_a2n(int *id, char *arg)
{
	char *end;
	unsigned long res;

	if (strcmp(arg, "local") == 0)
		res = RTN_LOCAL;
	else if (strcmp(arg, "nat") == 0)
		res = RTN_NAT;
	else if (matches(arg, "broadcast") == 0 ||
		 strcmp(arg, "brd") == 0)
		res = RTN_BROADCAST;
	else if (matches(arg, "anycast") == 0)
		res = RTN_ANYCAST;
	else if (matches(arg, "multicast") == 0)
		res = RTN_MULTICAST;
	else if (matches(arg, "prohibit") == 0)
		res = RTN_PROHIBIT;
	else if (matches(arg, "unreachable") == 0)
		res = RTN_UNREACHABLE;
	else if (matches(arg, "blackhole") == 0)
		res = RTN_BLACKHOLE;
	else if (matches(arg, "xresolve") == 0)
		res = RTN_XRESOLVE;
	else if (matches(arg, "unicast") == 0)
		res = RTN_UNICAST;
	else if (strcmp(arg, "throw") == 0)
		res = RTN_THROW;
	else {
		res = strtoul(arg, &end, 0);
		if (!end || end == arg || *end || res > 255)
			return -1;
	}
	*id = res;
	return 0;
}

static int rtnl_open_byproto(struct rtnl_handle *rth, unsigned subscriptions, int protocol)
{
	int addr_len;
	int sndbuf = 32768;
	int rcvbuf = 32768;

	memset(rth, 0, sizeof(struct rtnl_handle));

	rth->fd = socket(AF_NETLINK, SOCK_RAW, protocol);
	if (rth->fd < 0) {
		perror("Cannot open netlink socket");
		return -1;
	}

	if (setsockopt(rth->fd,SOL_SOCKET,SO_SNDBUF,&sndbuf,sizeof(sndbuf)) < 0) {
		perror("SO_SNDBUF");
		
		close(rth->fd);
		rth->fd = -1;
		
		return -1;
	}

	if (setsockopt(rth->fd,SOL_SOCKET,SO_RCVBUF,&rcvbuf,sizeof(rcvbuf)) < 0) {
		perror("SO_RCVBUF");
		
		close(rth->fd);
		rth->fd = -1;
		
		return -1;
	}

	memset(&rth->local, 0, sizeof(rth->local));
	rth->local.nl_family = AF_NETLINK;
	rth->local.nl_groups = subscriptions;

	if (bind(rth->fd, (struct sockaddr*)&rth->local, sizeof(rth->local)) < 0) {
		perror("Cannot bind netlink socket");
		
		close(rth->fd);
		rth->fd = -1;
		
		return -1;
	}
	addr_len = sizeof(rth->local);
	if (getsockname(rth->fd, (struct sockaddr*)&rth->local, (unsigned int *)&addr_len) < 0) {
		perror("Cannot getsockname");
		
		close(rth->fd);
		rth->fd = -1;
		
		return -1;
	}
	if (addr_len != sizeof(rth->local)) {
		fprintf(stderr, "Wrong address length %d\n", addr_len);
		
		close(rth->fd);
		rth->fd = -1;
		
		return -1;
	}
	if (rth->local.nl_family != AF_NETLINK) {
		fprintf(stderr, "Wrong address family %d\n", rth->local.nl_family);
		
		close(rth->fd);
		rth->fd = -1;
		
		return -1;
	}
	rth->seq = time(NULL);
	return 0;
}

static int rtnl_open(struct rtnl_handle *rth, unsigned subscriptions)
{
	return rtnl_open_byproto(rth, subscriptions, NETLINK_ROUTE);
}

static void rtnl_close(struct rtnl_handle *rth)
{
	close(rth->fd);
}


static int rtnl_dump_filter(struct rtnl_handle *rth,
		     rtnl_filter_t filter,
		     void *arg1,
		     rtnl_filter_t junk,
		     void *arg2)
{
	char	buf[16384];
	struct sockaddr_nl nladdr;
	struct iovec iov = { buf, sizeof(buf) };

	while (1) {
		int status;
		struct nlmsghdr *h;

		struct msghdr msg = {
			(void*)&nladdr, sizeof(nladdr),
			&iov,	1,
			NULL,	0,
			0
		};

		status = recvmsg(rth->fd, &msg, 0);

		if (status < 0) {
			if (errno == EINTR)
				continue;
			perror("OVERRUN");
			continue;
		}
		if (status == 0) {
			fprintf(stderr, "EOF on netlink\n");
            printf("status == 0\n");
			return -1;
		}
		if (msg.msg_namelen != sizeof(nladdr)) {
			fprintf(stderr, "sender address length == %d\n", msg.msg_namelen);
			return -1;
		}

		h = (struct nlmsghdr*)buf;
		while (NLMSG_OK(h, status)) {
			int err;

			if (nladdr.nl_pid != 0 ||
			    h->nlmsg_pid != rth->local.nl_pid ||
			    h->nlmsg_seq != rth->dump) {
				if (junk) {
					err = junk(&nladdr, h, arg2);
					if (err < 0)
					{
                        printf("junk < 0\n");
						return err;
					}
				}
				goto skip_it;
			}

			if (h->nlmsg_type == NLMSG_DONE)
				return 0;
			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *err = (struct nlmsgerr*)NLMSG_DATA(h);
				if (h->nlmsg_len < NLMSG_LENGTH(sizeof(struct nlmsgerr))) {
					fprintf(stderr, "ERROR truncated\n");
				} else {
					errno = -err->error;
					perror("RTNETLINK answers");
				}
                printf("h->nlmsg_type == NLMSG_ERROR");
				return -1;
			}
			err = filter(&nladdr, h, arg1);
			if (err < 0)
				return err;

skip_it:
			h = NLMSG_NEXT(h, status);
		}
		if (msg.msg_flags & MSG_TRUNC) {
			fprintf(stderr, "Message truncated\n");
			continue;
		}
		if (status) {
			fprintf(stderr, "!!!Remnant of size %d\n", status);
			return -1;
		}
	}
}

static int rtnl_wilddump_request(struct rtnl_handle *rth, int family, int type)
{
	struct {
		struct nlmsghdr nlh;
		struct rtgenmsg g;
	} req;
	struct sockaddr_nl nladdr;

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;

	req.nlh.nlmsg_len = sizeof(req);
	req.nlh.nlmsg_type = type;
	req.nlh.nlmsg_flags = NLM_F_ROOT|NLM_F_MATCH|NLM_F_REQUEST;
	req.nlh.nlmsg_pid = 0;
	req.nlh.nlmsg_seq = rth->dump = ++rth->seq;
	req.g.rtgen_family = family;

	return sendto(rth->fd, (void*)&req, sizeof(req), 0, (struct sockaddr*)&nladdr, sizeof(nladdr));
}

static int ll_remember_index(const struct sockaddr_nl *who, 
		      struct nlmsghdr *n, void *arg)
{
	int h;
	struct ifinfomsg *ifi = NLMSG_DATA(n);
	struct idxmap *im, **imp;
	struct rtattr *tb[IFLA_MAX+1];

	if (n->nlmsg_type != RTM_NEWLINK)
		return 0;

	if (n->nlmsg_len < NLMSG_LENGTH(sizeof(struct ifinfomsg)))
		return -1;


	memset(tb, 0, sizeof(tb));
	parse_rtattr(tb, IFLA_MAX, IFLA_RTA(ifi), IFLA_PAYLOAD(n));
	if (tb[IFLA_IFNAME] == NULL)
		return 0;

	h = ifi->ifi_index&0xF;

	for (imp=&idxmap[h]; (im=*imp)!=NULL; imp = &im->next)
		if (im->index == ifi->ifi_index)
			break;

	if (im == NULL) {
		im = malloc(sizeof(*im));
		if (im == NULL)
			return 0;
		im->next = *imp;
		im->index = ifi->ifi_index;
		*imp = im;
	}

	im->type = ifi->ifi_type;
	im->flags = ifi->ifi_flags;
	if (tb[IFLA_ADDRESS]) {
		int alen;
		im->alen = alen = RTA_PAYLOAD(tb[IFLA_ADDRESS]);
		if (alen > sizeof(im->addr))
			alen = sizeof(im->addr);
		memcpy(im->addr, RTA_DATA(tb[IFLA_ADDRESS]), alen);
	} else {
		im->alen = 0;
		memset(im->addr, 0, sizeof(im->addr));
	}
	strcpy(im->name, RTA_DATA(tb[IFLA_IFNAME]));
	return 0;
}

static int ll_init_map(struct rtnl_handle *rth)
{
	if (rtnl_wilddump_request(rth, AF_UNSPEC, RTM_GETLINK) < 0) {
		perror("Cannot send dump request");
		return -1;
	}

	if (rtnl_dump_filter(rth, ll_remember_index, &idxmap, NULL, NULL) < 0) {
		fprintf(stderr, "Dump terminated\n");
		return -1;
	}
	return 0;
}

static int rtnl_talk(struct rtnl_handle *rtnl, struct nlmsghdr *n, pid_t peer,
	      unsigned groups, struct nlmsghdr *answer,
	      rtnl_filter_t junk,
	      void *jarg)
{
	int status;
	unsigned seq;
	struct nlmsghdr *h;
	struct sockaddr_nl nladdr;
	struct iovec iov = { (void*)n, n->nlmsg_len };
	char   buf[16384];
	struct msghdr msg = {
		(void*)&nladdr, sizeof(nladdr),
		&iov,	1,
		NULL,	0,
		0
	};

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	nladdr.nl_pid = peer;
	nladdr.nl_groups = groups;

	n->nlmsg_seq = seq = ++rtnl->seq;

	if (answer == NULL)
		n->nlmsg_flags |= NLM_F_ACK;

	status = sendmsg(rtnl->fd, &msg, 0);

	if (status < 0) {
		perror("Cannot talk to rtnetlink");
		return -1;
	}

	memset(buf,0,sizeof(buf));

	iov.iov_base = buf;

	while (1) {
		iov.iov_len = sizeof(buf);
		status = recvmsg(rtnl->fd, &msg, 0);

		if (status < 0) {
			if (errno == EINTR)
				continue;
			perror("OVERRUN");
			continue;
		}
		if (status == 0) {
			fprintf(stderr, "EOF on netlink\n");
			return -1;
		}
		if (msg.msg_namelen != sizeof(nladdr)) {
			fprintf(stderr, "sender address length == %d\n", msg.msg_namelen);
			return -1;
		}
		for (h = (struct nlmsghdr*)buf; status >= sizeof(*h); ) {
			int err;
			int len = h->nlmsg_len;
			int l = len - sizeof(*h);

			if (l<0 || len>status) {
				if (msg.msg_flags & MSG_TRUNC) {
					fprintf(stderr, "Truncated message\n");
					return -1;
				}
				fprintf(stderr, "!!!malformed message: len=%d\n", len);
				return -1;
			}

			if (nladdr.nl_pid != peer ||
			    h->nlmsg_pid != rtnl->local.nl_pid ||
			    h->nlmsg_seq != seq) {
				if (junk) {
					err = junk(&nladdr, h, jarg);
					if (err < 0)
						return err;
				}
				continue;
			}

			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *err = (struct nlmsgerr*)NLMSG_DATA(h);
				if (l < sizeof(struct nlmsgerr)) {
					fprintf(stderr, "ERROR truncated\n");
				} else {
					errno = -err->error;
					if (errno == 0) {
						if (answer)
							memcpy(answer, h, h->nlmsg_len);
						return 0;
					}
					perror("RTNETLINK answers");
				}
				return -1;
			}
			if (answer) {
				memcpy(answer, h, h->nlmsg_len);
				return 0;
			}

			fprintf(stderr, "Unexpected reply!!!\n");

			status -= NLMSG_ALIGN(len);
			h = (struct nlmsghdr*)((char*)h + NLMSG_ALIGN(len));
		}
		if (msg.msg_flags & MSG_TRUNC) {
			fprintf(stderr, "Message truncated\n");
			continue;
		}
		if (status) {
			fprintf(stderr, "!!!Remnant of size %d\n", status);
			return -1;
		}
	}
}

//////////////////          core        ///////////////////
static int iproute_modify(int cmd, unsigned flags, int argc, char **argv)
{
	struct rtnl_handle rth;
	struct {
		struct nlmsghdr 	n;
		struct rtmsg 		r;
		char   			buf[1024];
	} req;
	char  mxbuf[256];
	struct rtattr * mxrta = (void*)mxbuf;
	unsigned mxlock = 0;
	char  *d = NULL;
	int gw_ok = 0;
	int dst_ok = 0;
	int nhs_ok = 0;
	int scope_ok = 0;
	int table_ok = 0;
//	int proto_ok = 0;
	int type_ok = 0;

	memset(&req, 0, sizeof(req));

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST|flags;
	req.n.nlmsg_type = cmd;
	req.r.rtm_family = AF_UNSPEC;
	req.r.rtm_table = RT_TABLE_MAIN;
	req.r.rtm_scope = RT_SCOPE_NOWHERE;

	if (cmd != RTM_DELROUTE) {
		req.r.rtm_protocol = RTPROT_BOOT;
		req.r.rtm_scope = RT_SCOPE_UNIVERSE;
		req.r.rtm_type = RTN_UNICAST;
	}

	mxrta->rta_type = RTA_METRICS;
	mxrta->rta_len = RTA_LENGTH(0);

	while (argc > 0) {
		if (strcmp(*argv, "src") == 0) {
			inet_prefix addr;
			NEXT_ARG();
			get_addr(&addr, *argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC)
				req.r.rtm_family = addr.family;
			addattr_l(&req.n, sizeof(req), RTA_PREFSRC, &addr.data, addr.bytelen);
		}  else if (matches(*argv, "table") == 0) {
			int tid;
			NEXT_ARG();
			if (rtnl_rttable_a2n((__u32 *)&tid, *argv))
				fprintf(stderr, "\"table\" value is invalid\n");
			req.r.rtm_table = tid;
			table_ok = 1;
		} else if (strcmp(*argv, "dev") == 0 ||
			   strcmp(*argv, "oif") == 0) {
			NEXT_ARG();
			d = *argv;
		} else {
			int type;
			inet_prefix dst;

			if (strcmp(*argv, "to") == 0) {
				NEXT_ARG();
			}
			if ((**argv < '0' || **argv > '9') &&
			    rtnl_rtntype_a2n(&type, *argv) == 0) {
				NEXT_ARG();
				req.r.rtm_type = type;
				type_ok = 1;
			}

			if (dst_ok)
				fprintf(stderr, "Error: either \"%s\" is duplicate, or \"%s\" is a garbage.\n", "to", *argv);
			get_prefix(&dst, *argv, req.r.rtm_family);
			if (req.r.rtm_family == AF_UNSPEC)
				req.r.rtm_family = dst.family;
			req.r.rtm_dst_len = dst.bitlen;
			dst_ok = 1;
			if (dst.bytelen)
				addattr_l(&req.n, sizeof(req), RTA_DST, &dst.data, dst.bytelen);
		}
		argc--; argv++;
	}

	if (rtnl_open(&rth, 0) < 0)
		return -1;

	if (d || nhs_ok)  {
		int idx;

		ll_init_map(&rth);

		if (d) {
			if ((idx = ll_name_to_index(d)) == 0) {
			    rtnl_close(&rth);
				fprintf(stderr, "Cannot find device \"%s\"\n", d);
				return -1;
			}
			addattr32(&req.n, sizeof(req), RTA_OIF, idx);
		}
	}

	if (mxrta->rta_len > RTA_LENGTH(0)) {
		if (mxlock)
			rta_addattr32(mxrta, sizeof(mxbuf), RTAX_LOCK, mxlock);
		addattr_l(&req.n, sizeof(req), RTA_METRICS, RTA_DATA(mxrta), RTA_PAYLOAD(mxrta));
	}

	if (nhs_ok)
		parse_nexthops(&req.n, &req.r, argc, argv);

	if (!table_ok) {
		if (req.r.rtm_type == RTN_LOCAL ||
		    req.r.rtm_type == RTN_BROADCAST ||
		    req.r.rtm_type == RTN_NAT ||
		    req.r.rtm_type == RTN_ANYCAST)
			req.r.rtm_table = RT_TABLE_LOCAL;
	}
	if (!scope_ok) {
		if (req.r.rtm_type == RTN_LOCAL ||
		    req.r.rtm_type == RTN_NAT)
			req.r.rtm_scope = RT_SCOPE_HOST;
		else if (req.r.rtm_type == RTN_BROADCAST ||
			 req.r.rtm_type == RTN_MULTICAST ||
			 req.r.rtm_type == RTN_ANYCAST)
			req.r.rtm_scope = RT_SCOPE_LINK;
		else if (req.r.rtm_type == RTN_UNICAST ||
			 req.r.rtm_type == RTN_UNSPEC) {
			if (cmd == RTM_DELROUTE)
				req.r.rtm_scope = RT_SCOPE_NOWHERE;
			else if (!gw_ok && !nhs_ok)
				req.r.rtm_scope = RT_SCOPE_LINK;
		}
	}

	if (req.r.rtm_family == AF_UNSPEC)
		req.r.rtm_family = AF_INET;

	if (rtnl_talk(&rth, &req.n, 0, 0, NULL, NULL, NULL) < 0)
    {
        rtnl_close(&rth);
		return -1;
    }
    
    rtnl_close(&rth);

	return 0;
}


static int do_iproute(int argc, char **argv)
{	
	if (matches(*argv, "add") == 0)
		return iproute_modify(RTM_NEWROUTE, NLM_F_CREATE|NLM_F_EXCL,
				      argc-1, argv+1);

	if (matches(*argv, "delete") == 0)
		return iproute_modify(RTM_DELROUTE, 0,
				      argc-1, argv+1);
    
    return -1;
}

///////////////////////////// rule operation /////////////////////////////
static int iprule_modify(int cmd, int argc, char **argv)
{
	int table_ok = 0;
	struct rtnl_handle rth;
	struct {
		struct nlmsghdr 	n;
		struct rtmsg 		r;
		char   			buf[1024];
	} req;

	memset(&req, 0, sizeof(req));

	req.n.nlmsg_type = cmd;
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.r.rtm_family = AF_UNSPEC;
	req.r.rtm_protocol = RTPROT_BOOT;
	req.r.rtm_scope = RT_SCOPE_UNIVERSE;
	req.r.rtm_table = 0;
	req.r.rtm_type = RTN_UNSPEC;

	if (cmd == RTM_NEWRULE) {
		req.n.nlmsg_flags |= NLM_F_CREATE|NLM_F_EXCL;
		req.r.rtm_type = RTN_UNICAST;
	}

	while (argc > 0) {
		if (strcmp(*argv, "from") == 0) {
			inet_prefix dst;
			NEXT_ARG();
			get_prefix(&dst, *argv, req.r.rtm_family);
			req.r.rtm_src_len = dst.bitlen;
			addattr_l(&req.n, sizeof(req), RTA_SRC, &dst.data, dst.bytelen);
		} else if (matches(*argv, "table") == 0 ||
			   strcmp(*argv, "lookup") == 0) {
			int tid;
			NEXT_ARG();
			if (rtnl_rttable_a2n((__u32 *)&tid, *argv))
				fprintf(stderr, "invalid table ID\n");
			req.r.rtm_table = tid;
			table_ok = 1;
		}
		
		argc--;
		argv++;
	}

	if (req.r.rtm_family == AF_UNSPEC)
		req.r.rtm_family = AF_INET;

	if (!table_ok && cmd == RTM_NEWRULE)
		req.r.rtm_table = RT_TABLE_MAIN;

	if (rtnl_open(&rth, 0) < 0)
		return -1;

	if (rtnl_talk(&rth, &req.n, 0, 0, NULL, NULL, NULL) < 0)
    {
	    rtnl_close(&rth);
		return -1;
    }
    
    rtnl_close(&rth);

	return 0;
}


static int do_iprule(int argc, char **argv)
{
    if (matches(argv[0], "add") == 0) {
		return iprule_modify(RTM_NEWRULE, argc-1, argv+1);
	} else if (matches(argv[0], "delete") == 0) {
		return iprule_modify(RTM_DELRULE, argc-1, argv+1);
	}

	fprintf(stderr, "Command \"%s\" is unknown, try \"ip rule help\".\n", *argv);
	return -1;
}

int advrt_do_route_cmd(char *cmd)
{
    char *token = NULL;
    char *cmdbuf[64] = { 0 };
    char cmdTmp[256] = { 0 };
    int cmdargc = 0;
    int i;
    int ret = 0;
    
    for (i = 0; i < 64; i++)
    {
        cmdbuf[i] = malloc(64);
        if (NULL == cmdbuf[i])
        {
            return -1;
        }
        memset(cmdbuf[i], 0, 64);
    }
    
    strcpy(cmdTmp, cmd);

    token = strtok(cmdTmp, " ");
    while (NULL != token)
    {
        
        if (NULL != token)
        {   
            strcpy(cmdbuf[cmdargc], token);
            cmdargc ++;
        }
        token = strtok(NULL, " ");
    }
    
    ret = do_iproute(cmdargc, cmdbuf);

//FREE_AND_RETURN:
    for (i = 0; i < 64; i++)
    {
        free(cmdbuf[i]);
    }
    
    for (i = 0; i < 16; i ++)
    {
        if (idxmap[i] != NULL)
        {
            free(idxmap[i]);
            idxmap[i] = NULL;
        }
    }
    
    rtnl_rttable_init = 0;
    
    return ret;
}

int advrt_do_rule_cmd(char *cmd)
{
    char *token = NULL;
    char *cmdbuf[64] = { 0 };
    char cmdTmp[256] = { 0 };
    int cmdargc = 0;
    int i;
    int ret = 0;
    
    for (i = 0; i < 64; i++)
    {
        cmdbuf[i] = malloc(64);
        if (NULL == cmdbuf[i])
        {
            return -1;
        }
        memset(cmdbuf[i], 0, 64);
    }
    
    strcpy(cmdTmp, cmd);

    token = strtok(cmdTmp, " ");
    while (NULL != token)
    {
        if (NULL != token)
        {   
            strcpy(cmdbuf[cmdargc], token);
            cmdargc ++;
        }
        token = strtok(NULL, " ");
    }
    
    ret = do_iprule(cmdargc, cmdbuf);

//FREE_AND_RETURN:
    for (i = 0; i < 64; i++)
    {
        free(cmdbuf[i]);
    }
    
    for (i = 0; i < 16; i ++)
    {
        if (idxmap[i] != NULL)
        {
            free(idxmap[i]);
            idxmap[i] = NULL;
        }
    }
    
    rtnl_rttable_init = 0;
    
    return ret;
}

#if 0
int main(int argc, char **argv)
{
    int ret = 0;
    int i = 0;
    char tmpBuf[100] = { 0 };
    
    if (0 == strcmp(argv[1], "route"))
    {
        printf("the argv2 is %s\r\n", argv[2]); 
          
        ret = advrt_do_route_cmd(argv[2]);
        if (ret < 0)
        {
            printf("configure route failed\r\n");
            exit(-1);
        }
        printf("####do route succeed\r\n");            
    }
    else if (0 == strcmp(argv[1], "rule"))
    {
        printf("the argv2 is %s\r\n", argv[2]);
        
        ret = advrt_do_rule_cmd(argv[2]);
        if (ret < 0)
        {
            printf("configure route failed\r\n");
            exit(-1);
        }
        printf("####do rule succeed\r\n");      
    } 
    
    printf("config succeed\r\n");
    exit(0);
}
#endif

