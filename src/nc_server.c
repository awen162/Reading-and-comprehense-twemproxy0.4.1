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

#include <stdlib.h>
#include <unistd.h>

#include <nc_core.h>
#include <nc_server.h>
#include <nc_conf.h>

static void
server_resolve(struct server *server, struct conn *conn)
{
    rstatus_t status;

    status = nc_resolve(&server->addrstr, server->port, &server->info);
    if (status != NC_OK) {
        conn->err = EHOSTDOWN;
        conn->done = 1;
        return;
    }

    conn->family = server->info.family;
    conn->addrlen = server->info.addrlen;
    conn->addr = (struct sockaddr *)&server->info.addr;
}

//�����twemproxyΪ�����,Ҳ������proxy,��proxy���̣���Զ˾��ǿͻ��ˣ������ӵ��������ļ��е�listen�ڼ����ÿͻ��ˣ����ownerָ���server server_pool
//�������twemproxyΪ�ͻ��ˣ���server���̣���Զ�Ϊ�����ʵredis�����������ownerָ������ʵstruct server
//�ο�core_ctx_create  proxy_accept���յ��µ����ӣ���accept�����µ��׽��֣������׽��־��൱��һ��client
void
server_ref(struct conn *conn, void *owner)
{
    struct server *server = owner;

    ASSERT(!conn->client && !conn->proxy);
    ASSERT(conn->owner == NULL);

    server_resolve(server, conn); //��˷�������ַ��������conn

    server->ns_conn_q++;
    //��server���Ӷ�Ӧ��conn��ӵ�s_conn_q����
    printf("yang test xxxxxxxxxxxx 11 ccccccccccccccccccccccccccccccccccccccccccccccccc %p %p %p\r\n", 
        TAILQ_FIRST(&server->s_conn_q), server->s_conn_q.tqh_last, (&server->s_conn_q)->tqh_last);
    TAILQ_INSERT_TAIL(&server->s_conn_q, conn, conn_tqe); //conn_tqe��conn�ĵ�һ����Ա
    
    struct conn *conn_t = TAILQ_FIRST(&server->s_conn_q);
        //if(conn_t == NULL)
    printf("yang test xxxxxxxxxxxxxxxxxxxxxxxxxxxxx %p %p %p %p\r\n", conn, conn_t, server->s_conn_q.tqh_last, (&server->s_conn_q)->tqh_first);

    conn->owner = owner; //�����Ӷ�Ӧ�ĺ�˷�������ַserver

    log_debug(LOG_VVERB, "ref conn %p owner %p into '%.*s", conn, server,
              server->pname.len, server->pname.data);
}

void
server_unref(struct conn *conn)
{
    struct server *server;

    ASSERT(!conn->client && !conn->proxy);
    ASSERT(conn->owner != NULL);

    server = conn->owner;
    conn->owner = NULL;

    ASSERT(server->ns_conn_q != 0);
    server->ns_conn_q--;
    TAILQ_REMOVE(&server->s_conn_q, conn, conn_tqe);

    log_debug(LOG_VVERB, "unref conn %p owner %p from '%.*s'", conn, server,
              server->pname.len, server->pname.data);
}

//��ȡ�ͺ�˷������ĳ�ʱʱ�䣬�����ļ��е�timeout���ã�������Ĭ��-1
int
server_timeout(struct conn *conn)
{
    struct server *server;
    struct server_pool *pool;

    ASSERT(!conn->client && !conn->proxy);

    server = conn->owner;
    pool = server->owner;

    return pool->timeout;
}

bool
server_active(struct conn *conn)
{
    ASSERT(!conn->client && !conn->proxy);

    if (!TAILQ_EMPTY(&conn->imsg_q)) {
        log_debug(LOG_VVERB, "s %d is active", conn->sd);
        return true;
    }

    if (!TAILQ_EMPTY(&conn->omsg_q)) {
        log_debug(LOG_VVERB, "s %d is active", conn->sd);
        return true;
    }

    if (conn->rmsg != NULL) {
        log_debug(LOG_VVERB, "s %d is active", conn->sd);
        return true;
    }

    if (conn->smsg != NULL) {
        log_debug(LOG_VVERB, "s %d is active", conn->sd);
        return true;
    }

    log_debug(LOG_VVERB, "s %d is inactive", conn->sd);

    return false;
}

static rstatus_t
server_each_set_owner(void *elem, void *data)
{
    struct server *s = elem;
    struct server_pool *sp = data;

    s->owner = sp;

    return NC_OK;
}

//server_pool->server   conf_pool->server  server_pool
rstatus_t
server_init(struct array *server, struct array *conf_server,
            struct server_pool *sp) //conf_serverΪ�������ļ��н�������������
{
    rstatus_t status;
    uint32_t nserver;

    nserver = array_n(conf_server);
    ASSERT(nserver != 0);
    ASSERT(array_n(server) == 0);

    //��ʼ������server_pool->server
    status = array_init(server, nserver, sizeof(struct server));
    if (status != NC_OK) {
        return status;
    }

    /* transform conf server to server */ //����conf_server�����е�������ݵ�server_pool->server��
    status = array_each(conf_server, conf_server_each_transform, server);
    if (status != NC_OK) {
        server_deinit(server);
        return status;
    }
    ASSERT(array_n(server) == nserver);

    /* set server owner */
    status = array_each(server, server_each_set_owner, sp);
    if (status != NC_OK) {
        server_deinit(server);
        return status;
    }

    log_debug(LOG_DEBUG, "init %"PRIu32" servers in pool %"PRIu32" '%.*s'",
              nserver, sp->idx, sp->name.len, sp->name.data);

    return NC_OK;
}

void
server_deinit(struct array *server)
{
    uint32_t i, nserver;

    for (i = 0, nserver = array_n(server); i < nserver; i++) {
        struct server *s;

        s = array_pop(server);
        ASSERT(TAILQ_EMPTY(&s->s_conn_q) && s->ns_conn_q == 0);
    }
    array_deinit(server);
}

//Ϊ���server����������׼��������conn
struct conn *
server_conn(struct server *server)
{
    struct server_pool *pool;
    struct conn *conn;

    pool = server->owner;//�þ���ĺ��server��Ӧ�Ĵ�server,

    /*
     * FIXME: handle multiple server connections per server and do load
     * balancing on it. Support multiple algorithms for
     * 'server_connections:' > 0 key
     */

    if (server->ns_conn_q < pool->server_connections) {
        return conn_get(server, false, pool->redis);
    }
    ASSERT(server->ns_conn_q == pool->server_connections);

    /*
     * Pick a server connection from the head of the queue and insert
     * it back into the tail of queue to maintain the lru order
     */

     //��ѯ�úͺ�˵�conn����
    conn = TAILQ_FIRST(&server->s_conn_q);
    ASSERT(!conn->client && !conn->proxy);

    TAILQ_REMOVE(&server->s_conn_q, conn, conn_tqe);
    TAILQ_INSERT_TAIL(&server->s_conn_q, conn, conn_tqe);

    return conn;
}

//�����elem�Ǵ�server�ж�Ӧ��servers:������alpha: �е�servers:
static rstatus_t
server_each_preconnect(void *elem, void *data)
{ //��һ����alpha��Ӧ�Ĵ�server�е�servers:�б��еķ�������������
    rstatus_t status;
    struct server *server;
    struct server_pool *pool;
    struct conn *conn;

    server = elem;
    pool = server->owner; //���� - 127.0.0.1:6379:1 ��Ӧ�Ĵ�server������alpha

    conn = server_conn(server); //Ϊ���server����������׼��������conn
    if (conn == NULL) {
        return NC_ENOMEM;
    }

    status = server_connect(pool->ctx, server, conn);
    if (status != NC_OK) {
        log_warn("connect to server '%.*s' failed, ignored: %s",
                 server->pname.len, server->pname.data, strerror(errno));
        server_close(pool->ctx, conn);
    }

    return NC_OK;
}

static rstatus_t
server_each_disconnect(void *elem, void *data)
{
    struct server *server;
    struct server_pool *pool;

    server = elem;
    pool = server->owner;

    while (!TAILQ_EMPTY(&server->s_conn_q)) {
        struct conn *conn;

        ASSERT(server->ns_conn_q > 0);

        conn = TAILQ_FIRST(&server->s_conn_q);
        conn->close(pool->ctx, conn);
    }

    return NC_OK;
}

//�ͺ�������쳣��ʱ�������д�쳣���ߵ�������
static void
server_failure(struct context *ctx, struct server *server)
{
    struct server_pool *pool = server->owner;
    int64_t now, next;
    rstatus_t status;

    if (!pool->auto_eject_hosts) {
        return;
    }

    server->failure_count++; //ʧ�ܴ�������

    log_debug(LOG_VERB, "server '%.*s' failure count %"PRIu32" limit %"PRIu32,
              server->pname.len, server->pname.data, server->failure_count,
              pool->server_failure_limit);

    if (server->failure_count < pool->server_failure_limit) { 
    /* ���ʧ�ܴ����ﵽ����������server poolѡ�ٳ��аѸ�serverɾ�������Ϊ�ﵽ���ޣ����´λ����ܻ�ѡ����server */
        return;
    }

    //ʧ�ܴ����ﵽ���ޣ����¼���һ����hash��
    
    now = nc_usec_now();
    if (now < 0) {
        return;
    }

    stats_server_set_ts(ctx, server, server_ejected_at, now);

    next = now + pool->server_retry_timeout;

    log_debug(LOG_INFO, "update pool %"PRIu32" '%.*s' to delete server '%.*s' "
              "for next %"PRIu32" secs", pool->idx, pool->name.len,
              pool->name.data, server->pname.len, server->pname.data,
              pool->server_retry_timeout / 1000 / 1000);

    stats_pool_incr(ctx, pool, server_ejects);

    server->failure_count = 0;
    server->next_retry = next; //��server���Ϊ���ߺ��server_retry_timeout ms����Լ���ѡ���server�����Է�ֹ���ߺ������������ˣ����Ǿ���ѡ�ٲ���������

    status = server_pool_run(pool); //���¼���hash����
    if (status != NC_OK) {
        log_error("updating pool %"PRIu32" '%.*s' failed: %s", pool->idx,
                  pool->name.len, pool->name.data, strerror(errno));
    }
}

static void
server_close_stats(struct context *ctx, struct server *server, err_t err,
                   unsigned eof, unsigned connected)
{
    if (connected) {
        stats_server_decr(ctx, server, server_connections);
    }

    if (eof) {
        stats_server_incr(ctx, server, server_eof);
        return;
    }

    switch (err) {
    case ETIMEDOUT:
        stats_server_incr(ctx, server, server_timedout);
        break;
    case EPIPE:
    case ECONNRESET:
    case ECONNABORTED:
    case ECONNREFUSED:
    case ENOTCONN:
    case ENETDOWN:
    case ENETUNREACH:
    case EHOSTDOWN:
    case EHOSTUNREACH:
    default:
        stats_server_incr(ctx, server, server_err);
        break;
    }
}

void
server_close(struct context *ctx, struct conn *conn)
{
    rstatus_t status;
    struct msg *msg, *nmsg; /* current and next message */
    struct conn *c_conn;    /* peer client connection */

    ASSERT(!conn->client && !conn->proxy);

    server_close_stats(ctx, conn->owner, conn->err, conn->eof,
                       conn->connected);

    conn->connected = false;

    if (conn->sd < 0) {
        server_failure(ctx, conn->owner);
        conn->unref(conn);
        conn_put(conn);
        return;
    }

    for (msg = TAILQ_FIRST(&conn->imsg_q); msg != NULL; msg = nmsg) {
        nmsg = TAILQ_NEXT(msg, s_tqe);

        /* dequeue the message (request) from server inq */
        conn->dequeue_inq(ctx, conn, msg);

        /*
         * Don't send any error response, if
         * 1. request is tagged as noreply or,
         * 2. client has already closed its connection
         */
        if (msg->swallow || msg->noreply) {
            log_debug(LOG_INFO, "close s %d swallow req %"PRIu64" len %"PRIu32
                      " type %d", conn->sd, msg->id, msg->mlen, msg->type);
            req_put(msg);
        } else {
            c_conn = msg->owner;
            ASSERT(c_conn->client && !c_conn->proxy);

            msg->done = 1;
            msg->error = 1;
            msg->err = conn->err;

            if (msg->frag_owner != NULL) {
                msg->frag_owner->nfrag_done++;
            }

            if (req_done(c_conn, TAILQ_FIRST(&c_conn->omsg_q))) {
                event_add_out(ctx->evb, msg->owner);
            }

            log_debug(LOG_INFO, "close s %d schedule error for req %"PRIu64" "
                      "len %"PRIu32" type %d from c %d%c %s", conn->sd, msg->id,
                      msg->mlen, msg->type, c_conn->sd, conn->err ? ':' : ' ',
                      conn->err ? strerror(conn->err): " ");
        }
    }
    ASSERT(TAILQ_EMPTY(&conn->imsg_q));

    for (msg = TAILQ_FIRST(&conn->omsg_q); msg != NULL; msg = nmsg) {
        nmsg = TAILQ_NEXT(msg, s_tqe);

        /* dequeue the message (request) from server outq */
        conn->dequeue_outq(ctx, conn, msg);

        if (msg->swallow) {
            log_debug(LOG_INFO, "close s %d swallow req %"PRIu64" len %"PRIu32
                      " type %d", conn->sd, msg->id, msg->mlen, msg->type);
            req_put(msg);
        } else { //�ȴ��������Ӧ��ʱ��ʱ����ߵ�������
            c_conn = msg->owner;
            ASSERT(c_conn->client && !c_conn->proxy);

            msg->done = 1;
            msg->error = 1;
            msg->err = conn->err;
            if (msg->frag_owner != NULL) {
                msg->frag_owner->nfrag_done++;
            }

            if (req_done(c_conn, TAILQ_FIRST(&c_conn->omsg_q))) {
                event_add_out(ctx->evb, msg->owner);
            }

            log_debug(LOG_INFO, "close s %d schedule error for req %"PRIu64" "
                      "len %"PRIu32" type %d from c %d%c %s", conn->sd, msg->id,
                      msg->mlen, msg->type, c_conn->sd, conn->err ? ':' : ' ',
                      conn->err ? strerror(conn->err): " ");
        }
    }
    ASSERT(TAILQ_EMPTY(&conn->omsg_q));

    msg = conn->rmsg;
    if (msg != NULL) {
        conn->rmsg = NULL;

        ASSERT(!msg->request);
        ASSERT(msg->peer == NULL);

        rsp_put(msg);

        log_debug(LOG_INFO, "close s %d discarding rsp %"PRIu64" len %"PRIu32" "
                  "in error", conn->sd, msg->id, msg->mlen);
    }

    ASSERT(conn->smsg == NULL);

    server_failure(ctx, conn->owner);

    conn->unref(conn);

    status = close(conn->sd);
    if (status < 0) {
        log_error("close s %d failed, ignored: %s", conn->sd, strerror(errno));
    }
    conn->sd = -1;

    conn_put(conn);
}

//���Ӻ�˷�����
rstatus_t
server_connect(struct context *ctx, struct server *server, struct conn *conn)
{
    rstatus_t status;

    ASSERT(!conn->client && !conn->proxy);

    if (conn->err) {
      ASSERT(conn->done && conn->sd < 0);
      errno = conn->err;
      return NC_ERROR;
    }

    if (conn->sd > 0) {
        /* already connected on server connection */
        return NC_OK;
    }

    log_debug(LOG_VVERB, "connect to server '%.*s'", server->pname.len,
              server->pname.data);

    conn->sd = socket(conn->family, SOCK_STREAM, 0);
    if (conn->sd < 0) {
        log_error("socket for server '%.*s' failed: %s", server->pname.len,
                  server->pname.data, strerror(errno));
        status = NC_ERROR;
        goto error;
    }

    status = nc_set_nonblocking(conn->sd);
    if (status != NC_OK) {
        log_error("set nonblock on s %d for server '%.*s' failed: %s",
                  conn->sd, server->pname.len, server->pname.data,
                  strerror(errno));
        goto error;
    }

    if (server->pname.data[0] != '/') { //�����׽��ַ�ʽ
        status = nc_set_tcpnodelay(conn->sd);
        if (status != NC_OK) {
            log_warn("set tcpnodelay on s %d for server '%.*s' failed, ignored: %s",
                     conn->sd, server->pname.len, server->pname.data,
                     strerror(errno));
        }
    }

    status = event_add_conn(ctx->evb, conn); //��д�¼����������ط�ʽ
    if (status != NC_OK) {
        log_error("event add conn s %d for server '%.*s' failed: %s",
                  conn->sd, server->pname.len, server->pname.data,
                  strerror(errno));
        goto error;
    }

    ASSERT(!conn->connecting && !conn->connected);

    status = connect(conn->sd, conn->addr, conn->addrlen);
    if (status != NC_OK) {
        if (errno == EINPROGRESS) { //���ڽ������ӣ�ͨ��epoll�����¼����  ���ӳɹ����ߵ�����server_connected
            conn->connecting = 1;
            log_debug(LOG_DEBUG, "connecting on s %d to server '%.*s'",
                      conn->sd, server->pname.len, server->pname.data);
            return NC_OK;
        }

        log_error("connect on s %d to server '%.*s' failed: %s", conn->sd,
                  server->pname.len, server->pname.data, strerror(errno));

        goto error;
    }

    //���ӳɹ�
    ASSERT(!conn->connecting);
    conn->connected = 1;
    log_debug(LOG_INFO, "connected on s %d to server '%.*s'", conn->sd,
              server->pname.len, server->pname.data);

    return NC_OK;

error:
    conn->err = errno;
    return status;
}

void
server_connected(struct context *ctx, struct conn *conn)
{
    struct server *server = conn->owner;

    ASSERT(!conn->client && !conn->proxy);
    ASSERT(conn->connecting && !conn->connected);

    stats_server_incr(ctx, server, server_connections);

    conn->connecting = 0;
    conn->connected = 1;

    conn->post_connect(ctx, conn, server);

    log_debug(LOG_INFO, "connected on s %d to server '%.*s'", conn->sd,
              server->pname.len, server->pname.data);
}

void
server_ok(struct context *ctx, struct conn *conn)
{
    struct server *server = conn->owner;

    ASSERT(!conn->client && !conn->proxy);
    ASSERT(conn->connected);

    //��server���Ϊ���ߺ��server_retry_timeout ms����Լ���ѡ���server�����Է�ֹ���ߺ������������ˣ����Ǿ���ѡ�ٲ���������
    if (server->failure_count != 0) { //֮ǰ��server�Ѿ������ˣ����ڴ������ߺ�˵�����Լ���ʹ���ˣ����failure_count
        log_debug(LOG_VERB, "reset server '%.*s' failure count from %"PRIu32
                  " to 0", server->pname.len, server->pname.data,
                  server->failure_count);
        server->failure_count = 0; //�÷����������ˣ��Ѵ��������0
        server->next_retry = 0LL;
    }
}

static rstatus_t
server_pool_update(struct server_pool *pool)
{
    rstatus_t status;
    int64_t now;
    uint32_t pnlive_server; /* prev # live server */

    if (!pool->auto_eject_hosts) {
        return NC_OK;
    }

    /* ��Ҫ���½���һ����hash��ʼ����������ѡ�����ߵ�server */

    
    if (pool->next_rebuild == 0LL) {
        return NC_OK;
    }

    now = nc_usec_now();
    if (now < 0) {
        return NC_ERROR;
    }

    if (now <= pool->next_rebuild) {
        if (pool->nlive_server == 0) {
            errno = ECONNREFUSED;
            return NC_ERROR;
        }
        return NC_OK;
    }

    pnlive_server = pool->nlive_server;

    status = server_pool_run(pool);
    if (status != NC_OK) {
        log_error("updating pool %"PRIu32" with dist %d failed: %s", pool->idx,
                  pool->dist_type, strerror(errno));
        return status;
    }

    log_debug(LOG_INFO, "update pool %"PRIu32" '%.*s' to add %"PRIu32" servers",
              pool->idx, pool->name.len, pool->name.data,
              pool->nlive_server - pnlive_server);


    return NC_OK;
}

static uint32_t
server_pool_hash(struct server_pool *pool, uint8_t *key, uint32_t keylen)
{
    ASSERT(array_n(&pool->server) != 0);
    ASSERT(key != NULL);

    if (array_n(&pool->server) == 1) {
        return 0;
    }

    if (keylen == 0) {
        return 0;
    }

    return pool->key_hash((char *)key, keylen); //YANG ADD XXXXXXXXXX TODO������������Ҫ��Ϊ��redisһ��
}

//����keyѡ�ٺ��ָ��idx�ķ�����     memcached��slot����
uint32_t
server_pool_idx(struct server_pool *pool, uint8_t *key, uint32_t keylen)
{
    uint32_t hash, idx;

    ASSERT(array_n(&pool->server) != 0);
    ASSERT(key != NULL);

    /*
     * If hash_tag: is configured for this server pool, we use the part of
     * the key within the hash tag as an input to the distributor. Otherwise
     * we use the full key
     */
    if (!string_empty(&pool->hash_tag)) {
        struct string *tag = &pool->hash_tag;
        uint8_t *tag_start, *tag_end;

        tag_start = nc_strchr(key, key + keylen, tag->data[0]);
        if (tag_start != NULL) {
            tag_end = nc_strchr(tag_start + 1, key + keylen, tag->data[1]);
            if ((tag_end != NULL) && (tag_end - tag_start > 1)) {
                key = tag_start + 1;
                keylen = (uint32_t)(tag_end - key);
            }
        }
    }

    switch (pool->dist_type) {
    case DIST_KETAMA:
        hash = server_pool_hash(pool, key, keylen);
        idx = ketama_dispatch(pool->continuum, pool->ncontinuum, hash);
        break;

    case DIST_MODULA:
        hash = server_pool_hash(pool, key, keylen);
        idx = modula_dispatch(pool->continuum, pool->ncontinuum, hash);
        break;

    case DIST_RANDOM:
        idx = random_dispatch(pool->continuum, pool->ncontinuum, 0);
        break;

    default:
        NOT_REACHED();
        return 0;
    }
    ASSERT(idx < array_n(&pool->server));
    return idx;
}

//server_pool_server����ѡ�ٺ�˷�������ketama_updateΪһ����hash��ص�ketama�㷨
//����keyѡ�ٺ�˷�����
static struct server *
server_pool_server(struct server_pool *pool, uint8_t *key, uint32_t keylen)
{
    struct server *server;
    uint32_t idx;

    idx = server_pool_idx(pool, key, keylen);
    server = array_get(&pool->server, idx);

    log_debug(LOG_VERB, "key '%.*s' on dist %d maps to server '%.*s'", keylen,
              key, pool->dist_type, server->pname.len, server->pname.data);

    return server;
}

struct conn *
server_pool_conn(struct context *ctx, struct server_pool *pool, uint8_t *key,
                 uint32_t keylen)
{//ѡ�ٺ�˷���������������
    rstatus_t status;
    struct server *server;
    struct conn *conn;

    status = server_pool_update(pool);
    if (status != NC_OK) {
        return NULL;
    }

    /* from a given {key, keylen} pick a server from pool */
    server = server_pool_server(pool, key, keylen);//ѡ�ٺ�˷�����
    if (server == NULL) {
        return NULL;
    }

    /* pick a connection to a given server */
    conn = server_conn(server);  //Ϊѡ�ٳ��ĺ��server����������׼��������conn
    if (conn == NULL) {
        return NULL;
    }

    status = server_connect(ctx, server, conn); //��������  ���֮ǰ�Լ��������ӣ�ֱ�ӷ���OK��������connect
    if (status != NC_OK) {
        server_close(ctx, conn);
        return NULL;
    }

    return conn;
}

static rstatus_t
server_pool_each_preconnect(void *elem, void *data)
{
    rstatus_t status;
    struct server_pool *sp = elem;

    if (!sp->preconnect) {
        return NC_OK;
    }

    status = array_each(&sp->server, server_each_preconnect, NULL);
    if (status != NC_OK) {
        return status;
    }

    return NC_OK;
}



rstatus_t
server_pool_preconnect(struct context *ctx)
{
    rstatus_t status;

    status = array_each(&ctx->pool, server_pool_each_preconnect, NULL);
    if (status != NC_OK) {
        return status;
    }

    return NC_OK;
}

static rstatus_t
server_pool_each_disconnect(void *elem, void *data)
{
    rstatus_t status;
    struct server_pool *sp = elem;

    status = array_each(&sp->server, server_each_disconnect, NULL);
    if (status != NC_OK) {
        return status;
    }

    return NC_OK;
}

void
server_pool_disconnect(struct context *ctx)
{
    array_each(&ctx->pool, server_pool_each_disconnect, NULL);
}

static rstatus_t
server_pool_each_set_owner(void *elem, void *data)
{//���server_pool������context������
    struct server_pool *sp = elem;
    struct context *ctx = data;

    sp->ctx = ctx;

    return NC_OK;
}

//ÿ��server���Դ�sp->server_connections�����ӣ�n��server���Դ�n*server_connections�����ӣ��ڼ���listenһ����һ��������ô���
static rstatus_t
server_pool_each_calc_connections(void *elem, void *data)
{
    struct server_pool *sp = elem;
    struct context *ctx = data;

    ctx->max_nsconn += sp->server_connections * array_n(&sp->server);
    ctx->max_nsconn += 1; /* pool listening socket */

    return NC_OK;
}

/*
distribution  ����ketama��modula��random3�ֿ�ѡ�����á��京�����£�  
ketama  ketamaһ����hash�㷨������ݷ����������һ��hash ring����Ϊring�ϵĽڵ����hash��Χ��ketama��
    �������ڵ����ڵ���ӡ�ɾ��֮�󣬻����̶��ϱ�������Ⱥ���л����keyֵ���Ա����á�  
modula  modula�ǳ��򵥣����Ǹ���keyֵ��hashֵȡģ������ȡģ�Ľ��ѡ���Ӧ�ķ�������  
random  random������keyֵ��hash��ʲô���������ѡ��һ����������Ϊkeyֵ������Ŀ�ꡣ
*/
rstatus_t
server_pool_run(struct server_pool *pool)
{
    ASSERT(array_n(&pool->server) != 0);

    switch (pool->dist_type) { //
    case DIST_KETAMA:
        return ketama_update(pool);

    case DIST_MODULA:
        return modula_update(pool);

    case DIST_RANDOM:
        return random_update(pool);

    default:
        NOT_REACHED();
        return NC_ERROR;
    }

    return NC_OK;
}

static rstatus_t
server_pool_each_run(void *elem, void *data)
{
    return server_pool_run(elem);
}
 
//conf_poolΪ�������ļ��н��������������pool        �����������ļ��н�������������õ�server_pool�У�ͬʱ����������ȷ���ж�
rstatus_t
server_pool_init(struct array *server_pool, struct array *conf_pool,
                 struct context *ctx)
{
    rstatus_t status;
    uint32_t npool;

    npool = array_n(conf_pool);
    ASSERT(npool != 0);
    ASSERT(array_n(server_pool) == 0);


    //Ϊcontext->pool�����ռ�͸�ֵ
    status = array_init(server_pool, npool, sizeof(struct server_pool));
    if (status != NC_OK) {
        return status;
    }

    /* transform conf pool to server pool */
    //��conf pool�е������Ϣȫ��������context pool��
    status = array_each(conf_pool, conf_pool_each_transform, server_pool);
    if (status != NC_OK) {
        server_pool_deinit(server_pool);
        return status;
    }
    ASSERT(array_n(server_pool) == npool);

    /* set ctx as the server pool owner */
    //���server_pool������context������
    status = array_each(server_pool, server_pool_each_set_owner, ctx);
    if (status != NC_OK) {
        server_pool_deinit(server_pool);
        return status;
    }

    /* compute max server connections */
    ctx->max_nsconn = 0; //������Ҫ�������ٸ�sock
    status = array_each(server_pool, server_pool_each_calc_connections, ctx);
    if (status != NC_OK) {
        server_pool_deinit(server_pool);
        return status;
    }

    /* update server pool continuum */
    status = array_each(server_pool, server_pool_each_run, NULL);
    if (status != NC_OK) {
        server_pool_deinit(server_pool);
        return status;
    }

    log_debug(LOG_DEBUG, "init %"PRIu32" pools", npool);

    return NC_OK;
}

void
server_pool_deinit(struct array *server_pool)
{
    uint32_t i, npool;

    for (i = 0, npool = array_n(server_pool); i < npool; i++) {
        struct server_pool *sp;

        sp = array_pop(server_pool);
        ASSERT(sp->p_conn == NULL);
        ASSERT(TAILQ_EMPTY(&sp->c_conn_q) && sp->nc_conn_q == 0);

        if (sp->continuum != NULL) {
            nc_free(sp->continuum);
            sp->ncontinuum = 0;
            sp->nserver_continuum = 0;
            sp->nlive_server = 0;
        }

        server_deinit(&sp->server);

        log_debug(LOG_DEBUG, "deinit pool %"PRIu32" '%.*s'", sp->idx,
                  sp->name.len, sp->name.data);
    }

    array_deinit(server_pool);

    log_debug(LOG_DEBUG, "deinit %"PRIu32" pools", npool);
}
