#include "tju_tcp.h"

/*
创建 TCP socket 
初始化对应的结构体
设置初始状态为 CLOSED
*/
tju_tcp_t* tju_socket(){
    tju_tcp_t* sock = (tju_tcp_t*)malloc(sizeof(tju_tcp_t));
    sock->state = CLOSED;
    
    pthread_mutex_init(&(sock->send_lock), NULL);
    sock->sending_buf = NULL;
    sock->sending_len = 0;

    pthread_mutex_init(&(sock->recv_lock), NULL);
    sock->received_buf = NULL;
    sock->received_len = 0;
    
    if(pthread_cond_init(&sock->wait_cond, NULL) != 0){
        perror("ERROR condition variable not set\n");
        exit(-1);
    }

    sock->window.wnd_send = NULL;
    sock->window.wnd_recv = NULL;

    return sock;
}

/*
绑定监听的地址 包括ip和端口
*/
int tju_bind(tju_tcp_t* sock, tju_sock_addr bind_addr){
    sock->bind_addr = bind_addr;
    return 0;
}

/*
被动打开 监听bind的地址和端口
设置socket的状态为LISTEN
注册该socket到内核的监听socket哈希表
*/
int tju_listen(tju_tcp_t* sock){
       sock->state = LISTEN;
        pigsocket_queue.head=(node*)malloc(sizeof(node*)); //初始化连接队列的头结点
        pigsocket_queue.head->aasocket=NULL;
        pigsocket_queue.head->next=NULL;
        pigsocket_queue.tail=pigsocket_queue.head;
    
    int hashval = cal_hash(sock->bind_addr.ip, sock->bind_addr.port, 0, 0);
    listen_socks[hashval] = sock;
    return 0;
}

/*
接受连接 
返回与客户端通信用的socket
这里返回的socket一定是已经完成3次握手建立了连接的socket
因为只要该函数返回, 用户就可以马上使用该socket进行send和recv
*/
tju_tcp_t* tju_accept(tju_tcp_t* listen_sock){
    tju_tcp_t* new_conn = (tju_tcp_t*)malloc(sizeof(tju_tcp_t));
   // memcpy(new_conn, listen_sock, sizeof(tju_tcp_t));

   
    /*
     这里涉及到TCP连接的建立
     正常来说应该是收到客户端发来的SYN报文
     从中拿到对端的IP和PORT
     换句话说 下面的处理流程其实不应该放在这里 应该在tju_handle_packet中
    */ 
    while(pigsocket_queue.head->next==NULL);   //半连接队列为空则一直阻塞

    node* new_socket=pigsocket_queue.head->next;
    new_conn=new_socket->aasocket;
    pigsocket_queue.head->next=pigsocket_queue.head->next->next;
    if(pigsocket_queue.tail->next==new_socket){
        pigsocket_queue.tail=pigsocket_queue.head;
    }
    free(new_socket);
    // 这里应该是经过三次握手后才能修改状态为ESTABLISHED
    // 将新的conn放到内核建立连接的socket哈希表中
   
    // 如果new_conn的创建过程放到了tju_handle_packet中 那么accept怎么拿到这个new_conn呢
    // 在linux中 每个listen socket都维护一个已经完成连接的socket队列
    // 每次调用accept 实际上就是取出这个队列中的一个元素
    // 队列为空,则阻塞 
    return new_conn;
}


/*
连接到服务端
该函数以一个socket为参数
调用函数前, 该socket还未建立连接
函数正常返回后,    该socket一定是已经完成了3次握手, 建立了连接
因为只要该函数返回, 用户就可以马上使用该socket进行send和recv
*/
int tju_connect(tju_tcp_t* sock, tju_sock_addr target_addr){

    sock->established_remote_addr = target_addr;

    tju_sock_addr local_addr;
    local_addr.ip = inet_network("172.17.0.2");
    local_addr.port = 5678; // 连接方进行connect连接的时候 内核中是随机分配一个可用的端口
    sock->established_local_addr = local_addr;  
  

   
/*add codes*/
//客户端发送第一次建立连接报文
  char* msg1;
  msg1=create_packet_buf(local_addr.port,target_addr.port,0,0,DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN,(uint8_t)SYN_FLAG_MASK,1,0,NULL,0);
  sendToLayer3(msg1,DEFAULT_HEADER_LEN);
  printf("send SYN !\n");
  sock->state=SYN_SENT;
  int hashval = cal_hash(local_addr.ip, local_addr.port, target_addr.ip, target_addr.port);
  established_socks[hashval] = sock;
  //starttimer();
  while(sock->state!=ESTABLISHED);
   return 0;


    // 这里也不能直接建立连接 需要经过三次握手
    // 实际在linux中 connect调用后 会进入一个while循环
    // 循环跳出的条件是socket的状态变为ESTABLISHED 表面看上去就是 正在连接中 阻塞
    // 而状态的改变在别的地方进行 在我们这就是tju_handle_packet
    //sock->state = ESTABLISHED;

    // 将建立了连接的socket放入内核 已建立连接哈希表中
    
}

int tju_send(tju_tcp_t* sock, const void *buffer, int len){
    // 这里当然不能直接简单地调用sendToLayer35
    char* data = malloc(len);
    memcpy(data, buffer, len);

    char* msg;
    uint32_t seq = 464;
    uint16_t plen = DEFAULT_HEADER_LEN + len;

    msg = create_packet_buf(sock->established_local_addr.port, sock->established_remote_addr.port, seq, 0, 
              DEFAULT_HEADER_LEN, plen, NO_FLAG, 1, 0, data, len);

    sendToLayer3(msg, plen);
    
    return 0;
}


int tju_recv(tju_tcp_t* sock, void *buffer, int len){
    while(sock->received_len<=0){
        // 阻塞
    }

    while(pthread_mutex_lock(&(sock->recv_lock)) != 0); // 加锁

    int read_len = 0;
    if (sock->received_len >= len){ // 从中读取len长度的数据
        read_len = len;
    }else{
        read_len = sock->received_len; // 读取sock->received_len长度的数据(全读出来)
    }

    memcpy(buffer, sock->received_buf, read_len);

    if(read_len < sock->received_len) { // 还剩下一些
        char* new_buf = malloc(sock->received_len - read_len);
        memcpy(new_buf, sock->received_buf + read_len, sock->received_len - read_len);
        free(sock->received_buf);
        sock->received_len -= read_len;
        sock->received_buf = new_buf;
    }else{
        free(sock->received_buf);
        sock->received_buf = NULL;
        sock->received_len = 0;
    }
    pthread_mutex_unlock(&(sock->recv_lock)); // 解锁

    return 0;
}




int tju_handle_packet(tju_tcp_t* sock, char* pkt){
    uint16_t src_port=get_src(pkt);		//2 bytes 源端口
	uint16_t des_port=get_dst(pkt);	//2 bytes 目的端口
	uint32_t seq=get_seq(pkt); 			//4 bytes sequence number
	uint32_t ack=get_ack(pkt); 			//4 bytes ack number
	uint16_t hlen=get_hlen(pkt);				//2 bytes 包头长 这个项目里全是20
	uint16_t plen=get_plen(pkt);				//2 bytes 包总长 包括包头和包携带的数据 20+数据长度 注意总长度不能超过MAX_LEN(1400) 防止IP层分片
	uint8_t flag=get_flags(pkt);				//1 byte  标志位 比如 SYN FIN ACK 等
	uint16_t adv_window=get_advertised_window(pkt); //2 bytes 接收方发送给发送方的建议窗口大小 用于流量控制
    uint32_t data_len = get_plen(pkt) - DEFAULT_HEADER_LEN;

   
if( sock->state==LISTEN) {  //服务器监听状态
 //初始化半连接哈希表
  // int Lhashval=cal_hash(sock->bind_addr.ip,sock->bind_addr.port,0,0);
 if(flag==SYN_FLAG_MASK) { //服务器收到第一次SYN建立连接的报文

           tju_sock_addr local_addr, remote_addr;
            remote_addr.ip = inet_network("172.17.0.2"); //具体的IP地址
            remote_addr.port = src_port;           //端口

            local_addr.ip = sock->bind_addr.ip;     //具体的IP地址
            local_addr.port = sock->bind_addr.port; //端口

            tju_tcp_t* new_sock = (tju_tcp_t *)malloc(sizeof(tju_tcp_t));
            memcpy(new_sock,sock,sizeof(tju_tcp_t));
   new_sock->established_local_addr=local_addr;
   new_sock->established_remote_addr=remote_addr;
  //new_sock->established_local_addr.ip=inet_network("172.17.0.2");  //inet_network将点分地址转化为主机字节序的二进制IP地址
  //new_sock->bind_addr.port=des_port;
  //new_sock->established_remote_addr.ip=inet_network("172.17.0.3");
 // new_sock->established_remote_addr.port=src_port;   //对于服务器而言，接收到客户端发来的报文，将源端口(客户端)设置为自己的远程端口
   new_sock->state=SYN_RECV;  //将状态转换为SYN_RECV
  //服务器向客户端发送SYN_ACK

   //uint32_t t_seq=0;
  // uint32_t t_ack=seq+1;

  char* msg2=create_packet_buf(local_addr.port, src_port, 0, seq+1,
                          DEFAULT_HEADER_LEN,DEFAULT_HEADER_LEN,(uint8_t)(SYN_FLAG_MASK +ACK_FLAG_MASK),1,0,NULL,0);
   sendToLayer3(msg2,DEFAULT_HEADER_LEN);
   sock->state=SYN_RECV;
   printf("syn_ack sent!\n");
   //starttimer();
  //将新的socket放到监听他的socket半连接列表
 //int hashval=cal_hash(new_sock->bind_addr.ip,new_sock->bind_addr.port,new_sock->established_remote_addr.ip,new_sock->established_remote_addr.port);
    //established_socks[hashval]=new_sock;
   return 0;
 }
 else {return 0;}
 
}

else if(sock->state==SYN_RECV){//服务器处于SYN收到状态
if(flag==ACK_FLAG_MASK)  //服务器收到第三次的ACK
{ //将全连接中sock取出
  
      if(ack==1){
                node* new_acksock = (node*)malloc(sizeof(node*));
                new_acksock->next = NULL;
                new_acksock->aasocket = sock;
                pigsocket_queue.tail->next = new_acksock;    //把拥有元素sock新结点newsock赋值给原队尾结点的后继
                pigsocket_queue.tail = new_acksock;          //把当前的newsock设置为新的队尾结点
                sock->state=ESTABLISHED;
                return 0;
      }
 
}
else {return 0;}


}

else if(sock->state==SYN_SENT){ //客户端处于SYN发送状态,即发送完第一个SYN报文 
 //printf("abc\n");
if(flag==SYN_FLAG_MASK+ACK_FLAG_MASK && ack==1)  //判断接收到的是报文是否为第二次握手SYN+ACK
   {
          //tju_sock_addr local_addr, target_addr;

            // 目标地址
            //target_addr.ip = sock->established_remote_addr.ip;     //具体的IP地址
            //target_addr.port = sock->established_remote_addr.port; //端口

            // 本地地址
            //local_addr.ip = sock->established_local_addr.ip;     //具体的IP地址
            //local_addr.port = sock->established_local_addr.port; //端口  
     // uint32_t t_seq = ack; 
     // uint32_t t_ack = seq + 1;
    //发送ACK
   char* msg3=create_packet_buf(des_port,src_port,ack,seq+1,DEFAULT_HEADER_LEN,DEFAULT_HEADER_LEN,(uint8_t)ACK_FLAG_MASK,1,0,NULL,0);
   sendToLayer3(msg3,DEFAULT_HEADER_LEN);
   sock->state=ESTABLISHED;   //改变sock的状态
   printf("Established and ack send!\n");
   return 0;
   }
   else{return 0;}
}

else if(sock->state==ESTABLISHED){
    // 把收到的数据放到接受缓冲区
    if(flag==NO_FLAG){
    while(pthread_mutex_lock(&(sock->recv_lock)) != 0); // 加锁

    if(sock->received_buf == NULL){
        sock->received_buf = malloc(data_len);
    }else {
        sock->received_buf = realloc(sock->received_buf, sock->received_len + data_len);
    }
    memcpy(sock->received_buf + sock->received_len, pkt + DEFAULT_HEADER_LEN, data_len);
    sock->received_len += data_len;

    pthread_mutex_unlock(&(sock->recv_lock)); // 解锁
    }
    return 0;
 }
}


int tju_close (tju_tcp_t* sock){
    return 0;
}