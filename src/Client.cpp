#include "Chat.h"

using namespace chat;

Client::Client( char * username, FILE *log_level ) {
    _user_id = -1;
    _sock = -1;
    _close_issued = 0;
    pthread_mutex_init(  &_noti_queue_mutex, NULL );
    pthread_mutex_init( &_stop_mutex, NULL );
    pthread_mutex_init( &_connected_users_mutex, NULL );
    pthread_mutex_init( &_error_queue_mutex, NULL );
    _username = username;
    _log_level = log_level;
}

/*
* Connect to server on server_address:server_port
* returns 0 on succes -1 on error
*/
int Client::connect_server(char *server_address, int server_port) {
    /* Create socket */
    fprintf(_log_level,"INFO: Creating socket\n");
    if( ( _sock = socket(AF_INET, SOCK_STREAM, 0) ) < 0 ) {
        fprintf(_log_level,"ERROR: Error on socket creation");
        return -1;
    }

    /* Save server info on ds*/
    fprintf(_log_level, "DEBUG: Saving server info\n");
    _serv_addr.sin_family = AF_INET; 
    _serv_addr.sin_port = htons(server_port); // Convert to host byte order
    fprintf(_log_level,"Converting to network address");
    if(  inet_pton(AF_INET, server_address, &_serv_addr.sin_addr) <= 0 ) { // Convert ti network address
        fprintf(_log_level,"ERROR: Invalid server address\n");
        return -1;
    }

    /* Set up connection */
    fprintf(_log_level,"INFO: Setting up connection\n");
    if( connect(_sock, (struct sockaddr *)&_serv_addr, sizeof(_serv_addr)) < 0 ) {
        fprintf(_log_level,"ERROR: Unable to connect to server\n");
        return -1;
    }

    return 0;
}

/*
* Log in to server using username
* Return 0 on succes -1 on error
*/
int Client::log_in() {

    /* Step 1: Sync option 1 */
    fprintf(_log_level,"DEBUG: Building log in request\n");
    MyInfoSynchronize * my_info(new MyInfoSynchronize);
    char ip[256];
    gethostname(ip, sizeof(ip)); // Get client ip address
    fprintf( _log_level, "DEBUG: Logging in from ip: %s\n", ip );
    my_info->set_username(_username);
    my_info->set_ip(ip);

    ClientMessage msg;
    msg.set_option(1);
    msg.set_allocated_synchronize(my_info);

    /* Sending sync request to server */
    send_request( msg );

    /* Step 2: Read ack from server */
    fprintf(_log_level,"DEBUG: Waiting for server ack\n");
    char ack_res[ MESSAGE_SIZE ];
    int rack_res_sz = read_message( ack_res );
    ServerMessage res = parse_response( ack_res );

    fprintf(_log_level,"INFO: Checking for response option\n");
    if( res.option() == 3 ) {
        fprintf(_log_level,"ERROR: Server returned error: %s\n", res.error().errormessage().c_str());
        return -1;
    } else if( res.option() != 4 ){
        fprintf(_log_level,"ERROR: Unexpected response from server\n");
        return -1;
    }

    fprintf(_log_level,"DEBUG: User id was returned by server\n");
    _user_id = res.myinforesponse().userid() ;

    /* Step 3: Send ack to server */
    // TODO MyInfoAcknowledge Not defined on protocol
    MyInfoAcknowledge * my_info_ack(new MyInfoAcknowledge);
    my_info_ack->set_userid(_user_id);

    ClientMessage res_ack;
    res_ack.set_option(6);

    send_request(res_ack);

    return 0;
}

/*
* Get all connected users to server
* returns client message with request details
*/
int Client::get_connected_request() {
    /* Build request */
    fprintf(_log_level,"DEBUG: Building connected request\n");
    connectedUserRequest * msg(new connectedUserRequest);
    msg->set_userid( _user_id );
    msg->set_username( _username );

    ClientMessage req;
    req.set_option( 2 );
    req.set_allocated_connectedusers( msg );
    
    send_request( req );

    char ack_res[ MESSAGE_SIZE ];
    int ack_res_sz;

    if( ( ack_res_sz = read_message( ack_res ) ) < 0 ) {
        return -1;
    }

    /* Wait for message */
    fprintf(_log_level,"DEBUG: New messages was received from server\n");
    ServerMessage res = parse_response( ack_res );

    /* Check for errors */
    if( res.option() == 3 ) {
        add_error( res.error() );
        return -1;
    } else if( res.option() != 5 ) {
        return -1;
    }

    /* Save connected users */
    map <string, connected_user> c_users;
    int i;
    for( i = 0; i < res.connecteduserresponse().connectedusers().size(); i++ ) {
        ConnectedUser rec_user = res.connecteduserresponse().connectedusers().at( i );
        struct connected_user lst_users;
        lst_users.name = rec_user.username();
        lst_users.id = rec_user.userid();
        lst_users.status = rec_user.status();
        lst_users.ip = rec_user.status();
        c_users[ rec_user.username() ] = lst_users;
    }

    set_connected_users( c_users );

    return 0;
}

/*
* Build request to change the status to n_st
*/
int Client::change_status( string n_st ) {
    ChangeStatusRequest * n_st_res( new ChangeStatusRequest );
    n_st_res->set_status( n_st );
    ClientMessage req;
    req.set_option( 3 );
    req.set_allocated_changestatus( n_st_res );
    
    send_request( req );

    char ack_res[ MESSAGE_SIZE ];
    int ack_res_sz;

    if( ( ack_res_sz = read_message( ack_res ) ) < 0 ) {
        return -1;
    }

    /* Wait for message */
    fprintf(_log_level,"DEBUG: New messages was received from server\n");
    ServerMessage res = parse_response( ack_res );

    /* Check for errors */
    if( res.option() == 3 ) {
        add_error( res.error() );
        return -1;
    } else if( res.option() != 6 ) {
        return -1;
    }

    fprintf(_log_level, "LOG: Status received by server %s\n", res.changestatusresponse().status().c_str());

    return 0;

}

/*
* Build a request to broadcast msg to all connected users on server
*/
int Client::broadcast_message( string msg ) {
    BroadcastRequest * br_msg( new BroadcastRequest );
    br_msg->set_message( msg );
    ClientMessage req;
    req.set_option( 4 );
    req.set_allocated_broadcast( br_msg );

    send_request( req );
    
    char ack_res[ MESSAGE_SIZE ];
    int ack_res_sz;

    if( ( ack_res_sz = read_message( ack_res ) ) < 0 ) {
        return -1;
    }

    /* Wait for message */
    fprintf(_log_level,"DEBUG: New messages was received from server\n");
    ServerMessage res = parse_response( ack_res );

    /* Check for errors */
    if( res.option() == 3 ) {
        add_error( res.error() );
        fprintf(_log_level,"ERROR: Error was returned by server: %s\n", res.error().errormessage().c_str());
        return -1;
    } else if( res.option() != 7 ) {
        fprintf(_log_level,"ERROR: Incorrect response was returned by server: %d\n", res.option());
        return -1;
    }

    fprintf(_log_level, "LOG: Status received by server %s\n", res.broadcastresponse().messagestatus().c_str());

    return 0;

}

/*
* Build request to send a direct message
*/
int Client::direct_message( string msg, int dest_id, string dest_nm ) {
    DirectMessageRequest * dm( new DirectMessageRequest );
    dm->set_message( msg );
    /* Verify optional params were passed */
    if( dest_id > 0 ) {
        dm->set_userid( dest_id );
    }

    if( dest_nm != "" ) {
        dm->set_username( dest_nm );
    }

    ClientMessage req;
    req.set_option( 5 );
    req.set_allocated_directmessage( dm );
    

    char ack_res[ MESSAGE_SIZE ];
    int ack_res_sz;

    if( ( ack_res_sz = read_message( ack_res ) ) < 0 ) {
        return -1;
    }

    /* Wait for message */
    fprintf(_log_level,"DEBUG: New messages was received from server\n");
    ServerMessage res = parse_response( ack_res );

    /* Check for errors */
    if( res.option() == 3 ) {
        add_error( res.error() );
        return -1;
    } else if( res.option() != 6 ) {
        return -1;
    }

    fprintf(_log_level, "LOG: Status received by server %s\n", res.broadcastresponse().messagestatus().c_str());

    return 0;
}

/*
* Send request to server
* returns 0 on succes -1 on error
*/
int Client::send_request(ClientMessage request) {
    /* Serealize string */
    int res_code = request.option();
    fprintf(_log_level,"DEBUG: Serealizing request with option %d\n", res_code);
    std::string srl_req;
    request.SerializeToString(&srl_req);

    char c_str[ srl_req.size() + 1 ];
    strcpy( c_str, srl_req.c_str() );

    /* Send request to server */
    fprintf(_log_level,"INFO: Sending request\n");
    if( sendto( _sock, c_str, strlen(c_str ), 0, (struct sockaddr*)&_serv_addr,sizeof( &_serv_addr ) ) < 0 ) {
        fprintf(_log_level,"ERROR: Error sending request");
        return -1;
    }

    fprintf(_log_level,"DEBUG: Request was send to socket %d\n", _sock);
    return 0;
}

/*
* Read for messages from server
* Returns the read message
*/
int Client::read_message( void *res ) {
    /* Read for server response */
    int rec_sz;
    fprintf(_log_level, "DEBUG: Waiting for messages from server on fd %d\n", _sock);
    if( ( rec_sz = recvfrom(_sock, res, MESSAGE_SIZE, 0, NULL, NULL) ) < 0 ) {
        fprintf(_log_level,"ERROR: Error reading response");
        exit(EXIT_FAILURE);
    }
    fprintf(_log_level, "DEBUG: Received message from server\n");
    return rec_sz;
}

/*
* Parse the response to Server message
*/
ServerMessage Client::parse_response( char *res ) {
    ServerMessage response;
    response.ParseFromString(res);
    return response;
 }

/*
* Send request to server to change status
*/


 /*
 * Backgorund listener for server messages and adds them to response queue
 */
void * Client::bg_listener( void * context ) {
    /* Get Client context */
    Client * c = ( ( Client * )context );

    pid_t tid = gettid();
    fprintf(c->_log_level, "DEBUG: Staring client interface on thread ID: %d\n", ( int )tid);

    /* read for messages */
    while( c->get_stopped_status() == 0 ) {
        char ack_res[ MESSAGE_SIZE ];
        if( c->read_message( ack_res ) <= 0 ) {
            fprintf( c->_log_level, "LOG: Server disconnected terminating session..." );
            c->send_stop();
            break;
        }
        fprintf(c->_log_level,"DEBUG: New messages was received from server\n");
        ServerMessage res = c->parse_response( ack_res );
        fprintf(c->_log_level, "DEBUG: Adding response to queue\n" );
        c->push_res( res );
    }
    fprintf(c->_log_level, "INFO: Exiting listening thread\n");
    pthread_exit( NULL );
}

/*
* Stop the server session
*/
void Client::stop_session() {
    fprintf(_log_level, "DEBUG: Starting shutting down process...\n");
    close( _sock ); // Stop send an write operations
    send_stop(); // Set stop flag
}

/*
* Get the stopped flag to verify if Client::stop_session has been called
*/
int Client::get_stopped_status() {
    int tmp = 0;
    pthread_mutex_lock( &_stop_mutex );
    tmp = _close_issued;
    pthread_mutex_unlock( &_stop_mutex );
    return tmp;
}

/*
* Send stop sets the stopped flag to start the shutdown process
*/
void Client::send_stop() {
    fprintf(_log_level, "DEBUG: Setting shutdown flag\n");
    pthread_mutex_lock( &_stop_mutex );
    _close_issued = 1;
    pthread_mutex_unlock( &_stop_mutex );
    fprintf(_log_level, "DEBUG: Shutdown flag set correctly\n");
}

/*
*  Get all the connected users on memory
*/
map <string, connected_user> Client::get_connected_users() {
    map <string, connected_user> tmp;
    pthread_mutex_lock( &_connected_users_mutex );
    tmp = _connected_users;
    pthread_mutex_unlock( &_connected_users_mutex );
    return tmp;
}

/*
* Refresh the list of connected users
*/
void Client::set_connected_users( map <string, connected_user> cn_u ) {
    pthread_mutex_lock( &_connected_users_mutex );
    _connected_users = cn_u;
    pthread_mutex_unlock( &_connected_users_mutex );
}

/*
* Add new error
*/
void Client::add_error( ErrorResponse err ) {
    pthread_mutex_lock( &_error_queue_mutex );
    _error_queue.push( err );
    pthread_mutex_unlock( &_error_queue_mutex );
}

/*
* Get the latest error
*/
string Client::get_last_error() {
    string error_msg;
    pthread_mutex_lock( &_error_queue_mutex );
    error_msg = _error_queue.front().errormessage();
    _error_queue.pop();
    pthread_mutex_unlock( &_error_queue_mutex );
    return error_msg;
}

/*
* Add respoonse to queue using mutext locks
*/
void Client::push_res( ServerMessage el ) {
    int option = el.option();
    fprintf(_log_level, "LOG: Adding new message to queue option %d\n", option);
    message_received msg;
    pthread_mutex_lock( &_noti_queue_mutex );
    if( option == 1 ) {
        msg.from_id = el.broadcast().userid();
        msg.message = el.broadcast().message();
        msg.type = BROADCAST;
        _br_queue.push( msg );
    } else if( option == 2 ) {
        msg.from_username = el.message().userid();
        msg.message = el.message().message();
        msg.type = DIRECT;
        _dm_queue.push( msg );
    }
    pthread_mutex_unlock( &_noti_queue_mutex );
}

/*
* Get element from response queue
*/
message_received Client::pop_res( message_type mtype ) {
    message_received res;
    pthread_mutex_lock( &_noti_queue_mutex );
    if( mtype == BROADCAST ) {
        res = _br_queue.front();
        _br_queue.pop();
    } else if( mtype == DIRECT ) {
        res = _dm_queue.front();
        _dm_queue.pop();
    }
    pthread_mutex_unlock( &_noti_queue_mutex );
    return res;
}

/*
* Get element to buffer. Returns 0 on succes -1 if empty
*/
int Client::pop_to_buffer( message_type mtype, message_received * buf ) {
    int res;
    pthread_mutex_lock( &_noti_queue_mutex );
    if( mtype == BROADCAST ) {
        if( !_br_queue.empty() ) {
            *buf = _br_queue.front();
            _br_queue.pop();
            res = 0;
        } else {
            res = -1;
        }
    } else if( mtype == DIRECT ) {
        if( !_dm_queue.empty() ) {
            *buf = _dm_queue.front();
            _dm_queue.pop();
            res = 0;
        } else {
            res = -1;
        }
    }
    pthread_mutex_unlock( &_noti_queue_mutex );
    return res;
}


 /*
 * Start a new session on server on cli interface
 */
void Client::start_session() {
    /* Verify a connection with server was stablished */
    if ( _sock < 0 ){
        fprintf(_log_level, "ERROR: No connection to server was found\n" );
        exit( EXIT_FAILURE );
    }

    /* Verify if client was registered on server */
    if( _user_id < 0 ) { // No user has been registered
        /* Attempt to log in to server */
        if( log_in() < 0 ) {
            fprintf(_log_level, "ERROR: Unable to log in to server\n");
            exit( EXIT_FAILURE );
        } else {
            fprintf(_log_level, "INFO: Logged in to server user id %d\n", _user_id);
        }
    }
    /* Create a new thread to listen for messages from server */
    pthread_t thread;
    pthread_create( &thread, NULL, &bg_listener, this );

    /* Start client interface */
    while(1) {
        int input;
        printf("\n\n------ Bienvenido al chat %s ------\n", _username);
        printf("Seleccione una opcion:\n");
        printf("\t1. Enviar mensaje al canal publico\n");
        printf("\t3. Enviar mensaje directo \n");
        printf("\t4. Cambiar de estado \n");
        printf("\t5. Ver usuarios conectados \n");
        printf("\t6. Ver informacion de usuario \n");
        printf("\t7. Ver canal general \n");
        printf("\t8. Ver mensajes directos \n");
        cin >> input;
        
        string br_msg, dm, dest_nm, n_st;
        message_type msg_t;
        int res_cd, no = 0;
        switch ( input ) {
            case 1:
                printf("Ingrese mensaje a enviar:\n");
                cin >> br_msg;
                res_cd = broadcast_message( br_msg );
                break;
            case 3:
                printf("Ingrese nombre de usuario del destinatario:\n");
                cin >> dest_nm;
                printf("Ingrese el mensaje a enviar: \n");
                cin >> br_msg;
                res_cd = direct_message( br_msg, -1, dest_nm );
                break;
            case 4:
                printf("Ingrese el nuevo estado: \n");
                cin >> n_st;
                res_cd = change_status( n_st );
                break;
            case 5:
                printf("Ver usuarios conectados: \n");
                res_cd = get_connected_request();
                break;
            case 6:
                printf("Ingrese usuario que desea ver: \n");
                break;
            case 7:
                msg_t = BROADCAST;
                no = 1;
                break;
            case 8:
                msg_t = DIRECT;
                no = 1;
                break;
            default:
                break;
        }
        if( input == 9 )
            break;
        
        if( no ) {
            message_received mtp;
            while( pop_to_buffer( msg_t, &mtp ) == 0 ) {
                cout << "-------------------------------------" << endl;
                cout << "ID from: " << mtp.from_id << endl;
                cout << "User name from: " << mtp.from_username << endl;
                cout << "Messgae" << mtp.message << endl;
                cout << "-------------------------------------" << endl;
            }
        }
    }
    stop_session();
}