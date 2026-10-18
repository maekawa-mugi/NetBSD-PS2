/* SPDX-License-Identifier: BSD-2-Clause */
/* Test helper for the isolated PCSX2 Sockets network (192.0.2.0/24). */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
fail(const char *what)
{
	perror(what);
	exit(1);
}

static void
address(struct sockaddr *p, const char *text)
{
	struct sockaddr_in *s = (void *)p;
	memset(s, 0, sizeof(*s));
	s->sin_len = sizeof(*s);
	s->sin_family = AF_INET;
	if (inet_pton(AF_INET, text, &s->sin_addr) != 1)
		exit(1);
}

static void
interface(unsigned char mac[6])
{
	struct ifaddrs *list;
	if (getifaddrs(&list) < 0)
		fail("getifaddrs");
	int found = 0;
	for (struct ifaddrs *a = list; a; a = a->ifa_next) {
		if (strcmp(a->ifa_name, "smap0") || !a->ifa_addr ||
		    a->ifa_addr->sa_family != AF_LINK)
			continue;
		struct sockaddr_dl *s = (void *)a->ifa_addr;
		if (s->sdl_alen != 6)
			continue;
		memcpy(mac, LLADDR(s), 6);
		struct if_data *d = a->ifa_data;
		if (d)
			printf("SMAP stats: tx=%llu rx=%llu errors=%llu/%llu\n",
			    (unsigned long long)d->ifi_opackets,
			    (unsigned long long)d->ifi_ipackets,
			    (unsigned long long)d->ifi_oerrors,
			    (unsigned long long)d->ifi_ierrors);
		found = 1;
	}
	freeifaddrs(list);
	if (!found) {
		fprintf(stderr, "smap0 MAC unavailable\n");
		exit(1);
	}
}

static unsigned char *
option(unsigned char *p, size_t n, unsigned tag, unsigned length)
{
	for (size_t i = 240; i < n;) {
		unsigned t = p[i++];
		if (t == 255)
			break;
		if (t == 0)
			continue;
		if (i == n)
			break;
		unsigned len = p[i++];
		if (len > n - i)
			break;
		if (t == tag && len == length)
			return &p[i];
		i += len;
	}
	return NULL;
}

static void
dhcp(int fd, const unsigned char mac[6])
{
	unsigned char request[300] = {0}, reply[1500];
	unsigned char offered[4], server[4];
	struct sockaddr_in peer = {.sin_len = sizeof(peer), .sin_family = AF_INET,
	    .sin_port = htons(67)};
	(void)inet_pton(AF_INET, "192.0.2.1", &peer.sin_addr);
	request[0] = 1; request[1] = 1; request[2] = 6;
	uint32_t xid = htonl(0x4e425344U ^ (unsigned)getpid());
	memcpy(request + 4, &xid, 4);
	request[10] = 0x80; /* Broadcast reply requested. */
	memcpy(request + 28, mac, 6);
	memcpy(request + 236, "\x63\x82\x53\x63", 4);
	for (unsigned phase = 0; phase < 2; phase++) {
		unsigned pos = 240;
		request[pos++] = 53; request[pos++] = 1;
		request[pos++] = phase ? 3 : 1; /* REQUEST / DISCOVER */
		if (phase) {
			request[pos++] = 50; request[pos++] = 4;
			memcpy(request + pos, offered, 4); pos += 4;
			request[pos++] = 54; request[pos++] = 4;
			memcpy(request + pos, server, 4); pos += 4;
		}
		request[pos] = 255;
		if (sendto(fd, request, sizeof(request), 0,
		    (void *)&peer, sizeof(peer)) != sizeof(request))
			fail("DHCP send");
		ssize_t n = recv(fd, reply, sizeof(reply), 0);
		if (n < 0)
			fail("DHCP receive");
		unsigned char *type = n >= 240 ? option(reply, n, 53, 1) : NULL;
		if (!type || *type != (phase ? 5 : 2) || reply[0] != 2 ||
		    reply[1] != 1 || reply[2] != 6 || memcmp(reply + 4, &xid, 4) ||
		    memcmp(reply + 28, mac, 6) ||
		    memcmp(reply + 236, request + 236, 4)) {
			fprintf(stderr, "invalid DHCP response (%zd bytes)\n", n);
			exit(1);
		}
		unsigned char *sid = option(reply, n, 54, 4);
		if (!sid || (phase && (memcmp(reply + 16, offered, 4) ||
		    memcmp(sid, server, 4))))
			exit(1);
		memcpy(offered, reply + 16, 4);
		memcpy(server, sid, 4);
		printf("DHCP %s: %u.%u.%u.%u (%zd bytes, matching xid/MAC)\n",
		    phase ? "ACK" : "OFFER", offered[0], offered[1], offered[2], offered[3], n);
	}
}

static void
echo_test(const char *host)
{
	/* Install only a host route through the isolated Sockets gateway. */
	struct {
		struct rt_msghdr h;
		struct sockaddr_in dst, gateway, mask;
	} route = {0};
	route.h.rtm_msglen = sizeof(route);
	route.h.rtm_version = RTM_VERSION;
	route.h.rtm_type = RTM_ADD;
	route.h.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_HOST | RTF_STATIC;
	route.h.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	route.h.rtm_seq = 1;
	route.h.rtm_pid = getpid();
	address((void *)&route.dst, host);
	address((void *)&route.gateway, "192.0.2.1");
	address((void *)&route.mask, "255.255.255.255");
	int r = socket(PF_ROUTE, SOCK_RAW, 0);
	if (r < 0 || write(r, &route, sizeof(route)) != sizeof(route))
		fail("host route");
	close(r);
	unsigned char data[1400], reply[1400];
	for (unsigned i = 0; i < sizeof(data); i++)
		data[i] = (i * 37 + 19) & 255;
	struct sockaddr_in peer;
	address((void *)&peer, host);
	struct timeval timeout = {.tv_sec = 5};
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0)
		fail("UDP socket");
	peer.sin_port = htons(38473);
	if (connect(fd, (void *)&peer, sizeof(peer)) < 0)
		fail("UDP connect");
	for (unsigned i = 0; i < 96; i++) {
		size_t n = i % 3 == 0 ? 64 : i % 3 == 1 ? 512 : sizeof(data);
		data[0] = i;
		if (write(fd, data, n) != (ssize_t)n)
			fail("UDP write");
		ssize_t got = read(fd, reply, sizeof(reply));
		if (got != (ssize_t)n || memcmp(data, reply, n)) {
			fprintf(stderr, "UDP echo mismatch iteration=%u got=%zd expected=%zu\n", i, got, n);
			exit(1);
		}
	}
	close(fd);
	puts("SMAP UDP echo: 96 packets, 64/512/1400 bytes, payloads OK");
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
	    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0)
		fail("TCP socket");
	peer.sin_port = htons(38474);
	if (connect(fd, (void *)&peer, sizeof(peer)) < 0)
		fail("TCP connect");
	for (unsigned i = 0; i < 32; i++) {
		data[0] = i;
		for (size_t sent = 0; sent < sizeof(data);) {
			ssize_t n = write(fd, data + sent, sizeof(data) - sent);
			if (n <= 0)
				fail("TCP write");
			sent += n;
		}
		for (size_t got = 0; got < sizeof(reply);) {
			ssize_t n = read(fd, reply + got, sizeof(reply) - got);
			if (n <= 0)
				fail("TCP read");
			got += n;
		}
		if (memcmp(data, reply, sizeof(data))) {
			fprintf(stderr, "TCP payload mismatch\n");
			exit(1);
		}
	}
	close(fd);
	puts("SMAP TCP echo: 44800 bytes, payloads OK");
}

int
main(int argc, char **argv)
{
	unsigned char mac[6];
	interface(mac);
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		fail("socket");
	struct ifaliasreq a = {0};
	strcpy(a.ifra_name, "smap0");
	address(&a.ifra_addr, "192.0.2.100");
	address(&a.ifra_mask, "255.255.255.0");
	address(&a.ifra_broadaddr, "192.0.2.255");
	if (ioctl(fd, SIOCAIFADDR, &a) < 0)
		fail("SIOCAIFADDR");
	struct ifreq flags = {0};
	strcpy(flags.ifr_name, "smap0");
	if (ioctl(fd, SIOCGIFFLAGS, &flags) < 0)
		fail("SIOCGIFFLAGS");
	flags.ifr_flags |= IFF_UP;
	if (ioctl(fd, SIOCSIFFLAGS, &flags) < 0)
		fail("SIOCSIFFLAGS");
	struct timeval timeout = {.tv_sec = 5};
	int on = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
	    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on)) < 0)
		fail("setsockopt");
	struct sockaddr_in local = {.sin_len = sizeof(local), .sin_family = AF_INET,
	    .sin_port = htons(68)};
	if (bind(fd, (void *)&local, sizeof(local)) < 0)
		fail("bind");
	dhcp(fd, mac);
	close(fd);
	if (argc == 2)
		echo_test(argv[1]);
	interface(mac);
	puts("SMAP ARP/IPv4/UDP DHCP round-trip OK (PCSX2 Sockets test subnet)");
	return 0;
}
