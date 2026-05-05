#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <ncurses.h>

#define MAX 256
#define PORT 8080
//ncurse windows used for the ui
//chat_win shows all the messages
//input_win is the small box at the bottom where the user types
WINDOW *chat_win;
WINDOW *input_win;

//socket that connects the client to the server
int sockfd;
//stores the name the user types at the start of the program
char username[50];
//lock helps stop the recieve thread and maint thread from printing text at the same time
pthread_mutex_t screenLock;

//redraws the bottom input box every tim the user sends a message
void drawInputBox()
{
    werase(input_win); // clears the input box
    box(input_win, 0, 0); // draws the border
    mvwprintw(input_win, 1, 1, "%s: ", username); // prints username prompt
    wrefresh(input_win); // updates the input win
}

//prints message into the chat window
void printMessage(char *message)
{
    pthread_mutex_lock(&screenLock);

    //your own messages
    if(strncmp(message, username, strlen(username)) == 0){
        wattron(chat_win, COLOR_PAIR(1));
        wprintw(chat_win, "%s", message);
        wattroff(chat_win, COLOR_PAIR(1));
    }

    //system messages
    else if(strstr(message, "Welcome") != NULL ||
            strstr(message, "Exit") != NULL ||
            strstr(message, "Server") != NULL){
        wattron(chat_win, COLOR_PAIR(3));
        wprintw(chat_win, "%s", message);
        wattroff(chat_win, COLOR_PAIR(3));
    }

    //other users
    else{
        wattron(chat_win, COLOR_PAIR(2));
        wprintw(chat_win, "%s", message);
        wattroff(chat_win, COLOR_PAIR(2));
    }

    wrefresh(chat_win);

    pthread_mutex_unlock(&screenLock);
}

//function runs in background
//constant wait for messages from the server

void *receiveMessages(void *arg)
{
    char buff[MAX];
    int bytesRead;

    while(1){
        bzero(buff, MAX); // clears the buffer before reading

        //reads from server
        // if another client sends something , the server sends it here.
        bytesRead = read(sockfd, buff, sizeof(buff) - 1);

        //if read returns 0 or less , server disconnecte.

        if(bytesRead <= 0){
            printMessage("Server disconnected ... \n");
            break;
        }

        buff[bytesRead] = '\0'; // makes sure the message ends

        printMessage(buff); // displays the message from server


    }
    return NULL;
    }

    int main()
    {
        struct sockaddr_in servaddr;
        char buff[MAX];
        char message [MAX];

        // ask for username before starting the ncureses screen 
        printf("Enter username: ");
        fgets(username, sizeof(username), stdin);
        username[strcspn(username, "\n")] = 0;

        //if they press enter withut typing a name, use User.

        if(strlen(username) == 0) {
            strcpy(username, "User");
        }

        //creates the client socket
        //AF_INET means IPv4
        // SOCK_STREAM   means tcp.

        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if(sockfd == -1){
            printf("socket creation failed ... \n");
            exit(0);
        }
        printf("socket successfully created .. \n");

        //clears server address structure before filling it in
        bzero(&servaddr, sizeof(servaddr));

        // set server connection information
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(PORT);

        //127.0.0.1 means the server is running on the same computer running

        inet_pton(AF_INET, "10.49.131.17", &servaddr.sin_addr);

         //tries to connect to th server

         if(connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0)      {

             printf("Connection with server failed :( ...\n");
             exit(0);
         }

         printf("Connected to the server :) ... \n");

         // starts the lock used for safe screen printing 
         pthread_mutex_init(&screenLock, NULL);

         // starts ncurses mode.
         //this lets us draw windows in terminal 
         initscr();
         cbreak();
         noecho();
         start_color();

         //color pairs
         init_pair(1, COLOR_GREEN, COLOR_BLACK); // your messages
            init_pair(2, COLOR_CYAN, COLOR_BLACK); // other users
          init_pair(3, COLOR_YELLOW, COLOR_BLACK); // system messages
         int rows, cols;
         // gets terminal size
         getmaxyx(stdscr, rows, cols);

         chat_win = newwin(rows - 3, cols, 0, 0);

         //input window is the bottom 3 rows
         input_win = newwin(3, cols, rows - 3, 0);

         // allows th chat window to scroll when it fills up.

         scrollok(chat_win, TRUE);

         //draw first input box.
         box(input_win, 0, 0);
         wrefresh(input_win);
         printMessage ("Welcome to chatter Terminal \n");
         printMessage("Type Exit to leave the chat \n\n");
         // creates a separate thread that recieves messages while user types.

         pthread_t thread;
         pthread_create(&thread, NULL, receiveMessages, NULL);

         // main chat loop

         while(1){
             bzero(buff, MAX);
             bzero(message, MAX);
             // show the input box and username prompt.
             drawInputBox();
             // let the user see what they type.
             echo();
             // get message from the input window.
             wgetnstr(input_win, buff, MAX - 1);
             // turns echo back off after input.
             noecho();
             //if user types Exit, tell server and leave loop.
             if(strncmp(buff, "Exit", 4) == 0){
                 write(sockfd, "Exit", 4);
                 break;
             }

             // adds username before the message.
             snprintf(message, MAX, "%s: %s\n", username, buff);

             // sends essage to server.
             write(sockfd, message, strlen(message));

             // also prints your own message locally.
             printMessage(message);
             }

         endwin();
         //closes connection to server
         close(sockfd);
         return 0;


         } 