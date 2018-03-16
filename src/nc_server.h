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

#ifndef _NC_SERVER_H_
#define _NC_SERVER_H_

#include <nc_core.h>

/*
 * server_pool is a collection of servers and their continuum. Each
 * server_pool is the owner of a single proxy connection and one or
 * more client connections. server_pool itself is owned by the current
 * context.
 *
 * Each server is the owner of one or more server connections. server
 * itself is owned by the server_pool.
 *
 *  +-------------+
 *  |             |<---------------------+
 *  |             |<------------+        |
 *  |             |     +-------+--+-----+----+--------------+
 *  |   pool 0    |+--->|          |          |              |
 *  |             |     | server 0 | server 1 | ...     ...  |
 *  |             |     |          |          |              |--+
 *  |             |     +----------+----------+--------------+  |
 *  +-------------+                                             //
 *  |             |
 *  |             |
 *  |             |
 *  |   pool 1    |
 *  |             |
 *  |             |
 *  |             |
 *  +-------------+
 *  |             |
 *  |             |
 *  .             .
 *  .    ...      .
 *  .             .
 *  |             |
 *  |             |
 *  +-------------+
 *            |
 *            |
 *            //
 */

typedef uint32_t (*hash_t)(const char *, size_t);

// �������ݽṹ����continuum���ϵĽ�㣬���ϵ�ÿ������ʵ������һ��ip��ַ���ýṹ�ѵ��ip��ַһһ��Ӧ������
struct continuum {
    uint32_t index;  /* server index */ //���server����������
    uint32_t value;  /* hash value */   //�ڻ��ϵ�λ��
};

//������صĽṹ��·��instance->context->conf->conf_pool(conf_server)->server_pool(server)
//�����ļ���ÿ�����server��Ӧ��servers: ���ã� servers:�б��е�ÿһ��- 192.168.1.111:7000:1��Ӧһ��server,��ownerΪ���server������alpha
//һ�������ʵredis��������Ӧһ��server
struct server { //server_pool->server�����ԱΪ�����ͣ������ռ�͸�ֵ��stats_pool_init->stats_server_map  ����server_pool_init
    uint32_t           idx;           /* server index */ //�����������е�λ��
    //��server��˭������ֵ��server_each_set_owner��Ҳ����������server pool
    struct server_pool *owner;        /* owner pool */ //����alpha:�����servers:��

    //127.0.0.1:11211:1 server1�е��ַ���127.0.0.1:11211:1
    struct string      pname;         /* hostname:port:weight (ref in conf_server) */
    struct string      name;          /* hostname:port or [name] (ref in conf_server) */
    struct string      addrstr;       /* hostname (ref in conf_server) */
    uint16_t           port;          /* port */
    uint32_t           weight;        /* weight */
    struct sockinfo    info;          /* server socket info */

    //server_ref������  ��ʾ��twemproxy���̺͸ú��server��������
    uint32_t           ns_conn_q;     /* # server connection */
    //������conn���Ӷ��У���server_ref  
    struct conn_tqh    s_conn_q;      /* server connection q */
    //���Է�ֹ���ߺ������������ˣ����Ǿ���ѡ�ٲ��������⣬���ߺ����ô��ms���Լ���ѡ�ٸ÷�����
    int64_t            next_retry;    /* next retry time in usec */
    //failure_count��server_failure_limit��ϣ���server_failure
    uint32_t           failure_count; /* # consecutive failures */ //������дʧ�ܴ�������server_failure
};


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
*/
//����������alpha��beta�ֱ��Ӧһ��server_pool

//������صĽṹ��·��instance->context->conf->conf_pool(conf_server)->server_pool(server)

//�洢��context->pool������
//server_pool�е�ֵ��conf pool�п�����������conf_pool_each_transform
//������������conf_handler  ,���մ��뵽conf_pool���ο�����conf_commands

//conf_pool��server_pool�кܶ�����һ���ģ�ΪʲôҪ��������?��Ϊconf_pool���������ļ���һ�ݿ�����
//��server_pool��������ͳ�Ƹ���ͳ����Ϣ�Լ���hash�ã����Բο�conf_dump

//server_pool�洢��context->pool   conf_pool�ṹ����conf->pool������  �ο�server_pool_init(&ctx->pool, &ctx->cf->pool, ctx);
struct server_pool { //����������Ϣʵ����������Դ���������ļ�   һ����server��Ӧһ��server_pool�ṹ�������server alpha:
    uint32_t           idx;                  /* pool index */ //idx��ʾ���ǵڼ���pool������alpha  beta�ǵڼ���   //�����������е�λ��
    //��ֵ��server_pool_each_set_owner
    struct context     *ctx;                 /* owner context */

    //listen������ʱ���Ӧ��conn
    struct conn        *p_conn;              /* proxy connection (listener) */
    uint32_t           nc_conn_q;            /* # client connection */
    //����ÿһ��server_pool�������ж��client����������ÿһ��client���Ӷ�����server_pool->c_conn_q���С�
    struct conn_tqh    c_conn_q;             /* client connection q */

    //�����ռ��server_init�͸�ֵ��conf_pool_each_transform      ��conf_pool->server�п������ݹ�����
    //��������ļ���ÿ����server�ж�Ӧ��servers: 
    struct array       server;               /* server[] */ //�����Ա����Ϊstruct server
    //���ϵĵ���  ���з������ڻ��ϵĵ����ܺ�
    uint32_t           ncontinuum;           /* # continuum points */
    //һ����hash�����Ч��������+ KETAMA_CONTINUUM_ADDITION
    uint32_t           nserver_continuum;    /* # servers - live and dead on continuum (const) */
    //һ����hash������ĵ����ͺ�˷����������Ķ�Ӧ��ϵ���飬���Բο�ketama_dispatch  
    struct continuum   *continuum;           /* continuum */ //�����ռ��nc_realloc(pool->continuum
    //������ߵķ���������
    uint32_t           nlive_server;         /* # live server */
    int64_t            next_rebuild;         /* next distribution rebuild time in usec */

    /*beta:
  listen: 0.0.0.0:22122
  hash: fnv1a_64  name��ӡΪ�����beta
    */ //��ֵ��conf_pool_each_transform��������Բο�conf_commands  conf_pool
    struct string      name;                 /* pool name (ref in conf_pool) */
    struct string      addrstr;              /* pool address - hostname:port (ref in conf_pool) */
    uint16_t           port;                 /* port */
    struct sockinfo    info;                 /* listen socket info */
    mode_t             perm;                 /* socket permission */
    int                dist_type;            /* distribution type (dist_type_t) */
    int                key_hash_type;        /* key hash type (hash_type_t) */
    //hash��������hash_algos
    hash_t             key_hash;             /* key hasher */ 
    struct string      hash_tag;             /* key hash tag (ref in conf_pool) */
    //���������������timeout����λĬ����ms //�����˳�ʱѡ��᲻��Ժ�������������������������У��Ӷ��ر�����??????
    //��������ó�ʱʱ�䣬��twemproxy��redis�������쳣��twemproxy��ⲻ�������ͻ��˿��ܻ�һֱ�����ȴ�
    int                timeout;              /* timeout in msec */ //��������ã�Ĭ��Ϊ-1�����޴� CONF_DEFAULT_TIMEOUT
    int                backlog;              /* listen backlog */
    int                redis_db;             /* redis database to connect to */
    uint32_t           client_connections;   /* maximum # client connection */
    //���ÿ��server����������Ĭ��Ϊ1����������
    uint32_t           server_connections;   /* maximum # server connection */
    //��⵽������ߺ󣬹���ô��ʱ����ֿ���ʵ��ѡ��ú��
    int64_t            server_retry_timeout; /* server retry timeout in usec */
    //failure_count��server_failure_limit��ϣ���server_failure
    uint32_t           server_failure_limit; /* server failure limit */
    struct string      redis_auth;           /* redis_auth password (matches requirepass on redis) */
    //�Ƿ���Ҫ����  redis_auth ����������Ҫ
    unsigned           require_auth;         /* require_auth? */
    //��һ��booleanֵ�����ڿ���twemproxy�Ƿ�Ӧ�ø���server������״̬�ؽ�Ⱥ�����������״̬����server_failure_limit��ֵ�����ơ�  Ĭ����false��
    //�Ƿ��ڽڵ�����޷���Ӧʱ�Զ�ժ���ýڵ� ������Ч�ĵط��ں���server_failure��server_pool_update
    unsigned           auto_eject_hosts:1;   /* auto_eject_hosts? */ //Ĭ��0
    unsigned           preconnect:1;         /* preconnect? */ //�Ƿ����������������Ӻú�˷����������ǵȵ�һ�����������ںͺ�˷�������������
    unsigned           redis:1;              /* redis? */
    unsigned           tcpkeepalive:1;       /* tcpkeepalive? */ //��conf_pool_each_transform
};

void server_ref(struct conn *conn, void *owner);
void server_unref(struct conn *conn);
int server_timeout(struct conn *conn);
bool server_active(struct conn *conn);
rstatus_t server_init(struct array *server, struct array *conf_server, struct server_pool *sp);
void server_deinit(struct array *server);
struct conn *server_conn(struct server *server);
rstatus_t server_connect(struct context *ctx, struct server *server, struct conn *conn);
void server_close(struct context *ctx, struct conn *conn);
void server_connected(struct context *ctx, struct conn *conn);
void server_ok(struct context *ctx, struct conn *conn);

uint32_t server_pool_idx(struct server_pool *pool, uint8_t *key, uint32_t keylen);
struct conn *server_pool_conn(struct context *ctx, struct server_pool *pool, uint8_t *key, uint32_t keylen);
rstatus_t server_pool_run(struct server_pool *pool);
rstatus_t server_pool_preconnect(struct context *ctx);
void server_pool_disconnect(struct context *ctx);
rstatus_t server_pool_init(struct array *server_pool, struct array *conf_pool, struct context *ctx);
void server_pool_deinit(struct array *server_pool);

#endif
