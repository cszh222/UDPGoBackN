#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket() and bind() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <string>
#include <signal.h>     /* for sigaction() */
#include <ctime>

using namespace std;

#define ECHOMAX 255     /* Longest string to echo */
#define PACKETSTORECV 30
#define TIMEOUTSEC 7

bool notTimedOut = true;

typedef struct {
    int type;
    int seq_no;
    int length;
    char data[512];
} packet;

typedef struct {
    int type;
    int ack_no;
} ack;

bool is_lost(float loss_rate);
void CatchAlarm(int ignored);
void DieWithError(string errorMessage);   /* Error handling function */  /* External error handling function */

int main(int argc, char *argv[])
{
    int sock;                        /* Socket */
    struct sockaddr_in echoServAddr; /* Local address */
    struct sockaddr_in echoClntAddr; /* Client address */
    unsigned int cliAddrLen;         /* Length of incoming message */
    char echoBuffer[ECHOMAX];        /* Buffer for echo string */
    unsigned short echoServPort;     /* Server port */
    int recvMsgSize;                 /* Size of received message */
    int chunkSize;
    float lossRate = 0.0;
    if (argc < 3 || argc > 4)         /* Test for correct number of parameters */
    {
        fprintf(stderr,"Usage:  %s <port_no> <chunk_size> [<loss_rate>]\n", argv[0]);
        exit(1);
    }

    echoServPort = atoi(argv[1]);  /* First arg:  local port */
    chunkSize = atoi(argv[2]);
    if(argc == 4){
        lossRate = atof(argv[3]);
        if(lossRate < 0.0 || lossRate > 1.0){
            fprintf(stderr, "loss_rate must be between 0 and 1\n");
            exit(1);
        }
    }

    /* Create socket for sending/receiving datagrams */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        DieWithError("socket() failed");

    /* Construct local address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr));   /* Zero out structure */
    echoServAddr.sin_family = AF_INET;                /* Internet address family */
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    echoServAddr.sin_port = htons(echoServPort);      /* Local port */

    /* Bind to the local address */
    if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
        DieWithError("bind() failed");

    int packet_rcvd = -1;
    packet* recvPkt = new packet;
    packet *recvBuff = new packet[PACKETSTORECV];
    ack* ackPkt = new ack;
    int ackSize =  2*sizeof(int);
    int pktSize = 3*sizeof(int) + chunkSize;
    //alarm for teardown timeout
    struct sigaction myAction;       /* For setting signal handler */
    /* Set signal handler for alarm signal */
    myAction.sa_handler = CatchAlarm;
    if (sigfillset(&myAction.sa_mask) < 0) /* block everything in handler */
        DieWithError("sigfillset() failed");
    myAction.sa_flags = 0;
    if (sigaction(SIGALRM, &myAction, 0) < 0)
        DieWithError("sigaction() failed for SIGALRM");
    srand48(time(NULL)); //seed for random;

    while (1) {/* Run forever */    
        /* Set the size of the in-out parameter */
        cliAddrLen = sizeof(echoClntAddr);

        /* Block until receive message from a client */
        if ((recvMsgSize = recvfrom(sock, (void *)recvPkt, pktSize, 0, (struct sockaddr *) &echoClntAddr, &cliAddrLen)) < 0){
            DieWithError("recvfrom() failed");
        }
        if(!is_lost(lossRate)){
            fprintf(stderr, "RECIEVED PACKET %d\n", recvPkt->seq_no);
            if(recvPkt->type == 1){
                //teardown packet recieved
                ackPkt->type = 2;
                ackPkt->ack_no = packet_rcvd;
                //message packet recieved            
                //send ack if sequence number match
                if((packet_rcvd + 1) == recvPkt->seq_no ){
                    /* Send received datagram back to the client */
                    //increment packet_rcvd
                    packet_rcvd = recvPkt->seq_no;
                    ackPkt->ack_no = packet_rcvd; 
                    //place packet in buffer  
                    recvBuff[packet_rcvd].type = recvPkt->type;
                    recvBuff[packet_rcvd].seq_no = recvPkt->seq_no;
                    recvBuff[packet_rcvd].length = recvPkt->length;
                    strncpy(recvBuff[packet_rcvd].data, recvPkt->data, recvPkt->length);
                }
                //send the ack
                fprintf(stderr, " -------- SEND ACK %d\n", ackPkt->ack_no);
                if (sendto(sock, (void *)ackPkt, ackSize, 0, (struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr)) != ackSize){
                        DieWithError("sendto() sent a different number of bytes than expected");
                    }
            }
            else if(recvPkt->type == 4) {
                //teardown packet recieved
                ackPkt->type = 8;
                packet_rcvd = recvPkt->seq_no;
                ackPkt->ack_no = packet_rcvd;
                //handle teardown
                alarm(TIMEOUTSEC);
                while(notTimedOut){
                    if(recvPkt->type == 4){
                        //send ack for teardown
                        if(!is_lost(lossRate)){
                            fprintf(stderr, " -------- SEND ACK %d\n", ackPkt->ack_no);
                            if (sendto(sock, (void *)ackPkt, ackSize, 0, (struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr)) != ackSize){
                                DieWithError("sendto() sent a different number of bytes than expected");
                            }
                        }                    
                    }
                    //recieve next packedt
                    recvMsgSize = recvfrom(sock, (void *)recvPkt, pktSize, 0, (struct sockaddr *) &echoClntAddr, &cliAddrLen);
                    if(recvMsgSize >= 0){
                        fprintf(stderr, "RECIEVED PACKET %d\n", recvPkt->seq_no);
                    }
                }
                //teardown has completed, reset packet_rcvd back tp 0 to restart recieving packets
                //fprintf(stderr, "TEARDOWN COMPLETE\n");
                packet_rcvd = -1;
                notTimedOut = true;
            }
        }
    }
    /* NOT REACHED */
}

void DieWithError(string errorMessage){
    fprintf(stderr, "%s\n", errorMessage.c_str());
    exit(1);
}

void CatchAlarm(int ignored) {    /* Handler for SIGALRM */
    //set nottimedout to false
    notTimedOut = false;
}

bool is_lost(float loss_rate){
    double rv;
    rv = drand48();
    return rv < loss_rate;
}