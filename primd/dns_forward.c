/*
 * Copyright (c) 2010-2012 Satoshi Ebisawa. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. The names of its contributors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "dns.h"
#include "dns_cache.h"
#include "dns_engine.h"
#include "dns_forward.h"

#define MODULE "forward"

#define MAX_FORWARDS       4
#define FORWARD_PORT_MIN   1024

typedef struct {
    unsigned        stat_queries;
    unsigned        stat_forwarded;
    unsigned        stat_timeout;
    unsigned        stat_failover_failure;
    unsigned        stat_recieved;
} forward_stats_t;

typedef struct {
    struct sockaddr_storage  conf_addr[MAX_FORWARDS];
    int conf_addr_cnt;
    int conf_timeout;
} forward_config_t;

static int forward_setarg(dns_engine_param_t *ep, char *arg);
static int forward_query(dns_engine_param_t *ep, dns_cache_rrset_t *rrset, dns_msg_question_t *q, dns_tls_t *tls);
static int forward_connect(struct sockaddr *to, dns_tls_t *tls);
static int forward_socket(struct sockaddr *to, dns_tls_t *tls);
static int forward_send(int s, dns_msg_question_t *q, uint16_t msgid);
static int forward_udp_receive(dns_cache_rrset_t *rrset, dns_msg_question_t *q, int s, uint16_t msgid, dns_tls_t *tls);
static int forward_msg_parse(dns_cache_rrset_t *rrset, dns_msg_question_t *q, char *buf, int len, uint16_t msgid, dns_tls_t *tls);
static int forward_msg_parse_resource(dns_cache_rrset_t *rrset, dns_msg_question_t *q, dns_msg_handle_t *handle, int count, dns_tls_t *tls);
static int forward_msg_parse_resource_soa(dns_cache_rrset_t *rrset, dns_msg_handle_t *handle, int count);
static int forward_validate_header(dns_header_t *header, uint16_t expid);

static forward_stats_t ForwardStats;

dns_engine_t ForwardEngine = {
    "forward", sizeof(forward_config_t),
    forward_setarg,
    NULL,  /* init */
    NULL,  /* destroy */
    forward_query,
    NULL,  /* notify */
    NULL,  /* dump */
};

void
dns_forward_printstats(int s)
{
    dns_util_sendf(s, "Forward:\n");
    dns_util_sendf(s, "    %10u queries requested\n",    ForwardStats.stat_queries);
    dns_util_sendf(s, "    %10u forwarded request\n",    ForwardStats.stat_forwarded);
    dns_util_sendf(s, "    %10u timeout response\n",     ForwardStats.stat_timeout);
    dns_util_sendf(s, "    %10u failover failure\n",     ForwardStats.stat_failover_failure);
    dns_util_sendf(s, "    %10u recieved response\n",    ForwardStats.stat_recieved);
    dns_util_sendf(s, "\n");
}

static int
forward_setarg(dns_engine_param_t *ep, char *arg)
{
    char *larg;
    char *sptr, *eptr;
    struct sockaddr_storage ss;
    forward_config_t *conf = (forward_config_t *) ep->ep_conf;

    larg = sptr = strdup(arg);
    if (larg == NULL) {
        return -1;
    }

    // timeout
    conf->conf_timeout = DNS_ENGINE_TIMEOUT;
    eptr = strchr(sptr, ' ');
    if (eptr != NULL) {
        *eptr = '\0';
        conf->conf_timeout = atoi(sptr);
        if (conf->conf_timeout <= 0) {
	    conf->conf_timeout = DNS_ENGINE_TIMEOUT;
	}
        sptr = eptr + 1;
    }

    // addrs
    conf->conf_addr_cnt = 0;
    while (conf->conf_addr_cnt < MAX_FORWARDS) {
        eptr = strchr(sptr, ',');
        if (eptr == NULL) {
            break;
        }
        *eptr = '\0';
        if (dns_util_str2sa((SA *) &ss, sptr, DNS_PORT) < 0) {
            free(larg);
            return -1;
        }
        memcpy(&conf->conf_addr[conf->conf_addr_cnt], &ss, sizeof(conf->conf_addr[0]));
        conf->conf_addr_cnt++;
        sptr = eptr + 1;
    }
    if (conf->conf_addr_cnt < MAX_FORWARDS) {
        if (dns_util_str2sa((SA *) &ss, sptr, DNS_PORT) < 0) {
            free(larg);
            return -1;
        }
        memcpy(&conf->conf_addr[conf->conf_addr_cnt], &ss, sizeof(conf->conf_addr[0]));
        conf->conf_addr_cnt++;
    }
    free(larg);

    return 0;
}

static int
forward_query(dns_engine_param_t *ep, dns_cache_rrset_t *rrset, dns_msg_question_t *q, dns_tls_t *tls)
{
    int i;
    int s;
    uint16_t msgid;
    struct sockaddr *to;
    forward_config_t *conf = (forward_config_t *) ep->ep_conf;

    ATOMIC_INC(&ForwardStats.stat_queries);

    for (i = 0; i < conf->conf_addr_cnt; i++) {
        msgid = xarc4random(&tls->tls_arctx);
        to = (SA *) &conf->conf_addr[i];

        if ((s = forward_connect(to, tls)) < 0)
            return -1;

        if (forward_send(s, q, msgid) < 0) {
            close(s);
            return -1;
        }

        ATOMIC_INC(&ForwardStats.stat_forwarded);

        if (dns_util_select(s, conf->conf_timeout) < 0) {
            ATOMIC_INC(&ForwardStats.stat_timeout);
            plog(LOG_WARNING, "%s: forward query timed out: %s", MODULE, q->mq_name);
            close(s);
            continue;
        }

        if (forward_udp_receive(rrset, q, s, msgid, tls) < 0) {
            plog(LOG_ERR, "%s: receiving response failed: %s", MODULE, q->mq_name);
            close(s);
            if (errno == ECONNREFUSED) {
                continue;
            }
            return -1;
        }

        ATOMIC_INC(&ForwardStats.stat_recieved);
        close(s);

        return 0;
    }

    ATOMIC_INC(&ForwardStats.stat_failover_failure);
    plog(LOG_ERR, "%s: failover failure: %s", MODULE, q->mq_name);

    return -1;
}

static int
forward_connect(struct sockaddr *to, dns_tls_t *tls)
{
    int sock;

    if ((sock = forward_socket(to, tls)) < 0)
        return -1;

    if (connect(sock, to, SALEN(to)) < 0) {
        plog_error(LOG_ERR, MODULE, "connect() failed");
        close(sock);
        return -1;
    }

    return sock;
}

static int
forward_socket(struct sockaddr *to, dns_tls_t *tls)
{
    int i, s, port;
    uint16_t r;

    /* try 20 times */
    for (i = 0; i < 20; i++) {
        r = xarc4random(&tls->tls_arctx);
        port = FORWARD_PORT_MIN + (r & 0xf000);

        if ((s = dns_util_socket(PF_INET, SOCK_DGRAM, port)) > 0) {
            plog(LOG_DEBUG, "%s: src port = %u", __func__, port);
            return s;
        }
    }
    plog(LOG_ERR, "%s: could't create forward socket", __func__);

    return -1;
}

static int
forward_send(int s, dns_msg_question_t *q, uint16_t msgid)
{
    int len;
    char buf[DNS_UDP_MSG_MAX];
    dns_msg_handle_t handle;

    if (dns_msg_write_open(&handle, buf, sizeof(buf)) < 0) {
        plog(LOG_ERR, "%s: dns_msg_write_open() failed", __func__);
        return -1;
    }

    if (dns_msg_write_header(&handle, msgid, DNS_FLAG_RD) < 0) {
        plog(LOG_ERR, "%s: dns_msg_write_header() failed", __func__);
        dns_msg_write_close(&handle);
        return -1;
    }

    if (dns_msg_write_question(&handle, q) < 0) {
        plog(LOG_ERR, "%s: dns_msg_write_question() failed", __func__);
        dns_msg_write_close(&handle);
        return -1;
    }

    if ((len = dns_msg_write_close(&handle)) < 0) {
        plog(LOG_ERR, "%s: dns_msg_write_close() failed", __func__);
        return -1;
    }

    return send(s, buf, len, 0);
}

static int
forward_udp_receive(dns_cache_rrset_t *rrset, dns_msg_question_t *q, int s, uint16_t msgid, dns_tls_t *tls)
{
    int len;
    char buf[DNS_UDP_MSG_MAX];
    socklen_t fromlen;
    struct sockaddr_storage from;

    errno = 0;
    fromlen = sizeof(from);
    if ((len = recvfrom(s, buf, sizeof(buf), 0,
                        (SA *) &from, &fromlen)) < 0) {
        plog_error(LOG_ERR, MODULE, "recvfrom() failed");
        return -1;
    }

    return forward_msg_parse(rrset, q, buf, len, msgid, tls);
}

static int
forward_msg_parse(dns_cache_rrset_t *rrset, dns_msg_question_t *q, char *buf, int len, uint16_t msgid, dns_tls_t *tls)
{
    int count, rcode;
    uint16_t flags;
    dns_header_t header;
    dns_msg_handle_t handle;
    dns_msg_question_t question;

    if (dns_msg_read_open(&handle, buf, len) < 0) {
        plog(LOG_NOTICE, "%s: open message failed. broken message?", MODULE);
        return -1;
    }

    if (dns_msg_read_header(&header, &handle) < 0) {
        plog(LOG_NOTICE, "%s: read header failed. broken message?", MODULE);
        dns_msg_read_close(&handle);
        return -1;
    }

    /* check result code */
    flags = ntohs(header.hdr_flags);
    rcode = DNS_RCODE(flags);

    if (flags & DNS_FLAG_TC) {
        /* XXX fallback to TCP */
        plog(LOG_NOTICE, "%s: XXX truncated message is not supported", MODULE);
        dns_msg_read_close(&handle);
        return -1;
    }

    if (forward_validate_header(&header, msgid) < 0) {
        dns_msg_read_close(&handle);
        return -1;
    }

    if (dns_msg_read_question(&question, &handle) < 0) {
        dns_msg_read_close(&handle);
        return -1;
    }

    /*
      XXX validate question
    */

    /*
     * resources in authority and addtioal sections should not be cached
     * because these RRs may not be entire of RRset. Partial RRsets cannot
     * be used for answer. (RFC2181 5.)
     */
    count = ntohs(header.hdr_ancount);
    if (forward_msg_parse_resource(rrset, q, &handle, count, tls) < 0) {
        dns_msg_read_close(&handle);
        return -1;
    }

    if (count == 0) {
        /* cache SOA for negative caching */
        count = ntohs(header.hdr_nscount);
        if (forward_msg_parse_resource_soa(rrset, &handle, count) < 0) {
            dns_msg_read_close(&handle);
            return -1;
        }
    }

    dns_msg_read_close(&handle);
    dns_cache_set_rcode(rrset, rcode);

    return 0;
}

static int
forward_msg_parse_resource(dns_cache_rrset_t *rrset, dns_msg_question_t *q, dns_msg_handle_t *handle, int count, dns_tls_t *tls)
{
    int i;
    dns_msg_resource_t res;

    for (i = 0; i < count; i++) {
        if (dns_msg_read_resource(&res, handle) < 0) {
            plog(LOG_NOTICE, "%s: read resource failed. broken message?", MODULE);
            return -1;
        }

        /* XXX validate resource */

        if (dns_cache_add_answer(rrset, q, &res, tls) < 0) {
            plog(LOG_ERR, "%s: can't add cache resource", MODULE);
            return -1;
        }
    }

    return 0;
}

static int
forward_msg_parse_resource_soa(dns_cache_rrset_t *rrset, dns_msg_handle_t *handle, int count)
{
    int i;
    uint32_t ttl;
    dns_msg_resource_t res;

    for (i = 0; i < count; i++) {
        if (dns_msg_read_resource(&res, handle) < 0) {
            plog(LOG_NOTICE, "%s: read resource failed. broken message?", MODULE);
            return -1;
        }

        /* XXX validate resource */

        if (res.mr_q.mq_type == DNS_TYPE_SOA) {
            if (dns_msg_parse_soa(NULL, NULL, NULL, NULL, NULL, NULL, &ttl, &res) < 0) {
                plog(LOG_NOTICE, "%s: Can't get minimum ttl from SOA record", MODULE);
                ttl = 0;
            }

            dns_cache_negative(rrset, ttl);
        }
    }

    return 0;
}

static int
forward_validate_header(dns_header_t *header, uint16_t expid)
{
    uint16_t msgid, flags;

    msgid = ntohs(header->hdr_id);
    flags = ntohs(header->hdr_flags);

    if (msgid != expid) {
        plog(LOG_NOTICE, "%s: message id mismatch", MODULE);
        return -1;
    }

    if ((flags & DNS_FLAG_QR) == 0) {
        plog(LOG_NOTICE, "%s: message is not a response", MODULE);
        return -1;
    }

    if (ntohs(header->hdr_qdcount) > 1) {
        plog(LOG_NOTICE, "%s: qdcount > 1", MODULE);
        return -1;
    }

    return 0;
}
