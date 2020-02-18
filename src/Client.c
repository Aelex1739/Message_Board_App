#include <stdio.h> 
#include <stdlib.h> 
#include <errno.h> 
#include <string.h> 
#include <netdb.h> 
#include <signal.h>
#include <sys/types.h> 
#include <netinet/in.h> 
#include <sys/socket.h> 
#include <unistd.h>
#include <string.h>

//declaration of important variables for the Client side
int my_id = 0;
volatile int livefeed = 0;
int sockfd = 0;
FILE* To_server;
#define MAX_MESSAGE_LENGTH = 1024;

//declaration of all the functions used on the client side
void rec_id(FILE* input);
void check_livefeed();
void input_handler(FILE* input, FILE* output, int sockfd);
void Bye(int sockfd, FILE* To_server);
void Send_recieve(char *message, FILE* input, FILE* output);
void Livefeed(char *message, FILE* input, FILE* output);
void Send(char *message, FILE* output);
char *first_word_handler(char *input_message);
//Define a list of channels which the client is subbed to, find a way to send this list to the server when the client closes and logs off

//main takes two arguments, host address and port number 
int main(int argc, char *argv[]){
    struct hostent *he;
    struct sockaddr_in host_addr;

    //Tells the program which function to call when it recieves a SIGINT
    signal(SIGINT, check_livefeed);

    // Checks to make sure a user has correctly used the program
    if (argc != 3){
        fprintf(stderr, "usage is as follows: client ip address, port number");
        exit(EXIT_FAILURE);
    }

    // Checks to see if the user has given a valid hostname 
    if((he=gethostbyname(argv[1])) == NULL){
        fprintf(stderr, "gethostbyname");
    }

    //checks to see if socket can be created
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        fprintf(stderr, "socket");
    }
    

    host_addr.sin_family = AF_INET;                                 //Singal family is ipv4
    host_addr.sin_port = htons(atoi(argv[2]));                      //Use the port number provided
    host_addr.sin_addr = *((struct in_addr *)he->h_addr_list[0]);   //Assign the sockaddr, host_addr with the address of the first recorded ip for the server
    
    // If connecting the socket fails, call error
    if (connect(sockfd, (struct sockaddr *)&host_addr, sizeof(struct sockaddr)) == -1){
        fprintf(stderr, "connect");
    }

    // variables local to the client
    FILE* From_server = fdopen(sockfd, "r");
    int sockfd_copy = dup(sockfd);
    To_server = fdopen(sockfd_copy, "w");
    
    //recieve the ID from the server 
    rec_id(From_server);
    while(1) {
        //loop this function to recieve info from the command line
        input_handler(From_server, To_server, sockfd);

    }
    return 0;
}


// LIST OF OFFICIAL COMMAND THE CLIENT REQUIRES

//Base function used for a lot of the communication for commands that send to recieve information
void Send_recieve(char *message, FILE* input, FILE* output){
    
    //allocate memory to the incoming message from command line 
    char *received = (char *)malloc(sizeof(char) * 1040);
    
    //print the message to the server as it has already been cross checked
    fprintf(output, "%s", message);
    fflush(output);

    //receive the return value from the server
    received = fgets(received, 1040, input);

    //check if the server has closed 
    if(received == NULL){
        printf("Server Closed Connection\n");
        fflush(stdout);
        Bye(sockfd, output);
    }

    //if there was nothing to recieve, recieve nothing, else, recieve the intended message
    if (strcmp(received, "nuttin\n") != 0) {
        printf("%s", received);
        fflush(stdout);
        fflush(input);
    }

    //free the allocated memory
    free(received);
}

//Function used for collecting channel message from server
void Channel(char *message, FILE* input, FILE* output){

    //Send the message to the server requesting information 
    fprintf(output, "%s", message);
    fflush(output);

    //while the server sends information, do:
    while (1){
        char *received = (char *)malloc(sizeof(char) * 40);

        //recieve information from the server 
        received = fgets(received, 40, input);
        //if the information is null, server closed
        if(received == NULL){
            printf("Server Closed Connection\n");
            fflush(stdout);
            Bye(sockfd, output);
        }
        //if the return is nothing, close the loop
        if(strcmp(received, "nuttin\n") == 0){
            break;
        }
        //otherwise print the returned information
        printf("%s", received);
        fflush(stdout);
        //free the memory for the message 
        free(received);
    }

}
//LIVEFEED(), takes no params and displays all of the unread messages 
void Livefeed(char *message, FILE* input, FILE* output){
    livefeed = 1;

    char *received = (char *)malloc(sizeof(char) * 1040);
    int channel = atoi(&message[9]);
    
    if ((message[8] != '\n') && ((channel == 0 && message[9] != '0') || (channel > 255 || channel < 0))){
        printf("Invalid Channel: %s", &message[9]);
        fflush(stdout);
        livefeed = 0;
    }else{

        while(livefeed == 1) {

            fprintf(output, "%s", message);
            fflush(output);

            received = fgets(received, 1040, input);
            char spare = received[26];

            if(received == NULL){
                printf("Server Closed Connection\n");
                fflush(stdout);
                Bye(sockfd, output);
            }
            received[26] = '\0';
            if(strcmp(received, "Not subscribed to channel:") == 0){
                received[26] = spare;
                printf("%s", received);
                fflush(stdout);
                livefeed = 0;
                break;
            }

            received[26] = spare;
            
            if (strcmp(received, "nuttin\n") != 0) {
                printf("%s", received);
                fflush(stdout);
                fflush(input);
            }
        }   
    }
}

//SEND(int channel_id, string/char message), takes two params and sends a new message to the channel with the given ID
//max size of a message is 1024 bytes 
void Send(char *message, FILE* output){
    int cheat = atoi(&message[5]);                                          // Turn the channel_id into an integer
    char *invalid = first_word_handler(&message[5]);

    if ((cheat == 0 && message[5] != '0') || (cheat > 255 || cheat < 0)){   // Check that the ID is valid
        printf("Invalid Channel: %s", invalid);                          //if invalid, tell them

    }else{
        fprintf(output, "%s", message);  
        fflush(output);                                   //otherwise, write to server
    }
}

//Function used to close connection to server and close the program
void Bye(int sockfd, FILE* To_server){
    //send a message to the server to close this clients socket
    fclose(To_server);     //Close the file pointer to this socket which will send a NULL terminator message which the Server will be waiting for
    close(sockfd);          //Close the socket which the Client is connected on
    printf("Closing Client\n");
    fflush(stdout);
    exit(EXIT_SUCCESS);     //exit the program
}

//function to find seperate the message given and the first word 
char *first_word_handler(char *input_message){

    char * first_word;                          //string for the first word
    int message_size = 20;                      //integer used to track how big the message itself is
    first_word = (char *)malloc(sizeof(char) * 20); 

    if (input_message == NULL){
        return NULL;
    }
    
    for(int i = 0; i < message_size; i++){
        if(input_message[i] == ' ' || input_message[i] == '\n'){
            first_word[i] = '\0';
            break;
        }else{
            first_word[i] = input_message[i];
        }
    }
    return first_word;
}

//Function that is used to collect information from the client and decide how to respond
void input_handler(FILE* input, FILE* output, int sockfd){

    char *input_message = (char *)malloc(sizeof(char) * 1040);

    input_message = fgets(input_message, 1040, stdin);

    // break up the message here so that i can check if the command is singluar or if it has an ID and associated parts to it.
    //find a way to store each part of the string so that it can be used later.
    char *first_word = first_word_handler(input_message);

    if(strcmp(first_word, "Bye") == 0){
        //send message to server to disconnect client
        Bye(sockfd, output);

    }else if(strcmp(first_word, "Livefeed") == 0){
        //need to split the string into the first word and if there is a second character after the livefeed reference that number in the call to livefeed on the server
        //      ### CHANGE TO NEXT FROM LIVEFEED ###
        Livefeed(input_message, input, output);

    }else if(strcmp(first_word, "Next") == 0){
        //break up the message and see if it has a number next to it and if so, what is that number and send them both
        Send_recieve(input_message, input, output);

    }else if (strcmp(first_word, "Sub") == 0){
        // Send a message to sub the client to a channel with the ID given
        Send_recieve(input_message, input, output);

    }else if (strcmp(first_word, "Unsub") == 0){
        //send a message to unsubscribe the client to the given ID
        Send_recieve(input_message, input, output);

    }else if(strcmp(first_word, "Send") == 0){
        //Send the given message to the server.
        Send(input_message, output);

    }else if(strcmp(first_word, "Channels") == 0){
        //Let the server know it has to send info back
        Channel(input_message, input, output);

    }else {
        //let the user know which commands they can use
        printf("Sorry, your command could not be interpreted, Please write one of the following commands: \n");
        printf("        - Sub <channel_id>\n\
        - Unsub <channel_id>\n\
        - Livefeed/Livefeed <channel_id>\n\
        - Next/Next <chanel_id>\n\
        - Send <channel_id> <message>\n\
        - Bye \n");
    }
}

//Function for recieving ID from the client
void rec_id(FILE* input){
    
    char *recieved = (char *)malloc(sizeof(char) * 10);

    recieved = fgets(recieved, 10, input);

    my_id = atoi(recieved);    
    if (my_id == 0 && recieved[0] != '0'){
        printf("%s", recieved);
        printf("recieved invalid ID\n");
    }else {
        printf("Welcome! Your client ID is: %i\n", my_id);
    }
}

//Function for checking if the program is in a livefeed or not and wether or not to close the client 
void check_livefeed(){
    if(livefeed == 1){
        livefeed = 0;
    }else{
        Bye(sockfd, To_server);
    }
    
}
