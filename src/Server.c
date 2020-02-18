/*

            ################## Code to run the Server side ################

 */


#include <arpa/inet.h>
#include <stdio.h> 
#include <stdlib.h> 
#include <signal.h>     // To handle the graceful close of program
#include <errno.h> 
#include <string.h> 
#include <sys/types.h> 
#include <netinet/in.h> //used in address storing and for transferring information in network byte order and stuff
#include <sys/socket.h> 
#include <sys/wait.h> 
#include <unistd.h>
#include <time.h>  

#define MAX_MESSAGE_SIZE 1024
#define DEFAULT_PORT 12345
#define PORT_SIZE 6
#define DEFAULT_MAX_USERS 128
#define MAX_CHANNELS 256

//declaration of structures used on the server side

//message structure 
struct Message_node{
    char *message;                  //the message itself
    int channel_id;                 //channel ID for this message
    int list_count;                 //counter to say which message this is in the linked list of messages 
    int channel_count;              //counter to say which message this is in the channel 
    struct Message_node *next_message;   //next message in linked list
    struct Message_node *next_chan_mes;  //next message in this channel
};

//List of the messages pointing to the last message sent to the server and the last message sent to any given channel
struct list{ 
    struct Message_node *last_message;              //records the most recent message sent to the server 
    struct Message_node *channel_list_end[MAX_CHANNELS];     //records the last message sent to each channel as a pointer to each message 
};

//Struct for the clinet which has a read pointer for messages on each channel 
struct Client {
    int socket;                                                     //stores the socket for the client 
    int client_id;                                                  //stores the ID for the client given by the server
    int connected;                                                  //If it says 1, client is connected, if it says 0, client is disconnected and should be reset.
    struct Message_node *first_message_since_joining[MAX_CHANNELS]; //points to the first message sent to a channel the client is subscribed to since joining the server (= list->last_message) essentially
    struct Message_node *read_channel_message[MAX_CHANNELS];        //An array of pointers to the last read message on channels the user is subscribed to  (=channel_list_end) basically
};

//declaration of arrays/lists for the messages and the clients. Also declaration for volatile int that determines the Next() while loop termination
static struct Client clients_list[DEFAULT_MAX_USERS];
static volatile int loopy = 0;
int Server_state = 1; // 0 equals false, therefor off, anything else is

//declaration of functions used on the serverside
void Client_init();
int Send_ID_Message(int sock_id, FILE* To_client);
void Dc_client(int client_id);
void Close_server(int sig);
void Init_list(struct list* list);
void Add_to_list(struct list* list, int channel_id, char* msg);
void Next(int channel_id, int client_id, FILE* output);
void Next2(int client_id, FILE* output);
void Channels(struct list* list, int client_id, FILE* output);
void Sub(int client_id, int channel_id, struct list* list, FILE* output);
void Unsub(int client_id, int channel_id, FILE* output);
char *first_word_handler(char *input_message);
int input_handler(FILE* input, FILE* output, int sockfd, struct list* list, int client_id);


//Takes one argument, does not handle for more than that
int main(int argc, char *argv[]){


    int sockfd, new_fd;                          /* listen on sockfd, new connections go to new_fd */
    struct sockaddr_in server_addr, client_addr; /* address of the server and the address of the client */
    socklen_t sin_size;                          /*length of the client addr */
    int port_check = 0;
    struct list *Message_list = (struct list*)malloc(sizeof(struct list));
    int current_client;

    //Tells the program which function to call when it recieves a SIGINT
    signal(SIGINT, Close_server);
    //INitialise the Linked list of messages
    Init_list(Message_list);
    //Initialise the clients
    Client_init();

    /* Check youve been given a port number */
    
    if (argc != 2){
        printf( "Usage: input client port number, Opening on default port 12345\n");
        fflush(stdout);
        port_check = DEFAULT_PORT;
    } else {
        port_check = atoi(argv[1]);
    }
    
    /* Generate a socket and check it doesnt fail */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        fprintf(stderr, "Please input valid socket number");
        exit(EXIT_FAILURE);
    }

    /* generate an end point */
    server_addr.sin_family = AF_INET;               /* the host/server network type (IPV4) */
    server_addr.sin_port = htons(port_check);    /* changes bit order from os to network */
    server_addr.sin_addr.s_addr = INADDR_ANY;       /* makes the current IP of cpu the server IP */

    /* Try to bind the end point to the port given by user */
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1){
        fprintf(stderr, "bind failed");
        exit(EXIT_FAILURE);
    }

            // ####### DONT KNOW IF I NEED THIS YET #######
    // Try to listen on the new socket and see if its possible 
    if (listen(sockfd, DEFAULT_MAX_USERS) == -1){
        fprintf(stderr, "listen failed");
        exit(EXIT_FAILURE);
    }
        
    /*repeat: accept, send, close, eat meat, close connection */
    while(1) {
        sin_size = sizeof(struct sockaddr_in);

        //Keep accepting new clients when the server is free
        if ((new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size)) == -1){
            perror("acceptance error");
            exit(EXIT_FAILURE);
        }

        

        //create a way for the server to communicate with the client by pointing towards the socket which the client is connected to
        FILE* From_client = fdopen(new_fd, "r");    // points towards the socket to recieve from the client

        int newfd_copy = dup(new_fd);               //duplicate the sockfd so that a second file pointer can be made

        FILE* To_client = fdopen(newfd_copy, "w");  //make a second file pointer to the socket which the client will be sent information  

        //Send the Client their user ID and then retrieve it so that the server knows who it is talking to
        current_client = Send_ID_Message(newfd_copy, To_client);

        //Run imput handler constantly bascally, it deals with all imput from the client
        while (input_handler(From_client, To_client, new_fd, Message_list, current_client) != 1){
        }
    }

    return -1;
}

// Function used to give every client in client_list a connected value of 0 so they do not get read as connected when searching for 
void Client_init(){
    
    for (int i = 0; i < DEFAULT_MAX_USERS; i++)                                  //for every client that has been created thus far
    {
        clients_list[i].connected = 0;                                              // Set the Joined status to 0 so that any one of them can become a joined client
        for(int ii = 0; ii < MAX_CHANNELS; ii++){   //for every channel do:
            clients_list[i].read_channel_message[ii] = NULL;                        //set the most recent read message to NULL
            clients_list[i].first_message_since_joining[ii] = NULL;                         // Set first message read to null as the client is yet to join
        }
    }
    
}

// Funciton used to allocate user id and then send the client their ID
int Send_ID_Message(int sock_id, FILE* To_client){
    int userID = 0;

    // Loop to determine what a new users ID will be
    for (int i = 0; i < DEFAULT_MAX_USERS; i++)
    {
        if(clients_list[i].connected == 0){             //if there is a client space that has not been connected to, do:
            clients_list[i].connected = 1;              //change the status to connected
            clients_list[i].socket = sock_id;           //allocate the current socket to this client 
            clients_list[i].client_id = i;              //allocate their client ID
            userID = (clients_list[i].client_id);       //convert the user ID to a string to send to the client
            break;                                      //break the loop because the client has been connected
        }
    }  

    fprintf(To_client, "%d\n", userID);                 //Send the client a userID
    fflush(To_client);                                  //Flush the output
    return userID;                                      //Return the user ID for the Servers reference
}

//Function used for adding a client to a subscription list
void Sub(int client_id, int channel_id, struct list* list, FILE* output){
    
    //check if the client is already subscribed to the channel, if they are, return already subbed, else, subscribe them and then send the message
    if(clients_list[client_id].read_channel_message[channel_id] != NULL){
       fprintf(output, "Already subscribed to channel: %i\n", channel_id);
    }else {
        //Allocate both the first message and the read channel to be this message as the user will not see this if they next it and the client needs reference points
        clients_list[client_id].first_message_since_joining[channel_id] = list->channel_list_end[channel_id];
        clients_list[client_id].read_channel_message[channel_id] = list->channel_list_end[channel_id];
        fprintf(output, "Subscribed to channel: %i\n", channel_id);
    }

    //print the result to the client and flush the buffer
    fflush(output);
}

// Function used to disconnect clients from the server either when they disconnect or when the server closes down
void Dc_client(int client_id){

    struct Client client = clients_list[client_id];                 //locate the client
    printf("Disconnecting Client on socket %d \n", client.socket);  //Print that the client is being disconnected from this particular socket
    close(client.socket);                                           //Close the clients connection from the server
    memset(&clients_list[client_id], 0, sizeof(struct Client));   //reset the location in memory for this client.
}

// Function used to gracefully close the server
void Close_server(int sig){
    
    Server_state = 0;                               //Change the server state from running to off
    

    for (int i = 0; i < DEFAULT_MAX_USERS; i++){    //for every user 
        if(clients_list[i].connected == 1){         //check if they are connected, if they are:
            Dc_client(clients_list[i].client_id);   //close their connection
        }
    }
    
    exit(EXIT_SUCCESS);                             //Return exit success as it gracefully exited

}

//function used to create an anchor point for every message list
struct Message_node* new_dummy_node(int channel_id) {

    //create somewhere for every channel to begin
    struct Message_node* dummy_node = (struct Message_node*)malloc(sizeof(struct Message_node));
    dummy_node->channel_count = 0;
    dummy_node->channel_id = channel_id;
    dummy_node->list_count = 0;
    dummy_node->message = "anchor";
    dummy_node->next_chan_mes = NULL;
    dummy_node->next_message = NULL;

    return dummy_node;
}

// Function for initialising the Linked list of messages
void Init_list(struct list* list) {

    list->last_message = new_dummy_node(0);              //The current last message is just the first dummy message
    list->channel_list_end[0] = list->last_message;      //give the 0th channel somewhere to start from
    for (int i = 1; i < MAX_CHANNELS; i++) {             //for every channel, do:
        list->channel_list_end[i] = new_dummy_node(i);   // The last message sent to any channel is an anchor as the server is starting up
    }

}

//Function for adding a message to the server
void Add_to_list(struct list* list, int channel_id, char* msg) {

    //End node represents a new message sent to the server, cast the void function malloc as a struct Message_node and alllocate memory for the new message.
    struct Message_node *end_node = (struct Message_node*)malloc(sizeof(struct Message_node));  

    end_node->message = msg;            // Set the message parsed to the function to be msg 
    end_node->channel_id = channel_id;  // Set the channel_id of the new message to the parsed channel id
    end_node->next_message = NULL;      // Because this is the latest message on the server now, this message points to no message, init as NULL
    end_node->next_chan_mes = NULL;     // Because this is the latest message on this channel, this message points to no message on the channel therefor init as NULL
                                                 
    end_node->list_count = list->last_message->list_count +1;   //the new latest message's list count is whatever the current last messages count is, plus one
    list->last_message->next_message = end_node;                //make the current last message, point to the new one
    list->last_message = end_node;                              //make the list, last_message pointer, point at the new last message
 
    end_node->channel_count = list->channel_list_end[channel_id]->channel_count +1; //make the end_node channel_count the same as the old last message on that channel +1  
    list->channel_list_end[channel_id]->next_chan_mes = end_node;                   //else, make the current last message point to this one and then make this the last message on that channel
    list->channel_list_end[channel_id] = end_node;                                  //Set the channels last message to be the new message

}

//Function to return the next message on a certain channel
void Next(int channel_id, int client_id, FILE* output){
    
    //find out what the last message that was read by the client on that channel and return pointer to the next message in the list if it exists
    if(clients_list[client_id].read_channel_message[channel_id] != NULL && clients_list[client_id].read_channel_message[channel_id]->next_chan_mes != NULL){

        //Move the pointer from the last read channel message on channel_id to the next message which will now be set/returned
        clients_list[client_id].read_channel_message[channel_id] = clients_list[client_id].read_channel_message[channel_id]->next_chan_mes;
        fprintf(output, "%s", clients_list[client_id].read_channel_message[channel_id]->message);
        fflush(output);

        //If the client is no subscribed to the channel, let them know
    }else if(clients_list[client_id].read_channel_message[channel_id] == NULL){
        fprintf(output, "Not subscribed to channel: %d\n", channel_id);
        fflush(output);
        
    }else {
        //If there is nothing to return to the client, let them know
        fprintf(output, "nuttin\n");
        fflush(output);
    }
}


// takes a client ID and returns the next message the client has to read from all their subscribed channels
void Next2(int client_id, FILE* output){

    int check = -1;
    int checkChan = -1;

    //for each channel that client_id is subscibed to, check if there is a message and if it is the last one, then check if that nessage is older than the current check
    for(int i = 0; i < MAX_CHANNELS; i++){
        if(clients_list[client_id].read_channel_message[i] != NULL && clients_list[client_id].read_channel_message[i]->next_chan_mes != NULL){
            if (check == -1){
                check = clients_list[client_id].read_channel_message[i]->next_chan_mes->list_count;
                checkChan = i;
            //if the next message on the channel after the read pointer is older than the check, make check the next message 
            }else if(clients_list[client_id].read_channel_message[i]->next_chan_mes->list_count < check){ 
                check = clients_list[client_id].read_channel_message[i]->next_chan_mes->list_count;
                checkChan = i;
            }  
        }
    }

    //handles there being no new messages by calling next on the first channel which would return Null if there is no new messages to be
    if (checkChan != -1) {
        //send the channel_id before sending the message itself
        fprintf(output, "Channel %d: ", checkChan);
        Next(checkChan, client_id, output);
    }else {
        fprintf(output, "nuttin\n");
        fflush(output);
    }

}

//Does not list channels the user is not subscribed to but sends relevant information for those that are 
void Channels(struct list* list, int client_id, FILE* output){

    //For every channel, check if the client is subscribed to the channel and then if they are, return the information requested
    for(int i = 0; i < MAX_CHANNELS; i++){
        if(clients_list[client_id].read_channel_message[i] != NULL){
            
            //number of messages in the channel since the server started
            int first_val = list->channel_list_end[i]->channel_count;
            //number of messages that have been read by the client
            int read_difference = clients_list[client_id].read_channel_message[i]->channel_count - clients_list[client_id].first_message_since_joining[i]->channel_count;
            //number of messages left to be read by the client
            int unread_difference = list->channel_list_end[i]->channel_count - clients_list[client_id].read_channel_message[i]->channel_count;
            
            //return relevant information to the client
            fprintf(output, "Channel %i: %i, %i, %i\n",\
            i, first_val, read_difference, unread_difference);
            fflush(output);
        }

    }
    //if there is nothing to return, tell the client
    fprintf(output, "nuttin\n");
    fflush(output);
}



//opposite of Sub, checks if a user is subscribed to the channel they wish to unsubscribe from and then sends corresponding message
void Unsub(int client_id, int channel_id, FILE* output){

    //if user is not subscribed to channel:
    if(clients_list[client_id].read_channel_message[channel_id] == NULL){
        fprintf(output, "Not subscribed to channel: %d\n", channel_id);

        //if they are subscribed to the channel:
    }else {
        clients_list[client_id].first_message_since_joining[channel_id] = NULL;
        clients_list[client_id].read_channel_message[channel_id] = NULL;
        fprintf(output, "Unsubscribed from channel %d\n", channel_id);
    }
    
    fflush(output);
}

//Function used to determine what the first word of an incoming message from the client is
char *first_word_handler(char *input_message){

    char * first_word;                              //string for the first word
    int message_size = 20;                          //integer used to track how big the message itself is
    first_word = (char *)malloc(sizeof(char) * 20); //Charater string memory allocation

    //if the message is NULL tell the server
    if (input_message == NULL){
        return NULL;
    }
    //for every character in the first word allocation, check if the character is a space or a new line or an actual character
    for(int i = 0; i < message_size; i++){
        if(input_message[i] == ' ' || input_message[i] == '\n'){
            first_word[i] = '\0';
            break;
        }else{
            first_word[i] = input_message[i];
        }
    }
    //returns what first word is 
    return first_word;
}


//Handles every kind of input from the client
int input_handler(FILE* input, FILE* output, int sockfd, struct list* list, int client_id){

    //allocate memory for the incoming message
    char *input_message = (char *)malloc(sizeof(char) * 1040);

    //collect the message from the server
    input_message = fgets(input_message, 1040, input);

    //Check that the message isnt Null from the client closing, if it is, dc the client
    if(input_message == NULL){
        Dc_client(client_id);
        return 1;
    }

    //collect the first word of the message provided
    char *first_word = first_word_handler(input_message);

    //case for if the first word is Next with channel ID or without
    if(strcmp(first_word, "Next") == 0){
        
        //if the client has asked for Next without a channel ID:
        if(input_message[4] == '\n'){
            Next2(client_id, output);
            
            //otherwise they have asked for Next with a channel ID or with an invalid ID
        }else {
            int channel = atoi(&input_message[5]);
            if ((channel == 0 && input_message[5] != '0') || (channel > 255 || channel < 0)){
                fprintf(output, "Invalid Channel: %s", &input_message[5]);
                fflush(output);

            //Call Next with a channel ID
            }else{
                Next(channel, client_id, output);
            }
        }

    //case for if the first word is sub
    }else if (strcmp(first_word, "Sub") == 0){
        // Send a message to sub the client to a channel with the ID given
        int skip = atoi(&input_message[4]);


        if ((skip == 0 && input_message[4] != '0') || (skip > 255 || skip < 0)){
            fprintf(output, "Invalid Channel: %s", &input_message[4]);
            fflush(output);
        }else{
            Sub(client_id, skip, list, output);
        }
    
    //Case for if the first word is Unsub
    }else if (strcmp(first_word, "Unsub") == 0){
        //send a message to unsubscribe the client to the given ID
        int skip = atoi(&input_message[6]);
        if ((skip == 0 && input_message[6] != '0') || (skip > 255 || skip < 0)){
            fprintf(output, "Invalid Channel: %s", &input_message[6]);
            fflush(output);
        }else{
            Unsub(client_id, atoi(&input_message[6]), output);
        }

    //Case for if the first word is send
    }else if(strcmp(first_word, "Send") == 0){

        //      ### CHANGE SO THAT YOU ONLY TAKE 1024 BYTES AFTER THE CHANNEL ID ###
        int channel = atoi(&input_message[5]);
        int add = 1;
        if (channel >= 100) {
            add = 3;
        } else if (channel >= 10) {
            add = 2;
        }
        Add_to_list(list, channel, &input_message[6 + add]);

    //Case for if the first word is channels
    }else if(strcmp(first_word, "Channels") == 0){
        //Let the server know it has to send info back
        Channels(list, client_id, output);

    //Case for if the first word is Livefeed either with or without the client ID
    }else if (strcmp(first_word, "Livefeed") == 0){
        if(input_message[8] == '\n'){
            Next2(client_id, output);
        }else {
            int channel = atoi(&input_message[9]);
            if ((channel == 0 && input_message[9] != '0') || (channel > 255 || channel < 0)){
                fprintf(output, "Invalid Channel: %s", &input_message[9]);
                fflush(output);
            }else{
                Next(channel, client_id, output);
            }
        }
        
    }

    //return 0 so that the while loop in main continues
    return 0;
}
