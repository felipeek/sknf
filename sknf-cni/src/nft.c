#include "nft.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/netfilter/nfnetlink.h>
#include <libmnl/libmnl.h>
#include <libnftnl/chain.h>
#include <libnftnl/expr.h>
#include <libnftnl/rule.h>
#include <libnftnl/table.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/nf_tables.h>

#include "err.h"
#include "util.h"

#define SKNF_NFTABLES_TABLE_NAME "sknf"
#define SKNF_NFTABLES_POSTROUTING_CHAIN_NAME "POSTROUTING"

// iptables -t nat -A POSTROUTING -s <podCIDR> -o eth0 -j MASQUERADE
int nft_nat_rule(Err* err, const char* ifname, const char* cidr) {
	struct in_addr addr;
	int prefix;

	int rc = 1; // assume failure
	struct mnl_socket* sk = NULL;
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct mnl_nlmsg_batch* batch = NULL;
	int batch_stopped = 0; // track whether we called mnl_nlmsg_batch_stop
	uint32_t seq = 0;

	if (util_cidr_parse(err, cidr, &addr, &prefix)) {
		fprintf(stderr, "unable to parse CIDR %s\n", cidr);
		return 1;
	}
	uint32_t mask = (prefix == 0) ? 0 : htonl(0xFFFFFFFFu << (32 - prefix));
	uint32_t net_be = addr.s_addr;

	sk = mnl_socket_open(NETLINK_NETFILTER);
	if (!sk) {
		fprintf(stderr, "failure opening mnl_socket: %s\n", strerror(errno));
		ERRF(err, "Failure opening mnl_socket", "%s", strerror(errno));
		goto out;
	}

	if (mnl_socket_bind(sk, 0, MNL_SOCKET_AUTOPID) < 0) {
		fprintf(stderr, "failure binding to mnl_socket: %s\n", strerror(errno));
		ERRF(err, "Failure binding to mnl_socket", "%s", strerror(errno));
		goto out;
	}

	int portid = mnl_socket_get_portid(sk);

	batch = mnl_nlmsg_batch_start(buf, sizeof(buf));
	nftnl_batch_begin(mnl_nlmsg_batch_current(batch), ++seq);
	mnl_nlmsg_batch_next(batch);

	// table
	struct nftnl_table* t = nftnl_table_alloc();
	if (!t) {
		fprintf(stderr, "failure allocating nftnl_table\n");
		ERR(err, "Failure allocating nftnl_table");
		goto out;
	}
	nftnl_table_set_str(t, NFTNL_TABLE_NAME, SKNF_NFTABLES_TABLE_NAME);
	nftnl_table_set_u32(t, NFTNL_TABLE_FAMILY, NFPROTO_IPV4);

	struct nlmsghdr* nlh = nftnl_table_nlmsg_build_hdr(
		mnl_nlmsg_batch_current(batch), NFT_MSG_NEWTABLE, NFPROTO_IPV4,
		NLM_F_CREATE | NLM_F_ACK, ++seq
	);
	int table_seq = seq;
	nftnl_table_nlmsg_build_payload(nlh, t);
	mnl_nlmsg_batch_next(batch);
	nftnl_table_free(t);

	// chain
	struct nftnl_chain* c = nftnl_chain_alloc();
	if (!c) {
		fprintf(stderr, "failure allocating nftnl_chain\n");
		ERR(err, "Failure allocating nftnl_chain");
		goto out;
	}
	nftnl_chain_set_str(c, NFTNL_CHAIN_TABLE, SKNF_NFTABLES_TABLE_NAME);
	nftnl_chain_set_str(c, NFTNL_CHAIN_NAME, SKNF_NFTABLES_POSTROUTING_CHAIN_NAME);
	nftnl_chain_set_str(c, NFTNL_CHAIN_TYPE, "nat");
	nftnl_chain_set_u32(c, NFTNL_CHAIN_HOOKNUM, NF_INET_POST_ROUTING);
	nftnl_chain_set_s32(c, NFTNL_CHAIN_PRIO, NF_IP_PRI_NAT_SRC);

	nlh = nftnl_chain_nlmsg_build_hdr(
		mnl_nlmsg_batch_current(batch), NFT_MSG_NEWCHAIN, NFPROTO_IPV4,
		NLM_F_CREATE | NLM_F_ACK, ++seq
	);
	nftnl_chain_nlmsg_build_payload(nlh, c);
	mnl_nlmsg_batch_next(batch);
	nftnl_chain_free(c);

	// rule
	struct nftnl_rule* r = nftnl_rule_alloc();
	if (!r) {
		fprintf(stderr, "failure allocating nftnl_rule\n");
		ERR(err, "Failure allocating nftnl_rule");
		goto out;
	}
	nftnl_rule_set_str(r, NFTNL_RULE_TABLE, SKNF_NFTABLES_TABLE_NAME);
	nftnl_rule_set_str(r, NFTNL_RULE_CHAIN, SKNF_NFTABLES_POSTROUTING_CHAIN_NAME);

	// meta oifname -> reg1 ; cmp reg1 == "<ifname>"
	{
		struct nftnl_expr *e_meta = nftnl_expr_alloc("meta");
		nftnl_expr_set_u32(e_meta, NFTNL_EXPR_META_KEY, NFT_META_OIFNAME);
		nftnl_expr_set_u32(e_meta, NFTNL_EXPR_META_DREG, NFT_REG_1);
		nftnl_rule_add_expr(r, e_meta);

		struct nftnl_expr *e_cmp = nftnl_expr_alloc("cmp");
		nftnl_expr_set_u32(e_cmp, NFTNL_EXPR_CMP_SREG, NFT_REG_1);
		nftnl_expr_set_u32(e_cmp, NFTNL_EXPR_CMP_OP, NFT_CMP_EQ);
		nftnl_expr_set_str(e_cmp, NFTNL_EXPR_CMP_DATA, ifname);
		nftnl_rule_add_expr(r, e_cmp);
	}

	// payload load saddr -> reg1
	{
		struct nftnl_expr *e = nftnl_expr_alloc("payload");
		nftnl_expr_set_u32(e, NFTNL_EXPR_PAYLOAD_BASE, NFT_PAYLOAD_NETWORK_HEADER);
		nftnl_expr_set_u32(e, NFTNL_EXPR_PAYLOAD_OFFSET, 12);
		nftnl_expr_set_u32(e, NFTNL_EXPR_PAYLOAD_LEN, 4);
		nftnl_expr_set_u32(e, NFTNL_EXPR_PAYLOAD_DREG, NFT_REG_1);
		nftnl_rule_add_expr(r, e);
	}

	// bitwise AND with mask
	{
		uint32_t zero = 0;
		struct nftnl_expr *e = nftnl_expr_alloc("bitwise");
		nftnl_expr_set_u32(e, NFTNL_EXPR_BITWISE_SREG, NFT_REG_1);
		nftnl_expr_set_u32(e, NFTNL_EXPR_BITWISE_DREG, NFT_REG_1);
		nftnl_expr_set_u32(e, NFTNL_EXPR_BITWISE_LEN, 4);
		nftnl_expr_set_data(e, NFTNL_EXPR_BITWISE_MASK, &mask, sizeof(mask));
		nftnl_expr_set_data(e, NFTNL_EXPR_BITWISE_XOR, &zero, sizeof(zero));
		nftnl_rule_add_expr(r, e);
	}

	// compare reg1 == net&mask
	{
		uint32_t net_and_mask = net_be & mask;
		struct nftnl_expr *e = nftnl_expr_alloc("cmp");
		nftnl_expr_set_u32(e, NFTNL_EXPR_CMP_SREG, NFT_REG_1);
		nftnl_expr_set_u32(e, NFTNL_EXPR_CMP_OP, NFT_CMP_EQ);
		nftnl_expr_set_data(e, NFTNL_EXPR_CMP_DATA, &net_and_mask, sizeof(net_and_mask));
		nftnl_rule_add_expr(r, e);
	}

	// action: masquerade
	{
		struct nftnl_expr *e = nftnl_expr_alloc("masq");
		nftnl_rule_add_expr(r, e);
	}

	nlh = nftnl_rule_nlmsg_build_hdr(
		mnl_nlmsg_batch_current(batch), NFT_MSG_NEWRULE, NFPROTO_IPV4,
		NLM_F_CREATE | NLM_F_ACK, ++seq
	);
	nftnl_rule_nlmsg_build_payload(nlh, r);
	mnl_nlmsg_batch_next(batch);
	nftnl_rule_free(r);

	nftnl_batch_end(mnl_nlmsg_batch_current(batch), ++seq);
	mnl_nlmsg_batch_next(batch);

	if (mnl_socket_sendto(sk, mnl_nlmsg_batch_head(batch), mnl_nlmsg_batch_size(batch)) < 0) {
		fprintf(stderr, "failure sending batch to configure nftables: %s\n", strerror(errno));
		ERRF(err, "Failure sending batch to configure nftables", "%s", strerror(errno));
		goto out;
	}

	mnl_nlmsg_batch_stop(batch);
	batch_stopped = 1;

	int ret = mnl_socket_recvfrom(sk, buf, sizeof(buf));
	while (ret > 0) {
		ret = mnl_cb_run(buf, ret, table_seq, portid, NULL, NULL);
		if (ret <= 0) break;
		ret = mnl_socket_recvfrom(sk, buf, sizeof(buf));
	}
	if (ret == -1) {
		fprintf(stderr, "received error when consuming nft acks: %s\n", strerror(errno));
		ERRF(err, "Received error when consuming nft acks", "%s", strerror(errno));
		goto out;
	}

	rc = 0;

out:
	if (batch && !batch_stopped) mnl_nlmsg_batch_stop(batch);
	if (sk) mnl_socket_close(sk);
	return rc;
}