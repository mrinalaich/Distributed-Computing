/* 
 * Author : Mrinal Aich
 * Algorithm : Spanning Tree based Termination Detection
 */
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <ctime>
#include <vector>
#include <queue>
#include <set>
#include <map>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#define uint unsigned int
#define ulong unsigned long

#define SUCCESS 1
#define FAILURE 0

#define DEBUG 0

#define BUFSIZE 256
#define IPV4_ADDR_LEN 20
#define THREAD_SLEEP_MILLISECONDS 100
#define GET_LOCAL_TIME() char strTime[32] = {0}; \
                         struct tm *localTime; \
                         time_t currentTime; \
                         time( &currentTime ); \
                         localTime = localtime( &currentTime ); \
                         sprintf(strTime, "%u:%u:%u", localTime->tm_hour, localTime->tm_min, localTime->tm_sec);

#define LOG_MESSAGE(format, ...)    { \
                                        pthread_mutex_lock(&fileLock); \
                                        logFile = fopen(logFileName, "a+"); \
                                        fprintf(logFile, format "\n", ##__VA_ARGS__); \
                                        fclose(logFile); \
                                        pthread_mutex_unlock(&fileLock); \
                                    }

/* Data Structures */
/* Parameters of Neighbours */ 
typedef struct neighbourParams {
    char ipAddr[IPV4_ADDR_LEN];
    uint port;
} nbrParams;

/* Enum: Type of Message */
typedef enum e_msgType {
    APPLICATION = 2,
    REPEAT = 3,
    WHITE_TOKEN = 0,
    BLACK_TOKEN = 1
} eMsgType;

/* Enum: Type of State */
typedef enum e_State {
    PASSIVE = 0,
    ACTIVE = 1
} eState;

/* Enum: Token Color */
typedef enum e_TokenColor {
    WHITE = 10,
    BLACK = 11
} eTokenColor;

/* Parameters */
// Problem statement related params
uint                        nodeId, nodes, minMsgLt, maxMsgLt, interEventTime, maxSent;
uint                        msgSentPhase, msgToBeSentPhase;
ulong                       msgSent = 0;
std::vector<uint>           nbrs;
ulong                       noControlMsgs;
ulong                       noChildCtrlMsgs;

// Algorithmic params
uint                        parentId, rootId;
std::set<uint>              childrenIdSet;
std::set<uint>              rcvdChildToken;
uint                        maxHopCount;
bool                        termDetectState;
eState                      state;
eTokenColor                 token;

// Socket/IP Layer related params
char                        selfAddr[IPV4_ADDR_LEN];
uint                        selfPort;
std::map<uint,nbrParams>    nbrMap;
std::map<uint,uint>         nbrSockfd;

// System related params
pthread_mutex_t             sharedLock, fileLock;
FILE*                       logFile;
char                        logFileName[25]; 

/* Function Protoypes */
// General Functions
void retreiveNodeParams(uint nodeId, char *fileName, char* ipAddr, uint &port);
bool retreiveNodeRelatives();
void serverUpdateToken(char* message);
void sendMessage(uint sockfd, uint nbrId, bool tokenMsg, bool repeatSignalMsg);

// Client Functions
void *ClientThreadFunction(void* params);
bool ClientConnectToNbr(uint &sockfd, char* nbrAddr, uint nbrPort);

// Server Functions
void *ServerThreadFunction(void* params);
bool HandleMessage(uint clntSock);
bool AcceptTCPConnection(uint servSock, uint &clntSock);

int main(int argc, char *argv[])
{
    uint i,j,k,u,v;
    uint temp, tempPort;
    int rc;
    char tempAddr[IPV4_ADDR_LEN];
    pthread_t ptrServerThread, ptrClientThread;
    bool initActive = false;

    // Sanity Check
    if(argc < 2)
    {
        printf("Please mention the Node identifier of this process.\n");
        return 0;
    }

    // Node - Identifier
    nodeId = atoi(argv[1]);
    printf("Node-Id: %u\n", nodeId);

    // Log File Name
    sprintf(logFileName, "logFile_%u.txt", nodeId);

    // Restart Log-file
    logFile = fopen(logFileName, "w");
    fprintf(logFile, " ");
    fclose(logFile);

    // Input Parameters FileName
    if (argc == 3)
        freopen((char*)argv[2], "r", stdin);
    else
        freopen("in-params.txt", "r", stdin);

    scanf("%u %u %u %u %u\n", &nodes, &minMsgLt, &maxMsgLt, &interEventTime, &maxSent);
    scanf("%u\n", &rootId);
    // Check if process is initially Active...
    {
        char tempBuff[BUFSIZE] = {0}, *strtokPtr = NULL;
        while(scanf("%[^\EOF]", tempBuff) != EOF);
        
        //Initially nodes 2 5 7 8 are active.
        LOG_MESSAGE("Initially node %s are active...", tempBuff);

        strtokPtr = strtok(tempBuff, " ");
        while(strtokPtr != NULL)
        {
            if(atoi(strtokPtr) == nodeId)
            {
                LOG_MESSAGE("Node is initally Active.");
                initActive = true;
            }
            strtokPtr = strtok(NULL, " ");
        }
    }

    // Input Node-Parameters
    if (argc == 4)
        freopen((char*)argv[3], "r", stdin);
    else
        freopen("topology.txt", "r", stdin);

    scanf("%u", &temp);
    
    // Sanity Check
    if(temp != nodes)
    {
        LOG_MESSAGE("Mismatch in %s and %s configuration of nodes. Exiting...\n",
                    argc == 3 ? (char*)argv[2] : "in-params.txt",
                    argc == 4 ? (char*)argv[3] : "topology.txt");
        return 0;
    }

    // Retrieve self node's IP Address and Port
    retreiveNodeParams(nodeId, argc == 4 ? (char*)argv[3] : (char*)"topology.txt", selfAddr, selfPort);
    if(DEBUG) {
        LOG_MESSAGE("Self - IP: %s | Port: %u", selfAddr, selfPort);
    }
    
    // Retrieve Neighbours
    char tempBuff[BUFSIZE];
    for(i=0;i<nodes;i++)
    {
        scanf("%u ", &temp);
        memset(tempBuff, 0x00, sizeof(tempBuff));
        scanf("%[^\n]\n", tempBuff);

        if(temp == nodeId)
        {
            char *tempPtr;
            tempPtr = strtok(tempBuff, " ");
            while(tempPtr != NULL)
            {
                nbrs.push_back(atoi(tempPtr));
                tempPtr = strtok(NULL, " ");
            }
        }
    }

    // Retreive Spanning Tree's Parent and Children
    if( SUCCESS != retreiveNodeRelatives())
    {
        LOG_MESSAGE("Incorrect Spanning Tree in configuration File.");
        exit(1);
    }
    else
    {
        char buf[BUFSIZE] = {0}, temp[BUFSIZE] = {0};
        for(auto it = childrenIdSet.cbegin(); it !=childrenIdSet.cend(); it++)
        {
            memset(temp, 0x00, sizeof(temp));
            sprintf(temp, "%u ", *it);
            strcat(buf, temp);
        }

        // If Leaf node
        if(childrenIdSet.size() == 0)
        {
            rcvdChildToken.clear();
            childrenIdSet.clear();
        }
        if(DEBUG)
        {
            LOG_MESSAGE("Parent: %u | Children: %s", parentId, buf);
        }
    }
    fflush(stdin);

    // Retreive Node's Parameters
    uint nbrId;
    for(auto it=nbrs.cbegin(); it != nbrs.cend(); it++)
    {
        nbrId = *it;
        retreiveNodeParams(*it, argc == 4 ? (char*)argv[3] : (char*)"topology.txt", tempAddr, tempPort);
        fflush(stdin);
        strcpy(nbrMap[nbrId].ipAddr, tempAddr);
        nbrMap[nbrId].port = tempPort;
    }

    // Initialize Global Locks
    pthread_mutex_init(&fileLock,0);
    pthread_mutex_init(&sharedLock,0);

    termDetectState = true;
    token = WHITE;
    rcvdChildToken.clear();

    // Activate Node, if configured
    if(initActive == true)
    {
        pthread_mutex_lock(&sharedLock);

        state = ACTIVE;
        msgSentPhase = 0;
        srand (time(NULL));
        msgToBeSentPhase = minMsgLt + (rand() % (maxMsgLt - minMsgLt));
       
        pthread_mutex_unlock(&sharedLock);
    }

    // Create Server Thread
    rc = pthread_create(&ptrServerThread, NULL, ServerThreadFunction, NULL);
    if (rc)
    {
        LOG_MESSAGE("Error:unable to create Server thread");
    }

    // Delay added for Server Sockets to get ready
    sleep(3);

    // Create Client Thread
    rc = pthread_create(&ptrClientThread, NULL, ClientThreadFunction, NULL);
    if(rc)
    {
        LOG_MESSAGE("Error:unable to %d create Client thread ", rc);
    }

    pthread_join(ptrServerThread, NULL);
    pthread_join(ptrClientThread, NULL);

    // Cleanup Memory
    pthread_mutex_destroy(&fileLock);
    pthread_mutex_destroy(&sharedLock);

    return 0;
}

void serverUpdateToken(char* message)
{
    uint nbrId;
    eMsgType msgType;
    char *strtokPtr = NULL, tempMsg[BUFSIZE] = {0};
    strcpy(tempMsg, message);

    strtokPtr = strtok(message, "|");
    do
    {
        // Retreive - NbrId    
        nbrId = atoi(strtokPtr);
    
        // Retreive - Type of message
        strtokPtr = strtok(NULL, "|");

        msgType = (!strncmp(strtokPtr,"A",1)) ? APPLICATION : (!strncmp(strtokPtr,"R",1)) ? REPEAT : (!strncmp(strtokPtr,"W",1)) ? WHITE_TOKEN:BLACK_TOKEN;

        pthread_mutex_lock(&sharedLock);

        /*
        If Rcvd:TOKEN Message then
            Update RcvdTokenChildSet
            Change color of token
            If State: Passive
                If rcvd all child tokens and termDetectState then
                    If Not root then
                        Send Token to its Parent
                    Else
                        Check for Termination

        Else If Rcvd:REPEAT Message then
            Change TerminateDetectState
            Change TokenColor
            Send Repeat Signal to children

        Else : Application Message
            If state: Passive then
                If not reached limit then
                    Go Active
                    Send random msgs to Neighbors            
        */

        // If Rcvd:TOKEN Message
        if(msgType == WHITE_TOKEN || msgType == BLACK_TOKEN)
        {
            // Retreived Hop Count - W(%u)
            char tempCh;
            uint hopCount = 0;
            ulong rcvdCtrlMsg = 0;
            sscanf(strtokPtr, "%c(%u,%lu)", &tempCh, &hopCount, &rcvdCtrlMsg);

            if(hopCount > maxHopCount)
                maxHopCount = hopCount;

            noChildCtrlMsgs += rcvdCtrlMsg;

            GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u receives Token: %s from Child %u at depth: %u.", 
                strTime, nodeId, msgType == WHITE_TOKEN ? "White" : "Black", nbrId, hopCount);

            // Update color of token
            if(msgType == BLACK_TOKEN)
            {
                if(token == WHITE && DEBUG)
                    LOG_MESSAGE("Token turns: Black");

                token = BLACK;
            }

            // Update RcvdTokenChildSet
            rcvdChildToken.insert(nbrId);

            // If State Passive and all Tokens child tokens rcvd then
            if(state == PASSIVE)
            {
                if((termDetectState == true) && (rcvdChildToken.size() == childrenIdSet.size()))
                {
                    rcvdChildToken.clear();
                    // Check if node is root
                    if(nodeId != rootId)
                    {
                        // Send Token to its Parent
                        sendMessage(nbrSockfd[parentId], parentId, true, false);
                        token = WHITE;
                        termDetectState = false;
                        GET_LOCAL_TIME();
                        LOG_MESSAGE("%s Node %u sends %s Token to its parent %u.", strTime, nodeId, token == WHITE ? "White" : "Black", nbrId);
                    }
                    // Check for Termination detection
                    else
                    {
                        GET_LOCAL_TIME();
                        // Announce successful Termination Detection
                        if(token == WHITE)
                        {
                            LOG_MESSAGE("%s Node %u has detected global Termination. Control Msgs: %lu.",
                                            strTime, nodeId, noChildCtrlMsgs + noControlMsgs);
                            termDetectState = false;
                        }
                        // Termination Failed
                        else
                        {
                            LOG_MESSAGE("%s Node %u broadcasts Repeat signal.", strTime, nodeId);

                            // Send Repeat Signal to children
                            for(auto it = childrenIdSet.cbegin(); it != childrenIdSet.cend(); it++)
                            {
                                nbrId = *it;
                                sendMessage(nbrSockfd[nbrId], nbrId, false, true);
                            }

                            token = WHITE;
                            rcvdChildToken.clear();
                            noChildCtrlMsgs = 0;
                        }
                    }
                }
            }
        }
        // If Rcvd:REPEAT Message
        else if(msgType == REPEAT)
        {
            termDetectState = true;
            token = WHITE;
            rcvdChildToken.clear();
            maxHopCount = 0;
            noChildCtrlMsgs = 0;
            // Send Repeat Signal to children
            for(auto it = childrenIdSet.cbegin(); it != childrenIdSet.cend(); it++)
            {
                nbrId = *it;
                sendMessage(nbrSockfd[nbrId], nbrId, false, true);
            }
        }
        // If: Application Message
        else if(msgType == APPLICATION)
        {
            GET_LOCAL_TIME();
            LOG_MESSAGE("%s Node %u receives a message from Node %u.", strTime, nodeId, nbrId);

            // If state: Passive
            if(state == PASSIVE)
            {
                // If not reached limit then
                if(msgSent != maxSent)
                {
                    // Go Active
                    state = ACTIVE;
                    msgSentPhase = 0;
                    srand (time(NULL));
                    msgToBeSentPhase = minMsgLt + (rand() % (maxMsgLt - minMsgLt));

                    // 10:03 Node 4 becomes active.
                    GET_LOCAL_TIME();
                    LOG_MESSAGE("%s Node %u becomes active.", strTime, nodeId);
                }
            }
        }

        pthread_mutex_unlock(&sharedLock);

        strtokPtr = strtok(NULL, "|");
        if(strtokPtr != NULL && DEBUG)
        {
            LOG_MESSAGE("Observed multiple messages in one packet: %s. Have to handle it!!!", tempMsg);
        }

    } while(strtokPtr != NULL);

    return;
}

void sendMessage(uint sockfd, uint nbrId, bool tokenMsg, bool repeatSignalMsg)
{
    char message[BUFSIZE] = {0};
    memset(message, 0x00, sizeof(message));

    GET_LOCAL_TIME();

    // Send Token Message to Parent
    if(tokenMsg)
    {
        // Concatenate Node-id with MsgType
        uint depth = 0;
        /* If leaf node: then depth = 1
           Else: depth = child(depth) + 1
        */  
        if(childrenIdSet.size() != 0)
            depth = maxHopCount;

        noControlMsgs++;
        sprintf(message,"%u|%c(%u,%lu)|", nodeId, token == WHITE ? 'W' : 'B', depth + 1, noControlMsgs + noChildCtrlMsgs);
    }
    else if(repeatSignalMsg)
    {
        // Concatenate Node-id with MsgType
        sprintf(message,"%u|R|", nodeId);
        noControlMsgs++;
    }
    // Sends Application Message to Neighbor
    else
    {
        // Concatenate the message with Node-id
        sprintf(message,"%u|A|", nodeId);
    }

    // Send message to neighbour
    size_t messageLen = strlen(message);
    ssize_t sentLen = send(sockfd, message, messageLen, 0);
    if (sentLen < 0) {
        perror("send() failed");
    } else if (sentLen != messageLen) {
        perror("send(): sent unexpected number of bytes");
    } else {
        if(DEBUG)
        {
            LOG_MESSAGE("Message: %s", message);
        }
    }
    return;
}

void *ClientThreadFunction(void* params)
{
    char *message, nbrAddr[IPV4_ADDR_LEN];
    uint sockfd;
    uint nbrId, nbrPort;
    nbrSockfd.clear();

    for(auto it=nbrMap.cbegin(); it!=nbrMap.cend(); it++)
    {
        nbrId = it->first;
        strcpy(nbrAddr, (char*)it->second.ipAddr);
        nbrPort = it->second.port;

        if( SUCCESS != ClientConnectToNbr(sockfd, nbrAddr, nbrPort))
        {
            LOG_MESSAGE("Unable to connect to %u at %s:%u.", nbrId, nbrAddr, nbrPort);
            pthread_exit(NULL);
        }
        else
        {
            if(DEBUG) {
                LOG_MESSAGE("Connected to %u at %s:%u. Going to sleep.", nbrId, nbrAddr, nbrPort);
            }
            nbrSockfd[nbrId] = sockfd;
        }
    }

    LOG_MESSAGE("Connected to all neighbours.");
    sleep(5);

    do
    {
        pthread_mutex_lock(&sharedLock);
        GET_LOCAL_TIME();
        LOG_MESSAGE("%s State: %s and token: %s and %sDetecting | Ctrl:%lu.", strTime,
                        state == PASSIVE ? "Passive" : "Active",
                        token == WHITE ? "White" : "Black",
                        termDetectState == true ? "" : "Non-",
                        noControlMsgs);

        /*      
        If state: Passive then
            Check termDetectState and rcvd all Tokens from Children then
                If not root
                    Send Token to parent
                    Change token color
                    Change termDetectState
                Else
                    Check for termination

        Else state: Active then
            Choose any neighbour to send Application Msg
            Change token color
            If termination satisfies then
                Go passive
                If all token rcvd from children and termDetectState then
                    If node is root
                        Check for termination detection
                    Else
                        If rcvd all Tokens from Children then
                            Send Token to parent
                            Change token color
                            Change termDetectState
        */

        // Process is Passive
        if(state == PASSIVE)
        {
            if(termDetectState == true && (rcvdChildToken.size() == childrenIdSet.size()))
            {
                // Check for termination
                if(nodeId == rootId)
                {
                    // Announce Termination Detection
                    if(token == WHITE)
                    {
                        GET_LOCAL_TIME();
                        LOG_MESSAGE("%s Node %u has detected global Termination. Control Msgs: %lu.",
                                            strTime, nodeId, noChildCtrlMsgs + noControlMsgs);
                        termDetectState == false;
                    }
                    // Failed to detect Termination, send outward Repeat Signal
                    else
                    {
                        GET_LOCAL_TIME();
                        LOG_MESSAGE("%s Node %u broadcasts Repeat signal.", strTime, nodeId);

                        // Repeat Signal to all neighbors
                        for(auto it = childrenIdSet.cbegin(); it != childrenIdSet.cend(); it++)
                        {
                            nbrId = *it;
                            sendMessage(nbrSockfd[nbrId], nbrId, false, true);
                        }
                    }
                }
                else
                {
                    // Send Token to parent
                    GET_LOCAL_TIME();
                    LOG_MESSAGE("%s Node %u sends its Token: %s to Parent: %u.", 
                        strTime, nbrId, token == WHITE ? "White" : "Black", parentId);
                    sendMessage(nbrSockfd[parentId], parentId, true, false);
                    token = WHITE;
                    termDetectState = false;
                }
            }
        }
        else // Process is Active
        {
            GET_LOCAL_TIME();

            // Send message to a random neighbour
            srand(time(NULL));
            uint nbrId = nbrs[rand() % nbrs.size()];
            sendMessage(nbrSockfd[nbrId], nbrId, false, false);
            
            // 10:02 Node 5 send a message to Node 9.
            LOG_MESSAGE("%s Node %u sends a message to Node %u.", strTime, nodeId, nbrId);

            //Change token to Black 
            token = BLACK;

            msgSentPhase++;
            msgSent++;

            if(DEBUG) {
                LOG_MESSAGE("This Phase: %u | Total: %lu", msgSentPhase, msgSent);
            }

            // If termination criterion statisfies, go Passive
            if((msgSentPhase == msgToBeSentPhase) || (msgSent == maxSent))
            {
                // Inter-event Time
                usleep(interEventTime);
   
                // 10:30 Node 2 announces termination
                GET_LOCAL_TIME();
                LOG_MESSAGE("%s Node %u announces termination.", strTime, nodeId);

                state = PASSIVE;

                if((termDetectState == true) && (childrenIdSet.size() == rcvdChildToken.size()))
                {
                    // Check for termination detection
                    if(nodeId == rootId)
                    {
                        // Announce Termination Detection
                        if(token == WHITE)
                        {
							LOG_MESSAGE("%s Node %u has detected global Termination. Control Msgs: %lu.",
                                            strTime, nodeId, noChildCtrlMsgs + noControlMsgs);
                            termDetectState == false;
                        }
                        // Failed to detect Termination, send outward Repeat Signal
                        else
                        {
                            GET_LOCAL_TIME();
                            LOG_MESSAGE("%s Node %u broadcasts Repeat signal.", strTime, nodeId);

                            // Repeat Signal to all neighbors
                            for(auto it = childrenIdSet.cbegin(); it != childrenIdSet.cend(); it++)
                            {
                                nbrId = *it;
                                sendMessage(nbrSockfd[nbrId], nbrId, false, true);
                            }

                            token = WHITE;
                            rcvdChildToken.clear();
                        }
                    }
                    // Send Token to its parent
                    else
                    {
                        GET_LOCAL_TIME();
                        LOG_MESSAGE("%s Node %u sends %s Token to its parent %u.", strTime, nodeId, token == WHITE ? "White" : "Black", parentId);

                        sendMessage(nbrSockfd[parentId], parentId, true, false);
                        token = WHITE;
                        termDetectState = false;
                    }
                }
            }
        }
        pthread_mutex_unlock(&sharedLock);

        // Inter-event Time
        usleep(interEventTime);

    } while(true);

    close(sockfd);
    pthread_exit(NULL);
}

bool retreiveNodeRelatives()
{
    uint tempParent, tempChild;
    char tempBuffer[BUFSIZE] = {0}, *strtokPtr;
    childrenIdSet.clear();

    while(scanf("%[^\n\EOF]\n", tempBuffer) != EOF)
    {
        strtokPtr = strtok(tempBuffer, " ");
        tempParent = atoi(strtokPtr);

        strtokPtr = strtok(NULL, " ");
        // Retreive info about its children
        if(tempParent == nodeId)
        {
            while(strtokPtr != NULL)
            {
                childrenIdSet.insert(atoi(strtokPtr));
                strtokPtr = strtok(NULL, " ");
            }
        }
        // Check if this is the parent
        else 
        {
            while(strtokPtr != NULL)
            {
                if(atoi(strtokPtr) == nodeId)
                {
                    if(parentId != 0)
                        return FAILURE;
                    else
                        parentId = tempParent;
                }
                strtokPtr = strtok(NULL, " ");
            }
        }
    }

    // Root is parent to itself
    if(nodeId == rootId)
        parentId = nodeId;

    return (parentId != 0) ? SUCCESS : FAILURE;
}

void retreiveNodeParams(uint nodeId, char *fileName, char* ipAddr, uint &port)
{
    uint temp,tempPort;
    char tempIpAddr[IPV4_ADDR_LEN];
    // Read from configuration file
    fflush(stdin);
    freopen(fileName, "r", stdin);
    scanf("%u\n", &temp);

    // Retrieve node's IP Address
    for(uint i=0;i<nodes;i++)
    {
        memset(tempIpAddr, 0x00, sizeof(tempIpAddr));
        // Format : 1 - 127.0.0.1:3333
        scanf("%u - %15[^:]:%u\n", &temp, tempIpAddr, &tempPort);

        if(temp == nodeId)
        {
            strcpy(ipAddr, tempIpAddr);
            port = tempPort;
        }
    }
    return;
}

/* Interprets the Message from the Neighbour */
bool HandleMessage(uint clntSock)
{
    bool retVal = SUCCESS;
	// Receive data
	char message[BUFSIZE];
	memset(message, 0, BUFSIZE);

    do
    {
        ssize_t recvLen = recv(clntSock, message, BUFSIZE, 0);
        if (recvLen < 0)
        {
            perror("recv() failed");
            retVal = FAILURE;
            break;
        }
        else if(recvLen == 0)
        {
            retVal = FAILURE;
            break;
        }
        message[recvLen] = '\0';

        if(DEBUG) {
            LOG_MESSAGE("Node-%u: Received Message - %s", nodeId, message);
        }

        // Update Token and its State
        serverUpdateToken(message);
    } while(0);

	return retVal;
}

bool AcceptTCPConnection(uint servSock, uint &clntSock) 
{
    bool retVal = SUCCESS;

    do
    {
        struct sockaddr_in clntAddr;
	    socklen_t clntAddrLen = sizeof(clntAddr);

        // Wait for a client to connect
        clntSock = accept(servSock, (struct sockaddr *) &clntAddr, &clntAddrLen);
        if (clntSock < 0) {
            perror("accept() failed");
            retVal = FAILURE;
            break;
        }

        char clntIpAddr[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &clntAddr.sin_addr.s_addr, clntIpAddr, sizeof(clntIpAddr)) == NULL)
        {
            LOG_MESSAGE("Unable to get client IP Address");
            retVal = FAILURE;
            break;
        }
    } while(0);

	return retVal;
}

void *ServerThreadFunction(void* params)
{
    uint servSock;
    char servIP[64];
    strcpy(servIP, selfAddr);
    in_port_t servPort = selfPort;

	// create socket for incoming connections
	if ((servSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
		perror("socket() failed");
		exit(-1);
	}

  	// Set local parameters
	struct sockaddr_in servAddr;
	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(servPort);

	// Bind to the local address
	if (bind(servSock, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
		perror("bind() failed");
		exit(-1);
	}

	// Listen to the client
	if (listen(servSock, nodes) < 0) {
		perror("listen() failed");
		exit(-1);
	}

	// Prepare for using select()
	fd_set orgSockSet; // Set of socket descriptors for select
	FD_ZERO(&orgSockSet);
	FD_SET(servSock, &orgSockSet);
	int maxDescriptor = servSock;

	// Setting select timeout as Zero makes select non-blocking
	struct timeval timeOut;
	timeOut.tv_sec = 0; // 0 sec
	timeOut.tv_usec = 100; // 0 microsec

    // Server Loop
	do
    {
		// The following process has to be done every time
		// because select() overwrite fd_set.
		fd_set currSockSet;
		memcpy(&currSockSet, &orgSockSet, sizeof(fd_set));

		select(maxDescriptor + 1, &currSockSet, NULL, NULL, &timeOut);

		for (uint currSock = 0; currSock <= maxDescriptor; currSock++)
        {
			if (FD_ISSET(currSock, &currSockSet))
            {
				// A new client, Establish TCP connection, 
                // register a new socket to fd_sed to watch with select()
				if (currSock  == servSock)
                {
					uint newClntSock;
					if (SUCCESS != AcceptTCPConnection(servSock, newClntSock))
                    {
                        LOG_MESSAGE("Unable to Accept Connection from Neighbor");
                        exit(-1);
                    }

					FD_SET(newClntSock, &orgSockSet);
					if (maxDescriptor < newClntSock)
						maxDescriptor = newClntSock;
				}
                // Handle the message
				else
                {
					if( SUCCESS != HandleMessage(currSock))
						FD_CLR(currSock, &orgSockSet);
				}
            }
        }

    } while(true);

	int closingSock;
	for (closingSock = 0; closingSock < maxDescriptor + 1; closingSock++)
		close(closingSock);

    pthread_exit(NULL);
}

bool ClientConnectToNbr(uint &sockfd, char* nbrAddr, uint nbrPort)
{
    bool retVal = SUCCESS;
    do
    {
        char servIP[64];
        strcpy(servIP, nbrAddr);
        in_port_t servPort = nbrPort;

        if(DEBUG) {
            LOG_MESSAGE("Connecting to IP: %s | Port: %u", servIP, servPort);
        }

        //Creat a socket
        sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd < 0) {
            perror("socket() failed");
            retVal = FAILURE;
            break;
        }

        // Set the server address
        struct sockaddr_in servAddr;
        memset(&servAddr, 0, sizeof(servAddr));
        servAddr.sin_family = AF_INET;
        int err = inet_pton(AF_INET, servIP, &servAddr.sin_addr.s_addr);
        if (err <= 0) {
            perror("inet_pton() failed");
            retVal = FAILURE;
            break;
        }
        servAddr.sin_port = htons(servPort);

        // Connect to server
        while(connect(sockfd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) 
        {

        }
    } while(0);

    return retVal;
}
