#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <libwebsockets.h>
#include <queue>
#include <list>
#include <set>
#include <string>
#include <mutex>
#include <thread>
#include <iostream>
#include <iterator>
#include <errno.h>
#include <map>
#include <fstream>
#include "json/json.h"
// #include <mutex>
// #include <atomic>

using namespace std;
struct barrage_t {
    time_t time ;
    string img  ;
    string comment;
};

// std::atomic_t<int>  globle_delete_message_count(0)
// std::atomic_t<int>  globle_insert_message_count(0)
int globle_delete_message_count     ;
int globle_insert_message_count     ;

std::list<barrage_t> globle_barrage_list;
std::map<lws*,int> globle_user_list;
pthread_rwlock_t globle_rwlock = PTHREAD_RWLOCK_INITIALIZER;

string barrage2Json(struct barrage_t barrage) 
{
    Json::Value root;
    root["img"]         = barrage.img;
    root["comment"]     = barrage.comment;
    return root.toStyledString();
}

barrage_t Json2barrage(string msg)
{
    Json::Reader reader;  
    Json::Value root;  
    struct  barrage_t  barrage; 
    Json::Features f = Json::Features::strictMode();
    if (reader.parse(msg, root) && root.type() == Json::objectValue) { 
        barrage.img     = root["img"].asString(); 
        barrage.comment = root["comment"].asString();
    }
    return barrage;
}
static int websocket_write_back(struct lws *wsi_in, char *str) 
{  
    if (str == NULL || wsi_in == NULL)  {
        return -1;  
    }
    unsigned char *out = (unsigned char *)malloc(sizeof(unsigned char)*(LWS_SEND_BUFFER_PRE_PADDING + strlen(str)+ LWS_SEND_BUFFER_POST_PADDING));  
    memcpy (out + LWS_SEND_BUFFER_PRE_PADDING, str,strlen(str));  //要发送的数据从此处拷贝
    int ret = lws_write(wsi_in, out + LWS_SEND_BUFFER_PRE_PADDING, strlen(str), LWS_WRITE_TEXT);  //lws的发送函数
    free(out); 
    return ret;  
}

int barrage_handle(struct lws *wsi_in)
{
    if( 0 >=  globle_barrage_list.size()) {
        return 0;
    }

    std::map<lws*,int>::iterator userIt = globle_user_list.find(wsi_in);
    if (userIt == globle_user_list.end()) {
        printf("userIt = null\n");
        globle_user_list[wsi_in] = globle_insert_message_count;
        return 0 ;
    }

    int needCount  = globle_insert_message_count - userIt->second;
    if(needCount <= 0 ) {
        return 0;
    }

    pthread_rwlock_rdlock(&globle_rwlock);
    std::list<barrage_t>::iterator barrageIt  = globle_barrage_list.begin();
    for(int n = 0 ; n < needCount ; n++) {
        std::advance(barrageIt, (userIt->second - globle_delete_message_count) + n);
        string message  = barrage2Json(*barrageIt);
        websocket_write_back(userIt->first, const_cast<char*>(message.c_str()));
    }
    globle_user_list[wsi_in] = needCount + userIt->second;
    pthread_rwlock_unlock(&globle_rwlock);
    return needCount;
}

void* barrage_rubbish(void * argv) {
    while (1) {
        pthread_rwlock_wrlock(&globle_rwlock);
        std::list<barrage_t>::iterator it ;
        for (it = globle_barrage_list.begin(); it != globle_barrage_list.end();) {
            if((*it).time < time(NULL) - 10 ) {
                globle_barrage_list.erase(it++);
                ++globle_delete_message_count;
            } 
            else {
                ++it; 
            }
        }
        pthread_rwlock_unlock(&globle_rwlock);
        sleep(10);
    }
}

void barrage_insert_handle(struct lws * wsi_in,string msg) 
{
    struct  barrage_t barrage  = Json2barrage(msg);
    if(!barrage.comment.size()) {
        return  ;
    }
    barrage.time    = time(NULL);
    pthread_rwlock_wrlock(&globle_rwlock);
    globle_barrage_list.push_back(barrage);
    globle_insert_message_count += 1;
    pthread_rwlock_unlock(&globle_rwlock);
}


void user_insert_handle(struct lws * wsi_in) 
{
    globle_user_list[wsi_in]    = globle_insert_message_count ;
}

void connect_destroy_handle(struct lws *wsi) 
{
    std::map<lws*,int>::iterator it;
    it = globle_user_list.find(wsi);
    if(it != globle_user_list.end()) { 
        globle_user_list.erase(it);
        printf("正在销毁\n");
    }
}

//callback_dumb_increment
static int callback_dumb_increment(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    int temp = 0;
    string  message;
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            user_insert_handle(wsi);
            break;
        case LWS_CALLBACK_RECEIVE: 
            message     = (char *)in;
            barrage_insert_handle(wsi, message);
            lws_close_reason(wsi, LWS_CLOSE_STATUS_GOINGAWAY, (unsigned char *)"seeya", 5);
            break;
        case LWS_CALLBACK_SERVER_WRITEABLE: 
            barrage_handle(wsi);
            break;
        case LWS_CALLBACK_CLOSED:
        case LWS_CALLBACK_WSI_DESTROY:
            connect_destroy_handle(wsi);
            break;
    }
    return 0;
}

//注册协议,一种协议，对应一套处理方案（类似驱动中的设备树）
static struct lws_protocols protocols[] = {
    {
        "dumb-increment-protocol",
        callback_dumb_increment,
        100, 
        0
    },
    { NULL, NULL, 0, 0 } /* terminator */
};


int writable_all_protocol(struct lws_context *context,unsigned int oldms)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned int ms = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
    if ((ms - oldms) > 50) {
        lws_callback_on_writable_all_protocol(context, &protocols[0]);
        oldms = ms;
    } 
    return oldms;
}

int start_ws_server(int port=8891) {
    struct lws_context_creation_info info;//上下文对象的信息
    struct lws_context *context;//上下文对象指针

    volatile int force_exit = 0;
    unsigned int oldms = 0;
    int n = 0;
    //设置info，填充info信息体
    memset(&info,0,sizeof(info));
    info.port = port;
    info.iface=NULL;
    info.protocols = protocols;
    info.extensions = NULL; 
    // info.ssl_cert_filepath = "./openssl/213941010200996.pem";
    // info.ssl_private_key_filepath = "./openssl/213941010200996.key"; 
    info.ssl_ca_filepath = NULL;  
    info.ssl_private_key_filepath = NULL;
    info.ssl_ca_filepath = NULL;
    info.gid = -1;  
    info.uid = -1;  
    info.options = 0;  
    info.ka_time = 0;
    info.ka_probes = 0;
    info.ka_interval = 0;

    context = lws_create_context(&info); //创建上下文对面，管理ws
    if (context == NULL) {  
        printf(" Websocket context create error.\n");  
        return -1;  
    }
    printf("starting server with thread: %d...\n", lws_get_count_threads(context));
    while(1) {
        oldms   = writable_all_protocol(context,oldms);
        lws_service(context, 50);
    }
    usleep(10);
    lws_context_destroy(context);
    return 0;
}

void savePid() {

    FILE * fp;
    char  pidStr[20]; 
    if( (fp = fopen ("/tmp/websocket.pid","w")) == NULL ) {
        perror("Cannot open pid output file.\n");
        exit(EXIT_FAILURE);
    }
    sprintf(pidStr,"%d",getpid()); 
    fwrite(pidStr,sizeof(char),strlen(pidStr),fp) ;
    fclose(fp);
}

int main(int argc ,char ** argv) 
{
    int pCount   = 1;                // 线程数量
    int port     = 9090;            // 启动四个线程  
    pthread_t barrageRubbishPID;    
    if (pthread_rwlock_init(&globle_rwlock ,NULL) != 0) {
        perror("rwlock initialization failed");
        exit(EXIT_FAILURE);
    }
    if(0 != pthread_create(&barrageRubbishPID, NULL, barrage_rubbish, NULL)) {
        perror("Thread creation failed");
        exit(EXIT_FAILURE);
    }
    savePid();
    start_ws_server(port) ;
    pthread_detach(barrageRubbishPID); 
    pthread_rwlock_destroy(&globle_rwlock);
    return 0;
}

