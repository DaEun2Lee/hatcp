#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

#include <sys/resource.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <ev.h>

#include "io.h"
#include "app.h"
#include "worker.h"
#include "acc/dedup.h"
#include "server.h"
#include "stats.h"
#include "utils.h"
#include "netutils.h"
#include <ff_api.h>
#include <ff_epoll.h>
#include <rte_ethdev.h>
#include <time.h>

static void server_loop(struct wanacc_app *app);
static int init_wan(struct stream_entry *s, struct wanacc_app *app);

#ifndef WANACC_ENABLE_DBG_APPERR
#define WANACC_ENABLE_DBG_APPERR 0
#endif

#if WANACC_ENABLE_DBG_APPERR
#define DBG_APPERR(...) APPERR(__VA_ARGS__)
#else
#define DBG_APPERR(...) do { } while (0)
#endif

static void finish_wan_connect(struct stream_entry *stream);
static void wan_stream_clean(struct stream_entry *stream);
static struct stream_entry *find_wan_stream_by_fd(struct wanacc_app *app, int fd);
static int wan_connect_ready(struct stream_entry *stream);

extern int DEDUP_ENABLED;

extern int COMPRESSION_ENABLED;

static void
log_dpdk_port_stats_once(void)
{
        static time_t last_ts = 0;
        time_t now = time(NULL);
        struct rte_eth_stats st;

        if (now == last_ts)
                return;
        last_ts = now;

        memset(&st, 0, sizeof(st));
        if (rte_eth_stats_get(1, &st) != 0) {
                APPERR("[DPDK-STATS] port=1 rte_eth_stats_get failed.");
                return;
        }

        APPERR("[DPDK-STATS] port=1 ipackets=%llu opackets=%llu ibytes=%llu obytes=%llu ierrors=%llu oerrors=%llu",
            (unsigned long long)st.ipackets,
            (unsigned long long)st.opackets,
            (unsigned long long)st.ibytes,
            (unsigned long long)st.obytes,
            (unsigned long long)st.ierrors,
            (unsigned long long)st.oerrors);
}


static struct stream_entry *
find_wan_stream_by_fd(struct wanacc_app *app, int fd)
{
        struct stream_entry *wan_stream;

        if (app == NULL || fd < 0)
                return NULL;

        TAILQ_FOREACH(wan_stream, &app->wan_streams, list) {
                if (wan_stream->fd == fd)
                        return wan_stream;
        }

        return NULL;
}

static int
wan_connect_ready(struct stream_entry *stream)
{
        struct sockaddr_in address;
        int error;

        if (stream == NULL || stream->app == NULL || stream->fd < 0)
                return (-1);

        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = stream->app->wan_addr;
        address.sin_port = stream->app->wan_port;

        error = ff_connect(stream->fd, (struct linux_sockaddr *)&address,
            sizeof(address));

        if (error == 0 || errno == EISCONN)
                return (1);

        if (errno == EINPROGRESS || errno == EALREADY ||
            errno == EWOULDBLOCK || errno == EAGAIN) {
                DBG_APPERR("[DBG] WAN connect still pending fd %d Err %d.",
                    stream->fd, errno);
                return (0);
        }

        DBG_APPERR("[DBG] WAN connect check failed fd %d Err %d.",
            stream->fd, errno);
        return (-1);
}

static int
purge_txq_fd(struct wanacc_app *app, int fd, int stream_type)
{
        struct tx_entry *tx;
        struct tx_entry *next;
        int count = 0;

        pthread_mutex_lock(&app->txq_mtx);
        for (tx = TAILQ_FIRST(&app->txq); tx != NULL; tx = next) {
                next = TAILQ_NEXT(tx, list);

                if (tx->fd == fd && tx->stream_type == stream_type) {
                        TAILQ_REMOVE(&app->txq, tx, list);
                        free(tx->buf);
                        free(tx);
                        count++;
                }
        }
        pthread_mutex_unlock(&app->txq_mtx);

        return (count);
}


static void
process_tx_queue(struct wanacc_app *app)
{
        struct tx_entry *tx;
        ssize_t n;
        int write_errno;
        int failed_fd;
        int failed_stream_type;
        int purged;

for (;;) {
                pthread_mutex_lock(&app->txq_mtx);
                tx = TAILQ_FIRST(&app->txq);

                if (tx == NULL) {
                        pthread_mutex_unlock(&app->txq_mtx);
                        return;
                }

                pthread_mutex_unlock(&app->txq_mtx);

                if (tx->stream_type == STREAM_IO_WAN_CONNECTING) {
                        /*
                        DBG_APPERR("[DBG] TXQ waiting for WAN_CONNECTING fd %d len %zu offset %zu.",
                            tx->fd, tx->len, tx->offset);
                        */
                        return;
                }

                if (tx->stream_type == STREAM_IO_WANFD) {
                        n = ff_write(tx->fd,
                            tx->buf + tx->offset,
                            tx->len - tx->offset);
                } else {
                        n = write_linux_socket(tx->fd,
                            tx->buf + tx->offset,
                            tx->len - tx->offset);
                }

                if (n > 0) {
                        tx->offset += (size_t)n;

                        if (tx->offset < tx->len)
                                return;

                        pthread_mutex_lock(&app->txq_mtx);
                        TAILQ_REMOVE(&app->txq, tx, list);
                        pthread_mutex_unlock(&app->txq_mtx);

                        free(tx->buf);
                        free(tx);
                        continue;
                }

                if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                        return;
                write_errno = errno;
                failed_fd = tx->fd;
                failed_stream_type = tx->stream_type;

                APPERR("write failed on fd %d type %d (Err %d).",
                    failed_fd, failed_stream_type, write_errno);

                if (write_errno == EBADF || write_errno == EPIPE ||
                    write_errno == ECONNRESET) {
                        purged = purge_txq_fd(app, failed_fd, failed_stream_type);
                        APPERR("purged %d txq entries for closed fd %d type %d.",
                            purged, failed_fd, failed_stream_type);
                        continue;
                }

                pthread_mutex_lock(&app->txq_mtx);
                TAILQ_REMOVE(&app->txq, tx, list);
                pthread_mutex_unlock(&app->txq_mtx);

                free(tx->buf);
                free(tx);
}
}

int
init_server(struct wanacc_app *app) 
{
	int error = 0;

#ifdef PERF_PROFILING
#ifdef TSC_CLOCK
	init_tsc();
#endif
#endif

	dedup_init();

#ifdef HASH_SKEIN_256
	DBG("Hash: Skein_256.");
#elif HASH_SHA3_256
	DBG("Hash: SHA3_256.");
#elif HASH_MD5
	DBG("Hash: MD5");
#else
	DBG("Hash: SHA2_256.");
#endif
	
	if (app->mode != WANACC_MID_SERVER && app->mode != WANACC_SERVER) {
		APPERR("Unrecognized app mode.");
		exit(0);
	}

	if (app->connect_to_wan && app->listen_on_wan) {
		APPERR("Cannot connect/listen to WAN port simultaneously.");
		exit(0);
	}
	
	if (app->mode == WANACC_MID_SERVER) {
		error = init_linux_listener(&(app->listen_fd),
		            app->app_addr, app->app_port);
		if (error) {
			DBG("Failed to initialize Linux front socket");
			return (error);
		}

		if (app->listen_on_wan) {
			error = init_socket_somig(&(app->listen_fd_wan), 
				    app->mode, app->wan_addr, app->wan_port, 
				    app->somig_on_wan ? app->somig_mode : 0, 
				    app->mso_addr, app->ctl_port,
				    app->rso_addr, app->ctl_port);
			if (error) {
				DBG("Failed to initialize wan socket");
				return (error);
			}
		}
	}

	if (app->mode == WANACC_SERVER) {
		if (app->connect_to_wan) {
			// do nothing
		} else {
			error = init_socket_somig(&(app->listen_fd), app->mode,
				    app->app_addr, app->app_port, app->somig_mode, 
				    app->mso_addr, app->ctl_port, app->rso_addr,
				    app->ctl_port);
			if (error) {
				DBG("Failed to initialize socket");
				exit(1);
			}
		}
	}

	return (error);
}


static void
wan_stream_clean(struct stream_entry *stream)
{
        struct wanacc_app *app;

        if (stream == NULL)
                return;

        app = stream->app;

        /*
         * app->wan_streams is a pre-created reusable slot list.
         * Each worker keeps worker->wan_stream pointing to its slot.
         * Therefore, do not remove/free this stream here.
         */
        if (app != NULL && app->epfd >= 0 && stream->fd >= 0)
                ff_epoll_ctl(app->epfd, EPOLL_CTL_DEL,
                    stream->fd, NULL);

        if (stream->fd >= 0)
                ff_close(stream->fd);

        stream->fd = -1;
        stream->hash = 0;
        stream->io.stream = stream;
        stream->io.stream_type = STREAM_IO_WANFD;
}


static int
start_wan_connect(struct stream_entry *stream)
{
        struct wanacc_app *app;
        struct sockaddr_in address;
        struct epoll_event ev;
        int nonblock = 1;
        int fd = -1;
        int error;
        int connect_errno;

        if (stream == NULL || stream->app == NULL)
                return (-1);

        app = stream->app;

        if (stream->fd >= 0)
                return (0);

        if (app->somig_on_wan) {
                APPERR("Async SOMIG WAN connect is not implemented yet.");
                return (-1);
        }

        error = init_socket(&fd);
        if (error != 0)
                return (-1);

        if (ff_ioctl(fd, FIONBIO, &nonblock) != 0) {
                APPERR("Failed to set WAN socket non-blocking.");
                ff_close(fd);
                return (-1);
        }

        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = app->wan_addr;
        address.sin_port = app->wan_port;

        DBG("Connecting to end wan @ %u:%u..",
            app->wan_addr, app->wan_port);

        error = ff_connect(fd,
            (struct linux_sockaddr *)&address,
            sizeof(address));
        connect_errno = (error == 0) ? 0 : errno;
        DBG_APPERR("[DBG] ff_connect ret %d errno %d fd %d.", error, connect_errno, fd);

        if (error != 0 &&
            connect_errno != EINPROGRESS &&
            connect_errno != EALREADY &&
            connect_errno != EWOULDBLOCK) {
                APPERR("Failed to start WAN connection (Err %d).",
                    connect_errno);
                ff_close(fd);
                return (-1);
        }

        stream->fd = fd;
        stream->hash = fd;

        memset(&ev, 0, sizeof(ev));
        ev.data.ptr = &stream->io;

        if (error == 0) {
                stream->io.stream_type = STREAM_IO_WANFD;
                ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
                DBG("Connected to end wan immediately, fd %d.", fd);
        } else {
                stream->io.stream_type = STREAM_IO_WAN_CONNECTING;
                ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
        }

        if (ff_epoll_ctl(app->epfd, EPOLL_CTL_ADD,
            fd, &ev) != 0) {
                APPERR("Failed to register connecting WAN fd %d.",
                    fd);
                ff_close(fd);
                stream->fd = -1;
                return (-1);
        }
        DBG_APPERR("[DBG] registered WAN fd %d stream_type %d events 0x%x.", fd, stream->io.stream_type, ev.events);

        return (0);
}

static void
finish_wan_connect(struct stream_entry *stream)
{
        struct epoll_event ev;
        struct tx_entry *tx_iter;
        int socket_error = 0;
        int tx_retag_count = 0;
        socklen_t error_len = sizeof(socket_error);

        if (stream == NULL || stream->app == NULL)
                return;

        DBG_APPERR("[DBG] finish_wan_connect called fd %d.", stream->fd);

        if (ff_getsockopt(stream->fd, SOL_SOCKET, SO_ERROR,
            &socket_error, &error_len) != 0) {
                APPERR("Failed to check WAN connection fd %d (Err %d).",
                    stream->fd, errno);
                wan_stream_clean(stream);
                return;
        }

        if (socket_error != 0) {
                APPERR("WAN connection fd %d failed (Err %d).",
                    stream->fd, socket_error);
                wan_stream_clean(stream);
                return;
        }

        stream->io.stream_type = STREAM_IO_WANFD;

        pthread_mutex_lock(&stream->app->txq_mtx);
        TAILQ_FOREACH(tx_iter, &stream->app->txq, list) {
                if (tx_iter->fd == stream->fd &&
                    tx_iter->stream_type == STREAM_IO_WAN_CONNECTING) {
                        tx_iter->stream_type = STREAM_IO_WANFD;
                        tx_retag_count++;
                }
        }
        pthread_mutex_unlock(&stream->app->txq_mtx);

        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
        ev.data.ptr = &stream->io;

        if (ff_epoll_ctl(stream->app->epfd, EPOLL_CTL_MOD,
            stream->fd, &ev) != 0) {
                APPERR("Failed to activate WAN fd %d.",
                    stream->fd);
                wan_stream_clean(stream);
                return;
        }

        DBG_APPERR("[DBG] WAN fd %d connected, retagged %d txq entries.",
            stream->fd, tx_retag_count);
        DBG("Connected to end wan, fd %d.", stream->fd);
        process_tx_queue(stream->app);
}

static void
accept_wan_stream(struct wanacc_app *app)
{
        struct stream_entry *wan_stream = NULL;
        struct epoll_event ev;
        int fd;

        TAILQ_FOREACH(wan_stream, &app->wan_streams, list) {
                if (wan_stream->fd < 0)
                        break;
        }

        if (wan_stream == NULL) {
                APPERR("No free WAN stream for incoming MS connection.");
                return;
        }

        fd = init_wan(wan_stream, app);
        if (fd <= 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                        APPERR("Failed to accept WAN connection from MS.");
                return;
        }

        wan_stream->io.stream_type = STREAM_IO_WANFD;
        wan_stream->io.stream = wan_stream;

        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
        ev.data.ptr = &wan_stream->io;

        if (ff_epoll_ctl(app->epfd, EPOLL_CTL_ADD,
            wan_stream->fd, &ev) != 0) {
                APPERR("Failed to register accepted WAN fd %d.",
                    wan_stream->fd);
                ff_close(wan_stream->fd);
                wan_stream->fd = -1;
                wan_stream->hash = 0;
                return;
        }

        DBG("Accepted and registered WAN connection from MS, fd %d.",
            wan_stream->fd);
}


static int
fstack_loop(void *arg)
{
struct wanacc_app *app = (struct wanacc_app *)arg;
struct epoll_event events[64];
struct stream_ev_io *sei;
struct stream_entry *wan_stream;
int nevents;

        /* log_dpdk_port_stats_once(); */
/* Start the MS -> ES WAN connection only after ff_run begins. */
if (app->mode == WANACC_MID_SERVER && !app->listen_on_wan) {
        TAILQ_FOREACH(wan_stream, &app->wan_streams, list) {
                if (wan_stream->fd < 0) {
                        if (start_wan_connect(wan_stream) != 0) {
                                APPERR("Failed to start WAN connection.");
                                wan_stream_clean(wan_stream);
                                break;
                        }
                }
        }
}

process_tx_queue(app);

/* 기존 libev 통계 타이머만 non-blocking으로 처리 */
if (app->loop != NULL)
ev_run(app->loop, EVRUN_NOWAIT);

nevents = ff_epoll_wait(app->epfd, events, 64, 0);
if (nevents < 0) {
if (errno != EINTR)
APPERR("ff_epoll_wait failed (Err %d).", errno);
return (0);
}

for (int i = 0; i < nevents; i++) {
sei = (struct stream_ev_io *)events[i].data.ptr;
if (sei == NULL)
continue;

if (sei->stream_type == STREAM_IO_WAN_CONNECTING &&
    (events[i].events & (EPOLLOUT | EPOLLERR | EPOLLHUP))) {
        DBG_APPERR("[DBG] WAN_CONNECTING event fd %d events 0x%x.",
            sei->stream != NULL ? sei->stream->fd : -1,
            events[i].events);
        finish_wan_connect(sei->stream);
        continue;
}

if (events[i].events & (EPOLLERR | EPOLLHUP)) {
        if (sei->stream_type == STREAM_IO_WANFD)
                wan_stream_clean(sei->stream);
        else if (sei->stream_type != STREAM_IO_LISTENFD)
                stream_clean(sei->stream);
        continue;
}

if (!(events[i].events & EPOLLIN))
        continue;

switch (sei->stream_type) {
case STREAM_IO_LISTENFD:
        if (sei->app->mode == WANACC_SERVER)
                accept_wan_stream(sei->app);
        else
                wanacc_accept_stream(sei->app);
        break;
case STREAM_IO_WANFD:
back_worker_process_stream(sei->stream);
break;

case STREAM_IO_CLIFD:
case STREAM_IO_TARGETFD:
front_worker_process_stream(sei->stream);
break;

default:
APPERR("Unknown F-Stack epoll stream type %d.",
    sei->stream_type);
break;
}
}

process_tx_queue(app);
return (0);
}

static int
init_wan(struct stream_entry *s, struct wanacc_app *app)
{
	int error;
	int fd = -1;

	switch (app->mode) {
	case WANACC_MID_SERVER:
		if (app->listen_on_wan) {
			/* Accept mid server connections */
			DBG("Waiting for incoming WAN connections from ES..");
			fd = accept_socket(app->listen_fd_wan);
			DBG("Accepted a WAN connection from ES.");
		} else {
			/* Connect to end server */
			error = init_socket(&fd);
			if (error) {
				DBG("Cannot init wan fd");
				return (-1);
			}
			DBG("Connecting to end wan @ %u:%u..", app->wan_addr, app->wan_port);
			error = connect_socket(fd, app->wan_addr, app->wan_port, 
				    app->somig_on_wan ? app->somig_mode : 0, app->mso_addr, app->ctl_port,
				    app->rso_addr, app->ctl_port);
			if (error) {
				DBG("Cannot connect to end wan");
				perror("Reason");
				return (-1);
			}
			DBG("Connected to end wan @ %u:%u..", app->wan_addr, app->wan_port);
		}
		break;
	case WANACC_SERVER:
		if (app->connect_to_wan) {
			error = init_socket(&fd);
			if (error) {
				DBG("Cannot init wan fd");
				return (-1);
			}
			DBG("Connecting to mid wan @ %u:%u..", app->app_addr, app->app_port);
			error = connect_socket(fd, app->app_addr, app->app_port, 
				    0, 0, 0, 0, 0);
			if (error) {
				DBG("Cannot connect to mid wan");
				perror("Reason");
				return (-1);
			}
			DBG("Connected to mid wan @ %u:%u..", app->app_addr, app->app_port);
		} else {
			/* Accept mid server connections */
			DBG("Waiting for incoming WAN connections from MS..");
			fd = accept_socket(app->listen_fd);
			DBG("Accepted a WAN connection from MS.");
		}
		break;
	}

	if (fd > 0) {
		s->fd = fd;
		s->hash = fd; /* TODO: replace with real hash */
	}

	return (fd);
}

static void
server_loop(struct wanacc_app *app) 
{
	struct stream_ev_io ev_io_listenfd;
	struct stream_ev_io wan_listen_io;
	struct stream_entry *wan_stream;
	pthread_t *thread_front, *thread_back;
	struct worker *w;
	int rt, worker_count, fd;
	
	if (app->mode == WANACC_MID_SERVER) {
		DBG("Starting middle server...");
	} else if (app->mode == WANACC_SERVER) {
		DBG("Starting end server...");
	}

	/* Spawn front and back workers */
	worker_count = 0;
//	thread_front = calloc(app->front_worker_count, sizeof(pthread_t));
//	thread_back = calloc(app->back_worker_count, sizeof(pthread_t));
	// Modify by delee
	thread_front = (pthread_t *)calloc(app->front_worker_count, sizeof(pthread_t));
	thread_back  = (pthread_t *)calloc(app->back_worker_count, sizeof(pthread_t));

	for (int i=0;i<app->front_worker_count;i++) {
		rt = pthread_create(&(thread_front[i]), NULL, 
			worker_loop, (void*)&(app->front_workers[i]));
		if (rt) {
			APPERR("Failed to start front worker thread. \n");
			exit(0);
		}
	}
	for (int i=0;i<app->back_worker_count;i++) {
		rt = pthread_create(&(thread_back[i]), NULL, 
			worker_loop, (void*)&(app->back_workers[i]));
		if (rt) {
			APPERR("Failed to start back worker thread. \n");
			exit(0);
		}
	}

	while (worker_count < app->front_worker_count + app->back_worker_count) {
		pthread_mutex_lock(&app->worker_mtx);
		worker_count = app->worker_count;
		pthread_mutex_unlock(&app->worker_mtx);
	}

	assert(app->wan_count == app->back_worker_count);

	app->epfd = ff_epoll_create(0);
	if (app->epfd < 0) {
		APPERR("Failed to create F-Stack epoll descriptor.");
		exit(1);
	}

        /* Prepare WAN streams.
         * The actual non-blocking connect starts inside ff_run(). */
        for (int i = 0; i < app->wan_count; i++) {
                wan_stream = (struct stream_entry *)calloc(
                    1, sizeof(struct stream_entry));
                if (wan_stream == NULL) {
                        APPERR("Failed to allocate WAN stream.");
                        exit(1);
                }

                w = &app->back_workers[i];

                wan_stream->fd = -1;
                wan_stream->worker = w;
                wan_stream->app = app;
                wan_stream->io.stream_type =
                    STREAM_IO_WAN_CONNECTING;
                wan_stream->io.stream = wan_stream;
                TAILQ_INIT(&wan_stream->ioq);

                TAILQ_INSERT_TAIL(&app->wan_streams,
                    wan_stream, list);
                w->wan_stream = wan_stream;
        }

	/* Register Linux client listener to the libev loop. */
	memset(&ev_io_listenfd, 0, sizeof(ev_io_listenfd));
	ev_io_listenfd.app = app;
	ev_io_listenfd.stream_type = STREAM_IO_LISTENFD;

	if (app->mode == WANACC_MID_SERVER && app->listen_fd >= 0) {
		ev_io_init(&ev_io_listenfd.evio, wanacc_new_stream_cb,
		    app->listen_fd, EV_READ);
		ev_io_start(app->loop, &ev_io_listenfd.evio);
	}
        /*
         * Register the ES F-Stack WAN listener.
         */
        if (app->mode == WANACC_SERVER &&
            !app->connect_to_wan &&
            app->listen_fd >= 0) {
                struct epoll_event listen_ev;
                int nonblock = 1;

                memset(&wan_listen_io, 0, sizeof(wan_listen_io));
                wan_listen_io.app = app;
                wan_listen_io.stream_type = STREAM_IO_LISTENFD;

                if (ff_ioctl(app->listen_fd, FIONBIO, &nonblock) != 0) {
                        APPERR("Failed to set ES WAN listener non-blocking.");
                        exit(1);
                }

                memset(&listen_ev, 0, sizeof(listen_ev));
                listen_ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
                listen_ev.data.ptr = &wan_listen_io;

                if (ff_epoll_ctl(app->epfd, EPOLL_CTL_ADD,
                    app->listen_fd, &listen_ev) != 0) {
                        APPERR("Failed to register ES WAN listener fd %d.",
                            app->listen_fd);
                        exit(1);
                }

                DBG("ES WAN listener fd %d registered to F-Stack epoll.",
                    app->listen_fd);
        }

  /* Setup remote connections if needed */
	if (app->mode == WANACC_SERVER && app->listen_on_remote) {
//		app->remote_fds = calloc(app->back_worker_count, sizeof(int));
		// Modify by delee
		app->remote_fds = (int *)calloc(app->back_worker_count, sizeof(int));
		if (app->somig_mode == WANACC_SERVER_SMG_REPLICA)
			usleep(200000);

		rt = init_socket_somig(&(app->listen_fd_remote), app->mode,
				    app->wan_addr, app->wan_port, app->somig_mode, 
				    app->wan_mso_addr, app->wan_ctl_port, 
				    app->wan_rso_addr, app->wan_ctl_port);
		if (rt) {
			DBG("Failed to initialize socket");
			exit(1);
		}
    
		for (int i=0;i<app->back_worker_count;i++) {
			DBG("Waiting for incoming connections from remote..");
			fd = accept_socket(app->listen_fd_remote);
			app->remote_fds[i] = fd;
			DBG("Accepted a connection from remote fd %d.",
			    fd);
		}
	}

	/* Start the F-Stack packet and socket event loop. */
	DBG("wanacc started...");
	ff_run(fstack_loop, app);

	DBG("stopping wanacc...");
	ev_loop_destroy(app->loop);

	for (int i=0;i<app->front_worker_count;i++) {
		pthread_join(thread_front[i], NULL);
	}
	for (int i=0;i<app->back_worker_count;i++) {
		pthread_join(thread_back[i], NULL);
	}
}

void
start_server(struct wanacc_app *app)
{
#ifdef SOMIGRATION
	struct stat_ev_io sio;
#endif
#ifdef PERF_PROFILING
	struct stat_ev_io pio;
#endif
	struct queue_ev_io *qio;
	struct worker *w;

	INFO("Dedup: %d\n", DEDUP_ENABLED);
	INFO("Compression: %d\n", COMPRESSION_ENABLED);

	getrusage(RUSAGE_SELF, &app->rlast);
	app->loop = ev_loop_new(0);

//	app->front_workers = calloc(app->front_worker_count, sizeof(struct worker));
//	app->back_workers = calloc(app->back_worker_count, sizeof(struct worker));
	// Modify by delee
	app->front_workers = (struct worker *)calloc(app->front_worker_count, sizeof(struct worker));
	app->back_workers  = (struct worker *)calloc(app->back_worker_count, sizeof(struct worker));
	for (int i=0;i<app->front_worker_count;i++) {
		w = &app->front_workers[i];
		w->id = i;
		w->type = WANACC_WORKER_FRONT;
		w->stream_count = 0;
		w->app = app;
		w->next_back_worker = 0;

		qio = &w->ev_queue;
		qio->worker = w;
		ev_async_init(&qio->io, front_worker_queueing_cb);
	}
	for (int i=0;i<app->back_worker_count;i++) {
		w = &app->back_workers[i];
		w->id = i;
		w->type = WANACC_WORKER_BACK;
		w->stream_count = 0;
		w->app = app;

		TAILQ_INIT(&w->ioq);
		TAILQ_INIT(&w->dtq);
//		w->dtq_mtx = PTHREAD_MUTEX_INITIALIZER;
		// Modify by delee
		pthread_mutex_init(&w->dtq_mtx, NULL);

		hashtable_init(&w->ht);

		qio = &w->ev_queue;
		qio->worker = w;
		ev_async_init(&qio->io, back_worker_queueing_cb);
	}

	if (app->usage_fn != NULL) {
		app->usage_fp = fopen(app->usage_fn, "w");
		if (app->usage_fp != NULL) {
#ifdef PERF_PROFILING
			fprintf(app->usage_fp, "second,fd,cli_cpu,smg_cpu,app_cpu,mbuf,mbuf9k,mem,ff,bf,bq,in,out\n");
#else
			fprintf(app->usage_fp, "second,fd,cli_cpu,smg_cpu,app_cpu,mbuf,mbuf9k,mem\n");
#endif
			stats_init();
		}
	}
#ifdef PERF_PROFILING
	else { 
		/* printf("ff,bf,bq,in,out,dedup_cyc_sum,dedup_cyc_avg,compz_cyc_sum,compz_cyc_avg\n"); */
		
		pio.app = app;
		ev_timer_init(&(pio.tmout_w), &wanacc_perf_cb, 1, 1);
		ev_timer_start(app->loop, &(pio.tmout_w));
	}
#endif

#ifdef SOMIGRATION
	if (app->usage_fp != NULL || app->failover > 0) {
		sio.app = app;
		ev_timer_init(&(sio.tmout_w), wanacc_stat_cb, 1, 1);
		ev_timer_start(app->loop, &(sio.tmout_w));
	}
#endif

	server_loop(app);

#ifdef SOMIGRATION
	if (app->usage_fp != NULL)
		fclose(app->usage_fp);
#endif

	if (app->remote_fds)
		free(app->remote_fds);
	free(app->front_workers);
	free(app->back_workers);
}

