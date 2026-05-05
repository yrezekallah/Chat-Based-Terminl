#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX 256 // Maximum buffer size (message)
#define PORT 8080  // Port number
#define SA struct sockaddr // Short form of struct sockaddr
#define MAX_CLIENTS 10
#define MAX_WORDS 100

char *clientFile = "clientNames.txt";
char *adminFile = "adminNames.txt";
char *bannedIPFile = "bannedIPs.txt";
char *adminTools[] = {"/ban", "/unban", "/kick", "/promote"};
char *bannedWords_file = "banned_words.txt";

int clients[MAX_CLIENTS];
int client_count = 0;

char *clientNames[MAX_CLIENTS];

char bannedWords[MAX_WORDS][MAX];
int bannedCount = 0;

pthread_mutex_t lock;

void testForTools(char *message);
void testForAdminTools(char *message, int connfd);
void filterMessage(char *buff);
void broadcast(char *buff, int sender);
void removeClient(int connfd);
void addClient(int connfd, char *name);
char* funcWelcomeToServer(int connfd);
bool isAdmin(char *name);
void promoteUserToAdmin(char *name, int connfd);
bool isBanned(char *ip);
void banUserIP(char *ip);
void unbanUserIP(char *ip);
void kickUser(int connfd);
void clearScreen(void);

/**
 * Function designed for chat between client and server. It handles
 *  the login/signup process, filters messages, tests for admin tools,
 *  and broadcasts messages to other clients. It also handles client
 * disconnection and cleanup.
 */
void *func(void *arg)
{
    int connfd = *((int *)arg);
    free(arg);

    // Login / signup first, get username
    char *name = funcWelcomeToServer(connfd);
    if (name == NULL) {
        // client disconnected or error during login/signup
        close(connfd);
        return NULL;
    }

    // Add to global client list
    pthread_mutex_lock(&lock);
    if (client_count < MAX_CLIENTS) {
        clients[client_count] = connfd;
        clientNames[client_count] = name;
        client_count++;
    } else {
        pthread_mutex_unlock(&lock);
        char *fullMsg = "Server full. Try again later.\n";
        write(connfd, fullMsg, strlen(fullMsg));
        free(name);
        close(connfd);
        return NULL;
    }
    pthread_mutex_unlock(&lock);

    char buff[MAX];
    int bytesRead;

    while (1) {
        memset(buff, 0, MAX);

        bytesRead = read(connfd, buff, sizeof(buff) - 1);

        if (bytesRead <= 0) {
            printf("Client disconnected...\n");
            break;
        }

        buff[bytesRead] = '\0';

        if (strncmp(buff, "Exit", 4) == 0) {
            printf("Client exiting...\n");
            break;
        }

        testForTools(buff);
        testForAdminTools(buff, connfd);
        filterMessage(buff);

        printf("From client (%s): %s", name, buff);

        broadcast(buff, connfd);
    }

    // Remove from client list and free username
    pthread_mutex_lock(&lock);
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == connfd) {
            free(clientNames[i]);
            clients[i] = clients[client_count - 1];
            clientNames[i] = clientNames[client_count - 1];
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&lock);

    close(connfd);

    return NULL;
}

/*
 * Simple screen clear using ANSI escape codes
 */
void clearScreen(void){
    printf("\033[H\033[J");
    fflush(stdout);
}

char* funcWelcomeToServer(int connfd){
    char buff[MAX];

    char *welcomeMessage = "Welcome to the server!\nPlease enter your username.\n";
    write(connfd, welcomeMessage, strlen(welcomeMessage));

    memset(buff, 0, MAX);
    int n = read(connfd, buff, MAX - 1);
    if (n <= 0) {
        // client disconnected or error
        return NULL;
    }
    buff[n] = '\0';
    buff[strcspn(buff, "\r\n")] = 0;

    char *name = (char *)malloc(MAX);
    if (name) {
        strncpy(name, buff, MAX - 1);
        name[MAX - 1] = '\0';
        return name;
    }

    return NULL;
}

/**
 * Loads banned words from the banned_words.txt file into the
 *  bannedWords array. It also processes the words to remove any
 * brackets and convert them to lowercase for easier filtering later on.
 */
void loadBannedWords(){
    FILE *fp;
    char word[MAX];

    fp = fopen("banned_words.txt", "r");

    if (fp == NULL) {
        printf("No banned_words.txt found...\n");
        return;
    }

    while (fgets(word, MAX, fp) != NULL && bannedCount < MAX_WORDS) {
        word[strcspn(word, "\n")] = 0;
        strcpy(bannedWords[bannedCount], word);
        bannedCount++;
    }

    fclose(fp);

    printf("Loaded banned words...\n");
}

/**
 * Filters the input message by replacing any occurrences of
 * banned words with asterisks. It converts the message to
 *  lowercase for comparison and then replaces the characters
 *  in the original message with "****".
 */
void filterMessage(char *buff)
{
    char lower[MAX];

    strcpy(lower, buff);

    for (int i = 0; lower[i] != '\0'; i++) {
        lower[i] = tolower((unsigned char)lower[i]);
    }

    for (int i = 0; i < bannedCount; i++) {
        char *found = strstr(lower, bannedWords[i]);

        while (found != NULL) {
            int mute = found - lower;
            int length = strlen(bannedWords[i]);

            for (int j = 0; j < length; j++) {
                buff[mute + j] = '*';
                lower[mute + j] = '*';
            }

            found = strstr(lower, bannedWords[i]);
        }
    }
}

/**
 * Lists options available to admin
 * /help for admin tools
 * /clients to show number of clients
 * Exit to leave chat
 */
void printTools()
{
    printf("/help - show admin tools\n");
    printf("/clients - show number of clients\n");
    printf("Exit - client leaves chat\n");
}

/**
 * Tests the input message for admin commands and executes if /help
 * or /clients is found. /help will print the admin tools and
 *  /clients will print the number of clients currently connected to
 *  the server.
 */
void testForTools(char *message)
{
    if (strncmp(message, "/help", 5) == 0) {
        for(int i = 0; i < (int)(sizeof(adminTools) / sizeof(adminTools[0])); i++){
            printf("%s\n", adminTools[i]);
        }
    }
    else if (strncmp(message, "/clients", 8) == 0) {
        printf("Clients connected: %d\n", client_count);
    }
}

/**
 * Adds a client to the clients array and adds one to the client count
 * (Not used anymore exactly as before; logic moved into func.)
 */
void addClient(int connfd, char *name)
{
    pthread_mutex_lock(&lock);

    if (client_count < MAX_CLIENTS) {
        clients[client_count] = connfd;
        clientNames[client_count] = name;
        client_count++;
    }

    pthread_mutex_unlock(&lock);
}

/**
 * Removes a client from the clients array and subtracts one from the client count
 */
void removeClient(int connfd)
{
    pthread_mutex_lock(&lock);

    for (int i = 0; i < client_count; i++) {
        if (clients[i] == connfd) {
            free(clientNames[i]);
            clients[i] = clients[client_count - 1];
            clientNames[i] = clientNames[client_count - 1];
            client_count--;
            break;
        }
    }

    pthread_mutex_unlock(&lock);
}

/**
 * Broadcasts a message to all clients except the sender. It locks the mutex
 */
void broadcast(char *buff, int sender)
{
    pthread_mutex_lock(&lock);

    for (int i = 0; i < client_count; i++) {
        if (clients[i] != sender) {
            write(clients[i], buff, strlen(buff));
        }
    }

    pthread_mutex_unlock(&lock);
}

//--------------------------------------------------------------
int main()
{
    int sockfd;
    int connfd;
    int len;
    struct sockaddr_in servaddr;
    struct sockaddr_in cli;

    pthread_mutex_init(&lock, NULL);

    loadBannedWords();

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(0);
    }
    else {
        printf("Socket successfully created..\n");
    }

    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind failed...\n");
        exit(0);
    }
    else {
        printf("Socket successfully binded..\n");
    }

    if ((listen(sockfd, 5)) != 0) {
        printf("Listen failed...\n");
        exit(0);
    }
    else {
        printf("Server listening...\n");
    }

    len = sizeof(cli);

    while (1) {
        int *newConnfd = (int *)malloc(sizeof(int));
        if (!newConnfd) {
            continue;
        }

        connfd = accept(sockfd, (SA*)&cli, (socklen_t*)&len);

        if (connfd < 0) {
            printf("server accept failed...\n");
            free(newConnfd);
            continue;
        }

        // Check if banned by IP
        char *ipStr = inet_ntoa(cli.sin_addr); // Convert IP to string
        if (isBanned(ipStr)) {
            char *banMsg = "You are banned from this server.\n";
            write(connfd, banMsg, strlen(banMsg));
            close(connfd);
            free(newConnfd);
            continue;
        }

        printf("server accepted the client from %s...\n", ipStr);

        *newConnfd = connfd;

        pthread_t thread;
        pthread_create(&thread, NULL, func, newConnfd);
        pthread_detach(thread);
    }

    close(sockfd);

    return 0;
}
//--------------------------------------------------------------

/**
 * Tests the if the user is one the banned list. It reads the bannedIPs.txt file and checks if the provided IP matches any of the entries.
 *  If a match is found, it returns true, indicating that the user is banned. If no match is found after checking all entries, it returns false.
 */
bool isBanned(char *ip){
    FILE *fp = fopen("bannedIPs.txt", "r");
    if (fp == NULL) {
        return false;
    }

    char buff[MAX];

    while(fgets(buff, sizeof(buff), fp) != NULL){
        buff[strcspn(buff, "\n")] = 0;
        if(strcmp(buff, ip) == 0){
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    return false;
}

void banUserIP(char *ip){
    FILE *fp = fopen("bannedIPs.txt", "a");
    if (fp == NULL) {
        printf("Could not open bannedIPs.txt\n");
        return;
    }

    fprintf(fp, "%s\n", ip);
    fclose(fp);
}

void unbanUserIP(char *ip){
    FILE *fp = fopen("bannedIPs.txt", "r");
    if(fp == NULL){
        printf("Could not open bannedIPs.txt\n");
        return;
    }

    FILE *tempFp = fopen("tempBannedIPs.txt", "w");
    if(tempFp == NULL){
        fclose(fp);
        return;
    }

    char buff[MAX];

    while(fgets(buff, sizeof(buff), fp) != NULL){
        buff[strcspn(buff, "\n")] = 0;
        if(strcmp(buff, ip) != 0){
            fprintf(tempFp, "%s\n", buff);
        }
    }

    fclose(fp);
    fclose(tempFp);

    remove("bannedIPs.txt");
    rename("tempBannedIPs.txt", "bannedIPs.txt");
}

void kickUser(int connfd){
    char *kickMessage = "You have been kicked from the chat.\n";
    write(connfd, kickMessage, strlen(kickMessage));
    removeClient(connfd);
    close(connfd);
}

void testForAdminTools(char *message, int connfd){
    char *senderName = NULL;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == connfd) {
            senderName = clientNames[i];
            break;
        }
    }
    pthread_mutex_unlock(&lock);

    if (senderName == NULL || !isAdmin(senderName)) {
        return;
    }

    if(strncmp(message, "/ban", 4) == 0){
        char *ipToBan = message + 5;
        ipToBan[strcspn(ipToBan, "\r\n")] = 0;
        banUserIP(ipToBan);
    }
    else if (strncmp(message, "/unban", 6) == 0){
        char *ipToUnban = message + 7;
        ipToUnban[strcspn(ipToUnban, "\r\n")] = 0;
        unbanUserIP(ipToUnban);
    }
    else if (strncmp(message, "/kick", 5) == 0){
        char *nameToKick = message + 6;
        nameToKick[strcspn(nameToKick, "\r\n")] = 0;

        pthread_mutex_lock(&lock);
        int targetFd = -1;
        for (int i = 0; i < client_count; i++) {
            if (strcmp(clientNames[i], nameToKick) == 0) {
                targetFd = clients[i];
                break;
            }
        }
        pthread_mutex_unlock(&lock);

        if (targetFd != -1) {
            kickUser(targetFd);
        }
    }
    else if (strncmp(message, "/promote", 8) == 0){
        char *nameToPromote = message + 9;
        nameToPromote[strcspn(nameToPromote, "\r\n")] = 0;
        promoteUserToAdmin(nameToPromote, connfd);
    }
}

bool isAdmin(char *name){
    FILE *fp = fopen(adminFile, "r");
    if(fp == NULL){
        return false;
    }

    char buff[MAX];

    while(fgets(buff, sizeof(buff), fp) != NULL){
        buff[strcspn(buff, "\n")] = 0;
        if(strcmp(buff, name) == 0){
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    return false;
}

void promoteUserToAdmin(char *name, int connfd){
    if(isAdmin(name)){
        return;
    }

    FILE *fp = fopen(clientFile, "r");
    if(fp == NULL){
        return;
    }

    char line[MAX];
    bool foundUser = false;

    while(fgets(line, sizeof(line), fp) != NULL){
        line[strcspn(line, "\n")] = 0;

        if(strcmp(line, name) == 0){
            foundUser = true;
            break;
        }
    }

    fclose(fp);

    if(!foundUser){
        return;
    }

    FILE *adminFp = fopen(adminFile, "a");
    if(adminFp == NULL){
        return;
    }

    fprintf(adminFp, "%s\n", name);
    fclose(adminFp);

    char successMessage[MAX];
    snprintf(successMessage, MAX, "%s has been promoted to admin!\n", name);
    write(connfd, successMessage, strlen(successMessage));
}