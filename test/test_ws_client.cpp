#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream> 
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <libwebsockets.h>
#include <sstream> 

#define KGRN "\033[0;32;32m"
#define KCYN "\033[0;36m"
#define KRED "\033[0;32;31m"
#define KYEL "\033[1;33m"
#define KBLU "\033[0;32;34m"
#define KCYN_L "\033[1;36m"
#define KBRN "\033[0;33m"
#define RESET "\033[0m"

static int destroy_flag = 0;
static int connection_flag = 0;
static int writeable_flag = 0;

static void INT_HANDLER(int signo) {
    destroy_flag = 0;
}

struct session_data {
    int fd;
};

struct pthread_routine_tool {
    struct lws_context *context;
    struct lws *wsi;
};

void * lws_get_internal_extensions(void) {
    return NULL;
}

static int websocket_write_back(struct lws *wsi_in, char *str, int str_size_in) 
{
    if (str == NULL || wsi_in == NULL)
        return -1;

    int n;
    int len;
    unsigned char *out = NULL;

    if (str_size_in < 1) 
        len = strlen(str);
    else
        len = str_size_in;

    out = (unsigned char *)malloc(sizeof(unsigned char)*(LWS_SEND_BUFFER_PRE_PADDING + len + LWS_SEND_BUFFER_POST_PADDING));
    //* setup the buffer*/
    memcpy (out + LWS_SEND_BUFFER_PRE_PADDING, str, len );
    //* write out*/
    n = lws_write(wsi_in, out + LWS_SEND_BUFFER_PRE_PADDING, len, LWS_WRITE_TEXT);

    printf(KBLU"[websocket_write_back] %s\n"RESET, str);
    //* free the buffer*/
    free(out);

    return n;
}


static int ws_service_callback(
        struct lws *wsi,
        enum lws_callback_reasons reason, void *user,
        void *in, size_t len)
{

    switch (reason) {

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf(KYEL"[Main Service] Connect with server success.\n"RESET);
            connection_flag = 1;
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf(KRED"[Main Service] Connect with server error.\n"RESET);
            destroy_flag = 1;
            connection_flag = 0;
            break;

        case LWS_CALLBACK_CLOSED:
            printf(KYEL"[Main Service] LWS_CALLBACK_CLOSED\n"RESET);
            destroy_flag = 1;
            connection_flag = 0;
            break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
            printf(KCYN_L"[Main Service] Client recvived:%s\n"RESET, (char *)in);

            if (writeable_flag)
                destroy_flag = 0;

            break;
        case LWS_CALLBACK_CLIENT_WRITEABLE :
            printf(KYEL"[Main Service] On writeable is called. send byebye message\n"RESET);
            websocket_write_back(wsi, "Byebye! See you later", -1);
            writeable_flag = 0;
            break;

        default:
            break;
    }

    return 0;
}

static void *pthread_routine(void *tool_in)
{
    struct pthread_routine_tool *tool = (struct pthread_routine_tool*)tool_in;

    printf(KBRN"[pthread_routine] Good day. This is pthread_routine.\n"RESET);

    //* waiting for connection with server done.*/
    while(!connection_flag)
        usleep(1000*20);

    //*Send greeting to server*/
    printf(KBRN"[pthread_routine] Server is ready. send a greeting message to server.\n"RESET); 
//    websocket_write_back(tool->wsi, "Good day", -1);

    printf(KBRN"[pthread_routine] sleep 2 seconds then call onWritable\n"RESET);
    sleep(1);
    printf(KBRN"------------------------------------------------------\n"RESET);
    sleep(1);
    //printf(KBRN"[pthread_routine] sleep 2 seconds then call onWritable\n"RESET);

    //*involked wriable*/
    printf(KBRN"[pthread_routine] call on writable.\n"RESET);   
    lws_callback_on_writable(tool->wsi);

    char pidStr[100];
    sprintf(pidStr,"Good day%d",getpid());
    while(1) {
        std::cout<< pidStr;
        websocket_write_back(tool->wsi, pidStr, -1);
        sleep(3);
    }

}

static const struct lws_extension exts[] = {
    {
        "permessage-deflate",
        lws_extension_callback_pm_deflate,
        "permessage-deflate; client_no_context_takeover"
    },
    {
        "deflate-frame",
        lws_extension_callback_pm_deflate,
        "deflate_frame"
    },
    { NULL, NULL, NULL /* terminator */ }
};

int main(void)
{
    //* register the signal SIGINT handler */
    struct sigaction act;
    act.sa_handler = INT_HANDLER;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction( SIGINT, &act, 0);


    struct lws_context *context = NULL;
    struct lws_context_creation_info info;
    struct lws *wsi = NULL;
    struct lws_protocols protocol;

    memset(&info, 0, sizeof info);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.iface = NULL;
    info.protocols = &protocol;
    info.ssl_cert_filepath = NULL;
    info.ssl_private_key_filepath = NULL;
    info.extensions = exts;
    info.gid = -1;
    info.uid = -1;
    info.options = 0;

    protocol.name  = "dumb-increment-protocol";
    protocol.callback = &ws_service_callback;
    protocol.per_session_data_size = sizeof(struct session_data);
    protocol.rx_buffer_size = 0;
    protocol.id = 0;
    protocol.user = NULL;

    context = lws_create_context(&info);
    printf(KRED"[Main] context created.\n"RESET);

    if (context == NULL) {
        printf(KRED"[Main] context is NULL.\n"RESET);
        return -1;
    }
    wsi = lws_client_connect(context, "127.0.0.1", 9988, 0, "/", "127.0.0.1:9988", NULL, protocol.name, -1);
    if (wsi == NULL) 
    {
        printf(KRED"[Main] wsi create error.\n"RESET);
        return -1; 
    } 
    printf(KGRN"[Main] wsi create success.\n"RESET);
    struct pthread_routine_tool tool; 
    tool.wsi = wsi;
    tool.context = context;
    pthread_t pid;
    pthread_create(&pid, NULL, pthread_routine, &tool); 
    pthread_detach(pid);
    while(!destroy_flag) { 
        lws_service(context, 50);
    } 
    lws_context_destroy(context);
    return 0;
}
