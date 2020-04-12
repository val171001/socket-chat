#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
#include <map>
#include <queue> 
#include <iostream>
#include <stdarg.h>
#include <pthread.h>
#include <sys/types.h>
#include "mensaje.pb.h"

using namespace std;
using namespace chat;

#ifndef MESSAGE_SIZE
#define MESSAGE_SIZE 8192
#endif

#ifndef MAX_QUEUE
#define MAX_QUEUE 20
#endif

#ifndef Client
class Client {
    public:
        int _sock;
        struct sockaddr_in _serv_addr;
        int _user_id;
        char * _username;
        Client( char * username, FILE *log_level = stdout );
        int connect_server(char *server_address, int server_port);
        int log_in();
        int send_request(ClientMessage req);
        int read_message( void * buff );
        ServerMessage parse_response( char *res );
        ClientMessage get_connected_request();
        ClientMessage change_status( string n_st );
        ClientMessage broadcast_message( string msg );
        ClientMessage direct_message( string msg );
        ClientMessage process_response( ServerMessage res );
        void start_session();
        void stop_session();
        static void * bg_listener( void * context );
    private:
        FILE *_log_level;
        int _close_issued;
        pthread_mutex_t _stop_mutex;
        int get_stopped_status();
        void send_stop();
};
#endif


#ifndef client_info
struct client_info {
    int id;
    struct sockaddr_in socket_info;
    int socket_fd;
    int req_fd;
    string name;
    string ip;
    string status;
};
#endif


#ifndef Server
class Server {
    public:
        struct sockaddr_in _serv_addr, _cl_addr;
        int _sock;
        int _user_count;
        int _port;
        queue <client_info> _req_queue;
        map<std::string, client_info> _user_list;
        Server( int port, FILE *log_level = stdout );
        int initiate();
        void start();
        int listen_connections();
        int read_request( int fd, void *buf );
        int send_response( int sock_fd, struct sockaddr_in *dest, ServerMessage res );
        ServerMessage process_request( ClientMessage cl_msg, client_info cl = {} );
        ServerMessage broadcast_message( BroadcastRequest req );
        ServerMessage send_to_all( string msg );
        ServerMessage direct_message( DirectMessageRequest req );
        ServerMessage error_response( string msg );
        string register_user( MyInfoSynchronize req, client_info cl );
        ServerMessage get_connected_users( connectedUserRequest req );
        ServerMessage change_user_status( ChangeStatusRequest req );
        ClientMessage parse_request( char *req );
        static void * new_conn_h( void * context );
    private:
        FILE *_log_level;
};
#endif
