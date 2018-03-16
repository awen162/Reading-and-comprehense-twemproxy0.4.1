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

#ifndef _NC_CONNECTION_H_
#define _NC_CONNECTION_H_

#include <nc_core.h>

typedef rstatus_t (*conn_recv_t)(struct context *, struct conn*);
typedef struct msg* (*conn_recv_next_t)(struct context *, struct conn *, bool);
typedef void (*conn_recv_done_t)(struct context *, struct conn *, struct msg *, struct msg *);

typedef rstatus_t (*conn_send_t)(struct context *, struct conn*);
typedef struct msg* (*conn_send_next_t)(struct context *, struct conn *);
typedef void (*conn_send_done_t)(struct context *, struct conn *, struct msg *);

typedef void (*conn_close_t)(struct context *, struct conn *);
typedef bool (*conn_active_t)(struct conn *);

typedef void (*conn_ref_t)(struct conn *, void *);
typedef void (*conn_unref_t)(struct conn *);

typedef void (*conn_msgq_t)(struct context *, struct conn *, struct msg *);
typedef void (*conn_post_connect_t)(struct context *ctx, struct conn *, struct server *server);
typedef void (*conn_swallow_msg_t)(struct conn *, struct msg *, struct msg *);

//�����ռ�͸�ֵ��_conn_get
struct conn { //���ͻ��˺ͷ���˶�Ӧ����ػص���conn_get
    TAILQ_ENTRY(conn)   conn_tqe;        /* link in server_pool / server / free q */
    //client_ref  server_ref  proxy_ref�и�ֵ  
    //�����twemproxyΪ�����,Ҳ������proxy,��proxy���̣���Զ˾��ǿͻ��ˣ������ӵ��������ļ��е�listen�ڼ����ÿͻ��ˣ����ownerָ���server server_pool
//�������twemproxyΪ�ͻ��ˣ���server���̣���Զ�Ϊ�����ʵredis�����������ownerָ������ʵstruct server
//client_ref  proxy_ref  server_ref�и�ֵ
    void                *owner;          /* connection owner - server_pool / server */

    //�ͺ�˷�������������������server_connect  ������ˣ��Դ����ǿͻ��ˣ���ʱ���ʾbind����˵�fd����proxy_listen
    int                 sd;              /* socket descriptor */

    //��ֵ��server_resolve  
    int                 family;          /* socket address family */
    socklen_t           addrlen;         /* socket length */
    struct sockaddr     *addr;           /* socket address (ref in server or server_pool) */

    /* enqueue_inq�����ѽ��յ��Ŀͻ���KV��Ϣmsg��ӵ�c_conn->imsg_q������msg���͵���˷�������dequeue_inq�Ѹ�msg��c_conn->imsg_q��ժ����
     Ȼ��ֱ���req_forward��req_send_doneͨ��enqueue_outq�Ѹ�msg��ӵ�c_conn->omsg_q��s_conn->omsg_q�еȴ����Ӧ���msg��Ӧ��ack��
     ���Ӧ���ᴴ��һ���µ�msg���պ��Ӧ����Ϣ����ͨ��rsp_forward��rsp_send_done�ֱ��dequeue_outq�Ѹ�msgժ����Ȼ��ͨ��msg_put
     �黹��free_msg��ͬʱ��rsp_forward�аѺ��Ӧ���msg�Ͷ�ȡ�ͻ�����Ϣ��msg�����������Ӷ����԰Ѻ�����ݷ��͸��ͻ���
     
     ���ͳ����Ϣ�е�in_queue����ľ���imsg_q�е�msg��Ϣ����ʾ��û�з��͵���˷�������msg��Ϣ��ͳ����Ϣ�е�out_queue����ľ���
     omsg_q�е�msg��Ϣ����ʾmsg�Ѿ����͵���˷��������ǻ�û�еõ���˷�����Ӧ����Ϣ��msg, 
     */

    //req_server_enqueue_imsgq��ӵ�����β����req_server_enqueue_imsgq_head��ӵ�����ͷ��  req_server_dequeue_imsgq�Ƴ�
    //���յĿͻ���msg��Ϣͨ��req_server_enqueue_imsgq��ӵ��ö��� ��req_send_next��ȡ�����������ʵ������  ����д�¼���req_forward->event_add_out�����
    //imsg_q���м�¼���ǽ��յ��Ŀͻ��˻�û�з��͵���˷�������msg��Ϣ�������͵���˷������󣬻��imsq_qȡ��Ȼ����ӵ�omsg_q��omsq_q���ڵȴ�������msg��Ӧ��Ӧ����Ϣ
    struct msg_tqh      imsg_q;          /* incoming request Q */ //��core_core�е�д�¼���imsg_q�е�msg���ͳ�ȥ
    //���Բο�req_client_enqueue_omsgq
    //imsg_q���м�¼���ǽ��յ��Ŀͻ��˻�û�з��͵���˷�������msg��Ϣ�������͵���˷������󣬻��imsq_qȡ��Ȼ����ӵ�omsg_q��omsq_q���ڵȴ�������msg��Ӧ��Ӧ����Ϣ
    //req_client_enqueue_omsgq   req_server_enqueue_omsgq�����
    struct msg_tqh      omsg_q;          /* outstanding request Q */
    //�ϴν�����������δ������ɵĲ�����rmgs�ݴ�����(����һ��KV���󣬿ͻ��˷�����һ���ֹ��������´οͻ����ٴη��͵�ʱ����ʹ�ø�rmsg����)��rmsg���ǽ������������ݵ�msg  
    //��ȡ��˷�����������rsp_recv_next rsp_recv_done�и�ֵ����ȡ�ͻ���������req_recv_next��req_recv_done�и�ֵ
    //˵��֮ǰĳ��KV����û��ȡ��ϣ���˾Ͳ��������ת������λ���ʹ�ø�msg���ж�ȡ���������ݣ���req_recv_next
    //req_recv_next �� req_recv_done�и�ֵ��ΪNULL��˵��������������KV����ΪNULL��˵������Ҫ�ȴ����������һ������KV
    struct msg          *rmsg;           /* current message being rcvd */
    //�����ɹ������ݴ浽��smsg�У���Ҫ���������ʵ����������req_send_next
    //req_send_next��rsp_send_next�и�ֵ
    struct msg          *smsg;           /* current message being sent */

    //msg_recv���� proxy_recv
    conn_recv_t         recv;            /* recv (read) handler */
    /* req_recv_next; rsp_recv_next; */
    conn_recv_next_t    recv_next;       /* recv next message handler */
    //req_recv_done  rsp_recv_done             msg_parsed��ִ��
    conn_recv_done_t    recv_done;       /* read done handler */
    //msg_send   core_send��ִ��
    conn_send_t         send;            /* send (write) handler */
    //�����ͻ�����rsp_send_next ������˷�������req_send_next    msg_send����msg_send_chain��ִ��
    conn_send_next_t    send_next;       /* write next message handler */
    //req_send_done  rsp_send_done  msg_send_chain��ִ��
    conn_send_done_t    send_done;       /* write done handler */
    //client_close  server_close  proxy_close
    conn_close_t        close;           /* close handler */ //�ڷ��������쳣��ʱ�򣬻���core_close��ִ�иú�������
    //client_active  server_active
    conn_active_t       active;          /* active? handler */
    //redis_post_connect����memcache_post_connect
    conn_post_connect_t post_connect;    /* post connect handler */
    //redis_swallow_msg  memcache_swallow_msg
    conn_swallow_msg_t  swallow_msg;     /* react on messages to be swallowed */

    //server_ref client_ref proxy_ref  conn_get��ִ�и�ref����
    conn_ref_t          ref;             /* connection reference handler */
    //client_unref server_unref proxy_unref
    conn_unref_t        unref;           /* connection unreference handler */

    /* enqueue_inq�����ѽ��յ��Ŀͻ���KV��Ϣmsg��ӵ�c_conn->imsg_q������msg���͵���˷�������dequeue_inq�Ѹ�msg��c_conn->imsg_q��ժ����
     Ȼ��ֱ���req_forward��req_send_doneͨ��enqueue_outq�Ѹ�msg��ӵ�c_conn->omsg_q��s_conn->omsg_q�еȴ����Ӧ���msg��Ӧ��ack��
     ���Ӧ���ᴴ��һ���µ�msg���պ��Ӧ����Ϣ����ͨ��rsp_forward��rsp_send_doneͨ��dequeue_outq�Ѹ�msgժ����Ȼ��ͨ��msg_put
     �黹��free_msg��ͬʱ��rsp_forward�аѺ��Ӧ���msg�Ͷ�ȡ�ͻ�����Ϣ��msg�����������Ӷ����԰Ѻ�����ݷ��͸��ͻ���
     
     ���ͳ����Ϣ�е�in_queue����ľ���imsg_q�е�msg��Ϣ����ʾ��û�з��͵���˷�������msg��Ϣ��ͳ����Ϣ�е�out_queue����ľ���
     omsg_q�е�msg��Ϣ����ʾmsg�Ѿ����͵���˷��������ǻ�û�еõ���˷�����Ӧ����Ϣ��msg, 
     */
    //req_forward��ִ��  req_server_enqueue_imsgq       req_forward����redis_add_auth��ִ��
    conn_msgq_t         enqueue_inq;     /* connection inq msg enqueue handler */
    //req_server_dequeue_imsgq  ��req_send_done�Ѹö��е�msg���͵���˷�����
    conn_msgq_t         dequeue_inq;     /* connection inq msg dequeue handler */

    /* enqueue_inq�����ѽ��յ��Ŀͻ���KV��Ϣmsg��ӵ�c_conn->imsg_q������msg���͵���˷�������dequeue_inq�Ѹ�msg��c_conn->imsg_q��ժ����
     Ȼ��ֱ���req_forward��req_send_doneͨ��enqueue_outq�Ѹ�msg��ӵ�c_conn->omsg_q��s_conn->omsg_q�еȴ����Ӧ���msg��Ӧ��ack��
     ���Ӧ���ᴴ��һ���µ�msg���պ��Ӧ����Ϣ����ͨ��rsp_forward��rsp_send_doneͨ��dequeue_outq�Ѹ�msgժ����Ȼ��ͨ��msg_put
     �黹��free_msg��ͬʱ��rsp_forward�аѺ��Ӧ���msg�Ͷ�ȡ�ͻ�����Ϣ��msg�����������Ӷ����԰Ѻ�����ݷ��͸��ͻ���
     
     ���ͳ����Ϣ�е�in_queue����ľ���imsg_q�е�msg��Ϣ����ʾ��û�з��͵���˷�������msg��Ϣ��ͳ����Ϣ�е�out_queue����ľ���
     omsg_q�е�msg��Ϣ����ʾmsg�Ѿ����͵���˷��������ǻ�û�еõ���˷�����Ӧ����Ϣ��msg, 
     */
    
    //req_client_enqueue_omsgq  req_server_enqueue_omsgq�����  req_send_done�л����       enqueue_outq��ӣ�dequeue_outp����
    //enquee_outq��Ŀ��Ӧ���Ǽ�¼���͵���˵�����Ⱥ��Ӧ����֪���Ƕ����������Ӧ��
    conn_msgq_t         enqueue_outq;    /* connection outq msg enqueue handler */
    //enqueue_outq��ӣ�dequeue_outp����  ȡֵΪreq_client_dequeue_omsgq req_server_dequeue_omsgq  rsp_send_done��ִ�� rsp_forward��ִ��
    //rsp_send_done�ӿͻ�������conn->dequeue_outq�г���  rsp_forward�ӷ��������s_conn->dequeue_outq�г���
    conn_msgq_t         dequeue_outq;    /* connection outq msg dequeue handler */

    size_t              recv_bytes;      /* received (read) bytes */
    size_t              send_bytes;      /* sent (written) bytes */

    uint32_t            events;          /* connection io events */
    //����Ǻ��Ӧ��ʱ����ֵΪETIMEDOUT����core_timeout  
    //err��1������core_core�л�ر�����
    //eof�������Ĵ���close���̣�����quit��ʱ��;��err��1��ʾ�������쳣����Ҫ����close����
    err_t               err;             /* connection errno */ 
    //proxy���տͻ������ӣ���event_add_conn��Ӷ��¼��� client conn��event_add_in����¼�
    unsigned            recv_active:1;   /* recv active? */ //��¼�Ƿ��Ѿ��Ѹ�conn���¼���ӵ�epoll
    //��ʾepoll��⵽read�¼��������ݿɶ������пͻ�������  ÿ�ζ�ȡ������ϣ���0����ʾ���ݶ�ȡ��� 
    //proxy_accept�а����пͻ������Ӷ�������Ϻ����recv_readyΪ0   ��������Ӻ�Ķ��¼������ʾ�����������ݵ���
    //��ȡ��Э��ջ���������ݺ󣬱�ʾЭ��ջ�����Ѿ���ȡ��ϣ����recv_ready��0����conn_recv
    unsigned            recv_ready:1;    /* recv ready? */ //��¼�Ƿ��Ѿ���дconn���¼���ӵ�epoll  Ϊ1˵�������ݵ���
    unsigned            send_active:1;   /* send active? */
    //msg_send����1
    unsigned            send_ready:1;    /* send ready? */

    //�������ǿͻ��˻��Ƿ����������Ϊ�ͻ�����Ϊ1������Ϊ0  �������accept���غ󣬻᷵��һ���µ��׽��֣����׽��ֶ�Ӧ��conn��clientΪ1
    unsigned            client:1;        /* client? or server? */
    //��ʶ��connΪproxy���ӣ��ȴ����տͻ������ӣ������յ��µĿͻ������Ӻ󣬻᷵���µ�fd���µ�conn
    unsigned            proxy:1;         /* proxy? */ //Ϊ1˵���ǽ��տͻ������ӵ�bind��Ӧ���׽���,��conn_get_proxy
    //server_connect��connectֱ�ӷ��ص���û�н������ӳɹ�  req_send_next->server_connected�жϳ����ӽ����ɹ�
    unsigned            connecting:1;    /* connecting? */ //���ڽ���3�����ֹ����� 
    unsigned            connected:1;     /* connected? */ //connect�ɹ�  ���Բο�server_connected
    //��1��ʾ��Ҫ�ر��׽��֣����Բο�req_filter   ����req_filter�ͻ��˷���quit,���ߺ�˽��̹��ˣ���������conn_recv��eof rsp_recv_next�йر����� 
    //eofΪ1�����ݷ����ʱ���ְλdone��ǣ���core_core�м�⵽done��ǻ�ر����ӣ���rsp_send_next
    //eof�������Ĵ���close���̣�����quit��ʱ��;��err��1��ʾ�������쳣����Ҫ����close����
    unsigned            eof:1;           /* eof? aka passive close? */
    //���eofΪ1�����ݷ�����߽������ʱ���ְλdone��ǣ���core_core�м�⵽done��ǻ�ر����ӣ���rsp_send_next  req_recv_next
    //req_recv_next�л��鷢���ͻ��˶����ϵ������Ƿ�����ɣ�ֻ�з�����ɺ�Ż���doneΪ1 ��Ȼ����core_core�йر�����

    //�����ֱ�Ӱ�conn->err��λerrno���������ر�����
    unsigned            done:1;          /* done? aka close? */
    //redisΪ1����redis������Ϊ0
    unsigned            redis:1;         /* redis? */
    //�Ƿ��Ѿ�����ɹ�
    unsigned            authenticated:1; /* authenticated? */
};

TAILQ_HEAD(conn_tqh, conn);

struct context *conn_to_ctx(struct conn *conn);
struct conn *conn_get(void *owner, bool client, bool redis);
struct conn *conn_get_proxy(void *owner);
void conn_put(struct conn *conn);
ssize_t conn_recv(struct conn *conn, void *buf, size_t size);
ssize_t conn_sendv(struct conn *conn, struct array *sendv, size_t nsend);
void conn_init(void);
void conn_deinit(void);
uint32_t conn_ncurr_conn(void);
uint64_t conn_ntotal_conn(void);
uint32_t conn_ncurr_cconn(void);
bool conn_authenticated(struct conn *conn);
uint32_t conn_ncurr_conn_zero(void);
uint64_t conn_ntotal_conn_zero(void);

#endif
