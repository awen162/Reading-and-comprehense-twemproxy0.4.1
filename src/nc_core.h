/*
 * twemproxy - A fast and lightweight proxy for memcached protocol.
 * Copyright (C) 2011 Twitter, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _NC_CORE_H_
#define _NC_CORE_H_

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#define HAVE_DEBUG_LOG 
#ifdef HAVE_DEBUG_LOG
# define NC_DEBUG_LOG 1
#endif

#ifdef HAVE_ASSERT_PANIC
# define NC_ASSERT_PANIC 1
#endif

#ifdef HAVE_ASSERT_LOG
# define NC_ASSERT_LOG 1
#endif

#ifdef HAVE_STATS
# define NC_STATS 1
#else
# define NC_STATS 0
#endif

#ifdef HAVE_EPOLL
# define NC_HAVE_EPOLL 1
#elif HAVE_KQUEUE
# define NC_HAVE_KQUEUE 1
#elif HAVE_EVENT_PORTS
# define NC_HAVE_EVENT_PORTS 1
#else
# error missing scalable I/O event notification mechanism
#endif

#ifdef HAVE_LITTLE_ENDIAN
# define NC_LITTLE_ENDIAN 1
#endif

#ifdef HAVE_BACKTRACE
# define NC_HAVE_BACKTRACE 1
#endif

#define NC_OK        0
#define NC_ERROR    -1
#define NC_EAGAIN   -2
#define NC_ENOMEM   -3

/* reserved fds for std streams, log, stats fd, epoll etc. */
#define RESERVED_FDS 32

typedef int rstatus_t; /* return type */
typedef int err_t;     /* error type */

struct array;
struct string;
struct context;
struct conn;
struct conn_tqh;
struct msg;
struct msg_tqh;
struct server;
struct server_pool;
struct mbuf;
struct mhdr;
struct conf;
struct stats;
struct instance;
struct event_base;

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>

#include <nc_array.h>
#include <nc_string.h>
#include <nc_queue.h>
#include <nc_rbtree.h>
#include <nc_log.h>
#include <nc_util.h>
#include <event/nc_event.h>
#include <nc_stats.h>
#include <nc_mbuf.h>
#include <nc_message.h>
#include <nc_connection.h>
#include <nc_server.h>

//������صĽṹ��·��instance->context->conf->conf_pool(conf_server)->server_pool(server)
//�����ռ�ͳ�ʼ����core_ctx_create
struct context { //�����instance->ctx    
    uint32_t           id;          /* unique context id */
    //��������ļ��е�������Ϣ ctx->cf = conf_create(nci->conf_filename);  ������Ϣ���
    struct conf        *cf;         /* configuration */
    struct stats       *stats;      /* stats */
/*
    alpha:
  listen: 127.0.0.1:22121
  hash: fnv1a_64
  distribution: ketama
  auto_eject_hosts: true
  redis: true
  server_retry_timeout: 2000
  server_failure_limit: 1
  servers:
   - 127.0.0.1:6379:1

beta:
  listen: 127.0.0.1:22122
  hash: fnv1a_64
  hash_tag: "{}"
  distribution: ketama
  auto_eject_hosts: false
  timeout: 400
  redis: true
  servers:
   - server1 127.0.0.1:6380:1
   - server2 127.0.0.1:6381:1
   - server3 127.0.0.1:6382:1
   - server4 127.0.0.1:6383:1
*/ //alpha��beta���Զ�Ӧһ��conf_pool�ṹ

    //�����Ա����Ϊserver_pool�������ռ�͸�ֵ��server_pool_init
    struct array       pool;        /* server_pool[] */ //
    //�����ռ�͸�ֵ��core_ctx_create->event_base_create   ���е�������Ϣ��ͨ��event_add_conn��conn���뵽��evb�У����Բο�proxy_accept
    struct event_base  *evb;        /* event base */
    //��instanceȡ������core_ctx_create  ������ԴΪinstance->stats_interval  ��������epoll_wait�ĳ�ʱʱ�䣬��core_loop core_timeout
    int                max_timeout; /* max timeout in msec */
    //��������epoll_wait�ĳ�ʱʱ�䣬��core_loop  ������ԴΪinstance->stats_interval  ��������epoll_wait�ĳ�ʱʱ�䣬��core_loop
    int                timeout;     /* timeout in msec */ //Ҳ������core_timeout�������ã�����Ϊ�뵱ǰ�����ʱ��

    //��fd��Ŀ
    uint32_t           max_nfd;     /* max # files */
    //�ܵļ�ȥtwemproxy bind���Լ��ڲ�ʹ�õ��ļ�fd����log��  �ο�core_calc_connections
    uint32_t           max_ncconn;  /* max # client connections */
    //��ֵ��server_pool_each_calc_connections   �ͺ�˷������ܵ�������
    uint32_t           max_nsconn;  /* max # server connections */
};

//������صĽṹ��·��instance->context->conf->conf_pool(conf_server)->server_pool(server)

//����������Ϣ����Դ��Ϣ��Դͷ���Ǹýṹָ������Դͷ��main�е�struct instance nci;
struct instance { //ȫ��ͨ�����ã���ʼ����nc_set_default_options�������ڴ���Ϣ��Դͷ��������
    //��ֵ��core_start
    struct context  *ctx;                        /* active context */
    //ֵԽ�󣬴�ӡ����־Խ��
    int             log_level;                   /* log level */ //���ո�ֵ����logger->level����nc_pre_run
    char            *log_filename;               /* log filename */ //Ĭ��û����־��ͨ�����г����ʱ�����-O���������ã���nci->log_filename = optarg;
    //-c����ָ�������ļ�·��
    char            *conf_filename;              /* configuration filename */

    /*
     Ĭ��ֵ
     STATS_ADDR      "0.0.0.0"   �����IP��ַ�Ͷ˿���Ϊ��ȡstatsʹ�õ�
     STATS_PORT      22222
    */ //������ַ�Ͷ˿� ����ͨ��-s����  ע������������ļ��е�listen�����ǲ�һ���ģ�stats_port��ͨ��-s�����ڳ���������������
    //���ձ���ֵ��stats->port,��stats_create
    uint16_t        stats_port;                  /* stats monitoring port */
    //���ձ���ֵ��stats->interval����stats_create
    //���ձ���ֵ��stats->interval����stats_create  ��������epoll_wait�ĳ�ʱʱ�䣬��core_loop
    int             stats_interval;              /* stats aggregation interval */
    //���ձ���ֵ��stats->addr����stats_create
    char            *stats_addr;                 /* stats monitoring addr */
    //ͨ��nc_gethostname��ȡ������
    char            hostname[NC_MAXHOSTNAMELEN]; /* hostname */ 
    /*
    read, writev and mbuf
   ���е��������Ӧ����mbuf���棬mbufĬ�ϴ�С��16K��512b-16M��,����ʹ��
-m or -mbuf-size=N�����ã�ÿһ�����Ӷ���������һ��mbuf������ζ��nutcracker֧�ֵ�
������������������mbuf�Ĵ�С��С��mbuf���Կ��Ƹ�������ӣ����mbuf����������
������д��������ݵ�socker buffer������������ܴ�ĳ������Ƽ�ʹ�ñȽ�С��mbuf��512 or 1K��

  ÿһ���ͻ������������Ҫһ��mbuf��һ�����������������������ӣ�client->proxy��proxy->server������������Ҫ����mbufs
  1000���ͻ������ӵĳ������㣺1000*2*mbuf=32M,���ÿ��������10�����������ֵ������320M,����������10000����ô����
����3.2G�ڴ棡���ֳ�������õ�Сmbufֵ����512b��1000*2*512*10=10M������ǵ��������ߵĳ�����ʹ��С��mbuf��ԭ��
    */ 
//ע���������������һ��msg����msg�ǲ����ͷŵģ���������ö��У����Ը�ֵ�ڸ߲��������²���̫�࣬ʵ�����ĵ��ڴ�Ϊ�����������µ��ڴ棬��ʹ���ӶϿ����ڴ�Ҳ���ͷ�
    size_t          mbuf_chunk_size;             /* mbuf chunk size */ //mbuf��С  Ĭ��ֵMBUF_SIZE
    pid_t           pid;                         /* process id */ //���̺�
    char            *pid_filename;               /* pid filename */ //-p����ָ��
    //��ʶ�Ƿ񴴽���pid�ļ�
    unsigned        pidfile:1;                   /* pid file created? */
};

struct context *core_start(struct instance *nci);
void core_stop(struct context *ctx);
rstatus_t core_core(void *arg, uint32_t events);
rstatus_t core_loop(struct context *ctx);

#endif
