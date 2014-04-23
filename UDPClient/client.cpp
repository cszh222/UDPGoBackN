#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), connect(), sendto(), and recvfrom() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() and alarm() */
#include <errno.h>      /* for errno and EINTR */
#include <signal.h>     /* for sigaction() */
#include <algorithm>
#include <string>

using namespace std;

#define TIMEOUT_SECS    7       /* Seconds between retransmits */
#define MAXTRIES        10       /* Tries before giving up */
#define MAXTEARDOWN     10      //max teardown tries
#define PACKETSTOSEND   30      /*send 30 packets with same data for this program*/

char buffer[512]="Here is the message. Put about eight thousand characters here!!!";
int tries=0;   /* Count of times sent - GLOBAL for signal-handler access */

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

void DieWithError(string errorMessage);   /* Error handling function */
void CatchAlarm(int ignored);            /* Handler for SIGALRM */
void GBNSend(int aSock, int aChunkSize, int aWindowSize, struct sockaddr_in &aSendAddr, struct sockaddr_in &aFromAddr);

int main(int argc, char *argv[])
{
    int sock;                        /* Socket descriptor */
    struct sockaddr_in echoServAddr; /* Echo server address */
    struct sockaddr_in fromAddr;     /* Source address of echo */
    struct sigaction myAction;       /* For setting signal handler */
    char *servIP;                    /* IP address of server */
    unsigned int servPort; //server port
    unsigned int chunkSize; //chunk size
    unsigned int windowSize; //window size
    if(argc != 5){
        fprintf(stderr,"Usage: %s <server_ip> <server_port> <chunk_size> <window_size>\n", argv[0]);
        exit(1);
    }
    servIP = argv[1];           //First arg:  server IP address (dotted quad) 
    servPort = atoi(argv[2]);   //server port
    chunkSize = atoi(argv[3]);  //chunk size
    if(chunkSize > 512){
        fprintf(stderr,"Max chunk size is 512, chunk size set to 512\n");
        chunkSize = 512;
    }
    windowSize = atoi(argv[4]);  //window size

     /* Create a best-effort datagram socket using UDP */
     if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
         DieWithError("socket() failed");
     }

    /* Set signal handler for alarm signal */
    myAction.sa_handler = CatchAlarm;
    if (sigfillset(&myAction.sa_mask) < 0) /* block everything in handler */
        DieWithError("sigfillset() failed");
    myAction.sa_flags = 0;
    if (sigaction(SIGALRM, &myAction, 0) < 0)
        DieWithError("sigaction() failed for SIGALRM");

    /* Construct the server address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr));    /* Zero out structure */
    echoServAddr.sin_family = AF_INET;
    echoServAddr.sin_addr.s_addr = inet_addr(servIP);  /* Server IP address */
    echoServAddr.sin_port = htons(servPort);       /* Server port */

    GBNSend(sock, chunkSize, windowSize, echoServAddr, fromAddr);
    exit(0);
}

void GBNSend(int aSock, int aChunkSize, int aWindowSize, struct sockaddr_in &aSendAddr, struct sockaddr_in &aFromAddr){
    int base = 0;
    int nextSeq = 0;
    int pktHdrSize = 3*sizeof(int);
    packet* packets = new packet[PACKETSTOSEND];
    ack* recvPkt = new ack;
    //initializes data to send
    for(int i=0; i<PACKETSTOSEND; i++){
        packets[i].type = 1;
        packets[i].seq_no = i;
        packets[i].length = strlen(buffer);
        strcpy(packets[i].data, buffer);
    }

    int sendSize;
    int pktSize;
    unsigned int fromSize = sizeof(aFromAddr);
    int ackNo;
    int ackSize = 2*sizeof(int);

    int recvSize;
    //start loop to send
    while(base < PACKETSTOSEND){
        if(nextSeq < base + aWindowSize && nextSeq < PACKETSTOSEND){
            //send packet with next seq and set tim
            pktSize = pktHdrSize + packets[nextSeq].length;
            fprintf(stderr, "SEND PACKET %d\n", nextSeq);
            sendSize = sendto(aSock, (void *) &packets[nextSeq], pktSize, 0, (struct sockaddr *) &aSendAddr, sizeof(aSendAddr));
            if (sendSize != pktSize){
                DieWithError("sendto() sent a different number of bytes than expected");  
            }
            if(base == nextSeq){
                //start timer
                alarm(TIMEOUT_SECS);
            }
            nextSeq++;
        }
        else {
            //all packets within window size has been sent
            //wait for acks
            while((recvSize = recvfrom(aSock, (void *) recvPkt, ackSize, 0,(struct sockaddr *) &aFromAddr, &fromSize)) < 0){
                if (errno == EINTR){/* Alarm went off  */
                    if (tries < MAXTRIES) {
                        //resend packets
                        for(int i=base; i<base+aWindowSize && i < PACKETSTOSEND; i++){
                            pktSize = pktHdrSize + packets[i].length;
                            fprintf(stderr, "SEND PACKET %d\n", i);
                            sendSize = sendto(aSock, (void*) &packets[i], pktSize, 0, (struct sockaddr *) &aSendAddr, sizeof(aSendAddr));
                            if(sendSize != pktSize){
                                DieWithError("sendto() sent a different number of bytes than expected");  
                            }
                        }
                        alarm(TIMEOUT_SECS);
                    }
                    else{
                        DieWithError("No Response");
                    }
                }   
                else {
                    DieWithError("recvfrom() failed");
                } 
            }            
            //recieved ack
            ackNo = recvPkt->ack_no;
            fprintf(stderr, "-------- RECEIVE ACK %d\n", ackNo);
            base = max(base, ackNo + 1);
            if(base == nextSeq){
                alarm(0);
            }
            else{
                alarm(TIMEOUT_SECS);
            }
            //reset tries
            tries = 0;
        }
    }
    //all acks have been recieved start teardown
    packet* teardown = new packet;
    teardown->type = 4;
    teardown->seq_no = nextSeq;
    teardown->length = 0;
    //start timer for teardown
    alarm(TIMEOUT_SECS);
    //send first teardown
    tries = 0;
    fprintf(stderr, "SEND PACKET %d\n", teardown->seq_no);
    sendSize = sendto(aSock, (void *) teardown, pktHdrSize, 0, (struct sockaddr *) &aSendAddr, sizeof(aSendAddr));
    if (sendSize != pktHdrSize){
        DieWithError("sendto() sent a different number of bytes than expected");  
    }
    int recvType = 0;
    while(recvType != 8 && tries < MAXTEARDOWN){  
        while((recvSize = recvfrom(aSock, (void *) recvPkt, ackSize, 0,(struct sockaddr *) &aFromAddr, &fromSize)) < 0){
            if (errno == EINTR){/* Alarm went off  */
                if (tries < MAXTEARDOWN) {
                    //resend teardown
                    fprintf(stderr, "SEND PACKET %d\n", teardown->seq_no);
                    sendSize = sendto(aSock, (void *) teardown, pktHdrSize, 0, (struct sockaddr *) &aSendAddr, sizeof(aSendAddr));
                    if (sendSize != pktHdrSize){
                        DieWithError("sendto() sent a different number of bytes than expected");  
                    }
                    alarm(TIMEOUT_SECS);
                }
                else{
                    DieWithError("No Response");
                }
            }   
            else {
                DieWithError("recvfrom() failed");
            } 
        }
        recvType = recvPkt->type;
        fprintf(stderr, "-------- RECEIVE ACK %d\n", recvPkt->ack_no);
    }
    //clean up memory
    delete teardown;
    delete recvPkt;
    delete[] packets;
    return;
}

void CatchAlarm(int ignored) {    /* Handler for SIGALRM */
    tries += 1;
}

void DieWithError(string errorMessage){
    fprintf(stderr, "%s\n", errorMessage.c_str());
    exit(1);
}