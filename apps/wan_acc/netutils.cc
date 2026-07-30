#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "io.h"
#include "app.h"
#include "hatcp_compat.h"
#include <ff_api.h>
#include "utils.h"
#include "netutils.h"

// Add by delee
#ifndef SO_NOSIGPIPE
#define SO_NOSIGPIPE 0
#endif

int init_primary(int, uint32_t, uint16_t, uint32_t, uint16_t);
int init_replica(int, uint32_t, uint16_t, uint32_t, uint16_t, uint32_t, uint16_t);
int read_socket_internal(int fd, char *buf, size_t count, int arb);


int
init_linux_listener(int *ifd, uint32_t addr, uint16_t port)
{
        int fd;
        int opt = 1;
        struct sockaddr_in address;

        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
                return (-1);

        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
            &opt, sizeof(opt)) != 0) {
                close(fd);
                return (-1);
        }

        if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT,
            &opt, sizeof(opt)) != 0) {
                close(fd);
                return (-1);
        }

        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = addr;
        address.sin_port = port;

        if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
                close(fd);
                return (-1);
        }

        if (listen(fd, 32) != 0) {
                close(fd);
                return (-1);
        }

        if (ioctl(fd, FIONBIO, &opt) != 0) {
                close(fd);
                return (-1);
        }

        INFO("Linux socket listening to %s:%d..\n",
            inet_ntoa(address.sin_addr), ntohs(port));

        *ifd = fd;
        return (0);
}

int
accept_linux_socket(int fd)
{
        int tmpfd;
        int opt = 1;
        socklen_t len;
        struct sockaddr_in address;

        memset(&address, 0, sizeof(address));
        len = sizeof(address);

        tmpfd = accept(fd, (struct sockaddr *)&address, &len);
        if (tmpfd < 0)
                return (-1);

        if (ioctl(tmpfd, FIONBIO, &opt) != 0) {
                close(tmpfd);
                return (-1);
        }

        return (tmpfd);
}

int
connect_linux_socket(uint32_t addr, uint16_t port)
{
        int fd;
        int opt = 1;
        struct sockaddr_in address;

        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
                return (-1);

        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = addr;
        address.sin_port = port;

        if (connect(fd, (struct sockaddr *)&address,
            sizeof(address)) != 0) {
                close(fd);
                return (-1);
        }

        if (ioctl(fd, FIONBIO, &opt) != 0) {
                close(fd);
                return (-1);
        }

        return (fd);
}

ssize_t
read_linux_socket(int fd, void *buf, size_t count)
{
        return (read(fd, buf, count));
}

ssize_t
write_linux_socket(int fd, const void *buf, size_t count)
{
        return (write(fd, buf, count));
}

int
close_linux_socket(int fd)
{
        return (close(fd));
}

int init_socket(int *ifd)
{
	int error = 0, opt = 1, fd;

	/* Init socket */
	fd = ff_socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		APPERR("Failed to create socket.\n");
		exit(0);
	}

	
error = ff_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (error != 0) {
		SYSERR(error, "Failed to set SO_REUSEADDR.\n");
		exit(0);
	}

	error = ff_setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
	if (error != 0) {
		SYSERR(error, "Failed to set SO_REUSEPORT.\n");
		exit(0);
	}

#ifndef __linux__
	error = ff_setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
	if (error != 0) {
		SYSERR(error, "Failed to set SO_NOSIGPIPE.\n");
		exit(0);
	}
#endif

	*ifd = fd;

	return (error);
}

int 
init_socket_somig(int *ifd, int mode, uint32_t addr, uint16_t port, 
    int somig_mode, uint32_t mso_addr, uint16_t mso_port, 
    uint32_t rso_addr, uint16_t rso_port)
{
	int error = 0, fd;
	struct sockaddr_in address;

	error = init_socket(&fd);
	if (error) 
		return (error);

	/* [REPLICA] Using somig to init the socket first. */
	if (somig_mode == WANACC_SERVER_SMG_REPLICA) {
		DBG("Initializing replica..\n");
		error = init_replica(fd, addr, port, mso_addr, mso_port, 
			    rso_addr, rso_port);
		if (error != 0) {
			APPERR("Failed to init SOMIG - Replica.\n");
			exit(0);
		}
	}

	/* [BOTH] Init socket (app server) */
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = addr;
	address.sin_port = port;
	error = ff_bind(fd, (struct linux_sockaddr *)&address,
		    sizeof(address));
	if (error < 0) {
		SYSERR(error, "Failed to bind.\n");
	}

	/* [BOTH] Start listening */
	ff_listen(fd, 32);
/* [PRIMARY] Init somig */
	if (somig_mode == WANACC_SERVER_SMG_PRIMARY) {
		DBG("Initializing primary..\n");
		error = init_primary(fd, addr, port, mso_addr, mso_port);
		if (error != 0) {
			APPERR("Failed to init SOMIG - Primary.\n");
			exit(0);
		}
	}

	INFO("Listening to %s:%d..\n", inet_ntoa(address.sin_addr), ntohs(port));
	
	*ifd = fd;
	return (0);
}

int 
accept_socket(int fd)
{
	int tmpfd;
	socklen_t len;
	struct sockaddr_in address;

	tmpfd = ff_accept(fd, (struct linux_sockaddr *)&address, &len);
	if (tmpfd < 0) {
		return (-1);
	}

	
int opt = 1;
if (ff_ioctl(tmpfd, FIONBIO, &opt) != 0) {
ff_close(tmpfd);
return (-1);
}

return (tmpfd);
}

int 
bind_socket(int fd, uint32_t addr, uint16_t port)
{
	struct sockaddr_in address;

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = addr;
	address.sin_port = port;

	return (ff_bind(fd, (struct linux_sockaddr *)&address, sizeof(address)));
}

void
listen_socket(int fd)
{
	ff_listen(fd, 32);
}

int 
connect_socket(int fd, uint32_t addr, uint16_t port, int somig_mode, 
	    uint32_t mso_addr, uint16_t mso_port, 
	    uint32_t rso_addr, uint16_t rso_port)
{
	int err = 0;
	struct sockaddr_in sa;

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = addr;
	sa.sin_port = port;

#ifdef SOMIGRATION
	if (somig_mode != WANACC_SERVER_SMG_REPLICA)
#endif
	err = ff_connect(fd, (struct linux_sockaddr *)&sa, sizeof(struct sockaddr));

#ifdef SOMIGRATION
	if (err != 0) {
		return (err);
	}

	if (somig_mode == WANACC_SERVER_SMG_PRIMARY) {
		err = init_primary(fd, addr, port, mso_addr, mso_port);
		if (err != 0) {
			APPERR("Failed to init SOMIG - Primary.\n");
			exit(0);
		}
	} else if (somig_mode == WANACC_SERVER_SMG_REPLICA) {
		usleep(500000);
		err = init_replica(fd, addr, port, mso_addr, mso_port, 
		    rso_addr, rso_port);
		if (err != 0) {
			APPERR("Failed to init SOMIG - Replica.\n");
			exit(0);
		}

		err = ff_connect(fd, (struct linux_sockaddr *)&sa, sizeof(struct sockaddr));
	}
#endif

	if (err == 0) {
		int nonblock = 1;
		if (ff_ioctl(fd, FIONBIO, &nonblock) != 0) {
			SYSERR(errno, "Failed to set connected socket non-blocking.\n");
			ff_close(fd);
			return (-1);
		}
	}

	return (err);
}

int 
init_primary(int fd, uint32_t app_addr, uint16_t app_port, uint32_t addr, uint16_t port)
{
#ifdef SOMIGRATION
	int opt = 1, error = 0;
	struct sockaddr_in address;

	opt = SOMIG_PRIMARY;
	error = ff_setsockopt_freebsd(fd, FF_SOL_SOCKET, SO_MIG_ROLE, &opt, sizeof(opt));
	if (error) {
		SYSERR(error, "Failed to init primary role.\n");
		exit(0);
	}

#ifdef SMCP
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = app_addr;
	address.sin_port = app_port;

	if (ff_setsockopt_freebsd(fd, FF_SOL_SOCKET, SO_MIG_PREBIND, 
		(struct sockaddr*)&address, sizeof(address))) {
		perror("setsockopt - somigpostbind");
		return (-1);
	}
#endif

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = addr;
	address.sin_port = port;
	error = ff_setsockopt_freebsd(fd, FF_SOL_SOCKET, SO_MIG_BIND, 
	    (struct linux_sockaddr *)&address, sizeof(address));
	if (error) {
		SYSERR(error, "Failed to bind SOMIG.\n");
		exit(0);
	}

	opt = 1;
	error = ff_setsockopt_freebsd(fd, FF_SOL_SOCKET, SO_MIG_LISTEN, &opt, sizeof(opt));
	if (error) {
		SYSERR(error, "Failed to listen.\n");
		exit(0);
	}
#else
	APPERR("Current kernel does not have SOMIG support.\n");
	exit(0);
#endif
	return (0);
}

int
init_replica(int fd, uint32_t app_addr, uint16_t app_port, 
    uint32_t paddr, uint16_t pport, uint32_t raddr, uint16_t rport)
{
#ifdef SOMIGRATION
	int opt = 1, error = 0;
	struct sockaddr_in address;

	opt = SOMIG_REPLICA;
	error = ff_setsockopt_freebsd(fd, FF_SOL_SOCKET, SO_MIG_ROLE, &opt, sizeof(opt));
	if (error) {
		SYSERR(error, "Failed to init replica role.\n");
		exit(0);
	}

#ifdef SMCP
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = app_addr; 
	address.sin_port = app_port;

	if (ff_setsockopt_freebsd(fd, FF_SOL_SOCKET, SO_MIG_PREBIND, 
		(struct sockaddr*)&address, sizeof(address))) {
		perror("setsockopt - somigpostbind");
		return (-1);
	}
#endif

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = raddr;
#ifndef SMCP
	address.sin_port = rport;
#endif
	error = ff_setsockopt_freebsd(fd, FF_SOL_SOCKET, SO_MIG_BIND, 
	    (struct linux_sockaddr *)&address, sizeof(address));
	if (error) {
		SYSERR(error, "Failed to bind SOMIG.\n");
		exit(0);
	}

	opt = 1;
	error = ff_setsockopt_freebsd(fd, FF_SOL_SOCKET, SO_MIG_LISTEN,
	    (struct linux_sockaddr *)&opt, sizeof(opt));
	if (error) {
		SYSERR(error, "Failed to change into listening state.\n");
		exit(0);
	}

	address.sin_addr.s_addr = paddr;
#ifndef SMCP
	address.sin_port = pport;
#endif
	error = ff_setsockopt_freebsd(fd, FF_SOL_SOCKET, SO_MIG_CONNECT,
	    (struct linux_sockaddr *)&address, sizeof(address));
	if (error) {
		SYSERR(error, "Failed to connect to primary.\n");
		exit(0);
	}
#else
	APPERR("Current kernel does not have SOMIG support.\n");
	exit(0);
#endif
	return (0);
}

int
readonce_socket(int fd, char *buf, size_t count)
{
	return read_socket_internal(fd, buf, count, 1);
}

int 
read_socket(int fd, char *buf, size_t count)
{
	return read_socket_internal(fd, buf, count, 0);
}

int 
read_socket_internal(int fd, char *buf, size_t count, int arb)
{
	ssize_t n, left = count;
	while (left > 0) {
		n = ff_read(fd, buf, left);
		if (n < 0) {
			return (-1);
		}
		if (n == 0) {
			return (count - left);
		}
		left -= n;
		buf += n;
		if (arb)
			break;
	}
	return (count - left);
}

int
write_socket(int fd, const char *buf, size_t count)
{
	ssize_t n, left = count;
	while (left > 0) {
		n = ff_write(fd, buf, left);
		if (n < 0) {
			DBG("Write returned errno %d", errno);
			return (-1);
		}
		if (n == 0) {
			DBG("Wrote 0 byte to client.");
			return (-1);
		}
		left -= n;
		buf += n;
	}
	return (count - left);
}

int
get_sockinfo(int fd, int *type, uint32_t *addr, uint16_t *port)
{
	struct sockaddr_in sin;
	socklen_t len = sizeof(sin);

	if (ff_getsockname(fd, (struct linux_sockaddr *)&sin, &len) == -1) {
		return (1);
	} else {
		*type = (int)sin.sin_family;
		*addr = sin.sin_addr.s_addr;
		*port = sin.sin_port;
		return (0);
	}
}

struct tcp_stat
get_tcp_info(int fd)
{
	struct tcp_stat rt;
	struct ff_tcp_info tinfo;
	socklen_t ti_len = sizeof(struct ff_tcp_info);

	if (ff_getsockopt_freebsd(fd, IPPROTO_TCP, FF_TCP_INFO, (void *)&tinfo, &ti_len)) {
		SYSERR(errno, "Cannot get tcp info from fd %d.", fd);
		return (rt);
	}

	rt.tcp_state = tinfo.tcpi_state;
	rt.rtt = tinfo.tcpi_rtt;
	rt.rtt_var = tinfo.tcpi_rttvar;
	rt.ss_thres = tinfo.tcpi_snd_ssthresh;
	rt.snd_cwnd = tinfo.tcpi_snd_cwnd;
	rt.rcv_space = tinfo.tcpi_rcv_space;
//	rt.snd_rexmitpkt = tinfo.tcpi_snd_rexmitpack;
//	rt.rcv_ooopkt = tinfo.tcpi_rcv_ooopack;

#ifdef __linux__
    rt.snd_rexmitpkt = 0;
    rt.rcv_ooopkt = 0;
#else
    rt.snd_rexmitpkt = tinfo.tcpi_snd_rexmitpack;
    rt.rcv_ooopkt = tinfo.tcpi_rcv_ooopack;
#endif

#ifdef SOMIGRATION
	rt.smg_bufsize = tinfo.tcpi_smg_bufsize;
	rt.smg_bufsize_cso = tinfo.tcpi_smg_bufsize_cso;
	rt.smg_clicpu = tinfo.tcpi_cli_cpu_id;
	rt.smg_smgcpu = tinfo.tcpi_smg_cpu_id;
#endif

	return (rt);
}
