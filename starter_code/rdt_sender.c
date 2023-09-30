// Importing libraries
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <errno.h>

#include "packet.h"
#include "common.h"

#define STDIN_FD 0
#define RETRY 120 // millisecond

int next_seqno = 0;   // stores the packet of the next seqno
int send_base = 0;    // stores the send_base index
int window_size = 10; // defines the window size of the sender

// data structure used for maintaining the window - long static array of size 2000
tcp_packet *packets[2000];
int packetsArrS = 0; // total number of packets in the array

// variables to denote the indexes of the following in the packets array
int next_seqnoI = 0;
int last_ackI = 0;

// for checking for duplicate acks
int dup_ackC = 0;
int prev_ackno = 0;
int dup_ackFlag = 0;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

int breakC = 1; // flag to break out of the infinite while loop

// variables to monitor retranmission due to timeout
int retransmissionCount = 0;
int maxRetransmission = 100; // this value needs to be changed based on the uplink and downlink conditions

void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        // checking whether to retransmit or not based on the previous number of retranmissions
        if (retransmissionCount < maxRetransmission)
        {
            retransmissionCount++;
            // making sure that the send_base doesn't go beyond the total number of packets present
            if (send_base < packetsArrS)
            {
                // timout condition
                VLOG(INFO, "\nTimout happend\n");
                // increments the number of times that the packet has been constantly getting retransmitted

                VLOG(DEBUG, "Resending packet %d to %s",
                     send_base, inet_ntoa(serveraddr.sin_addr));
                // sending the oldest unacked packet - packet with the lowest seq number in the current window
                if (sendto(sockfd, packets[send_base], TCP_HDR_SIZE + get_data_size(packets[send_base]), 0,
                           (const struct sockaddr *)&serveraddr, serverlen) < 0)
                {
                    error("sendto");
                }
                // restart timer
                start_timer();
            }

            // edge case handling - for the "End of the file" message
            if (last_ackI >= packetsArrS - 2)
            {
                VLOG(INFO, "End Of File has been reached");
                sndpkt = make_packet(0);

                sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                       (const struct sockaddr *)&serveraddr, serverlen);
            }
        }
        // case where the sender has reached its limit for retransmissions
        else
        {
            VLOG(DEBUG, "Maximum Retransmissions reached. Sender exiting.")
            exit(0);
        }
    }
}
/*
 * init_timer: Initialize timer
 * delay: delay in milliseconds
 * sig_handler: signal handler function for re-sending unACKed packets
 */
void init_timer(int delay, void (*sig_handler)(int))
{
    signal(SIGALRM, sig_handler);
    timer.it_interval.tv_sec = delay / 1000; // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;
    timer.it_value.tv_sec = delay / 1000; // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

int main(int argc, char **argv)
{
    int portno, len;
    int next_seqno;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL)
    {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* initialize server server details */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0)
    {
        fprintf(stderr, "ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    init_timer(RETRY, resend_packets);
    next_seqno = 0;

    // copying the file into the array by making TCP packets
    int temp_seqno = 0;
    while (1)
    {
        len = fread(buffer, 1, DATA_SIZE, fp);
        // when there is no more data in the file
        if (len <= 0)
        {
            // closing the file
            fclose(fp);
            break;
        }
        // make a TCP packet and copy data from the buffer into the packet
        sndpkt = make_packet(len);
        memcpy(sndpkt->data, buffer, len);
        sndpkt->hdr.seqno = temp_seqno;

        packets[packetsArrS] = malloc(TCP_HDR_SIZE + sndpkt->hdr.data_size);
        memcpy(packets[packetsArrS], sndpkt, TCP_HDR_SIZE + sndpkt->hdr.data_size);

        // incrementing temp_seqno to be put into the header of the next TCP packet
        temp_seqno += len;
        packetsArrS++;
    }
    printf("packet arraysize %d \n", packetsArrS); // DEBUG statement

    // running until the condition turns false
    while (breakC == 1)
    {
        // until there is space in the sender window, and when there are no duplicate acks
        while (next_seqnoI < send_base + window_size && dup_ackFlag == 0)
        {
            // when we recived the ack for the last packet, we inform the receiver that there is no more data to be sent
            if (last_ackI >= packetsArrS - 1)
            {
                VLOG(INFO, "End Of File has been reached");
                sndpkt = make_packet(0); // packet of data_size 0

                sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                       (const struct sockaddr *)&serveraddr, serverlen);

                break;
            }
            // edge case
            if (next_seqnoI >= packetsArrS)
            {
                break;
            }

            // start the timer only when a new packet/set of packets are being sent from the window.
            // This is important to maintain just a single retransmission timer
            if (send_base == next_seqnoI)
            {
                VLOG(DEBUG, "Timer Started");
                start_timer();
            }

            VLOG(DEBUG, "Sending packet %d to %s",
                 next_seqnoI, inet_ntoa(serveraddr.sin_addr));
            /*
             * If the sendto is called for the first time, the system will
             * will assign a random port number so that server can send its
             * response to the src port.
             */

            // sending the packet with the next seq number
            if (sendto(sockfd, packets[next_seqnoI], TCP_HDR_SIZE + get_data_size(packets[next_seqnoI]), 0,
                       (const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }

            // incrementing both next_seqno and next_seqno index
            next_seqno = next_seqno + len;
            next_seqnoI++;
        }

        // waiting for response from the user
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                     (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen) < 0)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                // no data available to read at the moment
                // had been implemented only when we had made the recvfrom unblocked
            }
            else
            {
                error("recvfrom");
            }
        }
        else
        {
            retransmissionCount = 0; // resetting the retranmissionCount whenever a ack is received

            // looking into the received ack
            recvpkt = (tcp_packet *)buffer;
            next_seqno = recvpkt->hdr.ackno;
            send_base = recvpkt->hdr.ackno / DATA_SIZE;
            last_ackI = send_base - 1; // because the the next required packet is acked from the receiver side
            next_seqnoI = send_base;

            VLOG(DEBUG, "Received ack  %d",
                 send_base);

            // VLOG(DEBUG, "Last acked packet %d",
            //      last_ackI);

            assert(get_data_size(recvpkt) <= DATA_SIZE);

            // special ack that is sent from the receiver side after closing the file on the receiver side
            if (recvpkt->hdr.data_size == 1)
            {
                VLOG(DEBUG, "Sender side exiting!");
                breakC = 0; // making the infinite while condition false
                break;
            }
            // checking for duplicate acks
            if (recvpkt->hdr.ackno == prev_ackno)
            {
                VLOG(DEBUG, "Received duplicate ack for packet %d",
                     send_base);
                dup_ackC++;      // incrementing the number of continuous duplicate acks
                dup_ackFlag = 1; // making duplicate ack flag true
            }
            else
            {
                // resetting duplicate ack variables
                dup_ackC = 0;
                dup_ackFlag = 0;
            }

            prev_ackno = recvpkt->hdr.ackno;

            // if there are more than three continuous duplicate acks
            if (dup_ackC >= 3)
            {
                // retransmitting packet due to packet loss
                if (send_base < packetsArrS)
                {
                    VLOG(DEBUG, "Three duplicate acks received for packet %d. Packet lost and retransitted",
                         send_base);
                    // packet lost, retransmit
                    if (sendto(sockfd, packets[send_base], TCP_HDR_SIZE + get_data_size(packets[send_base]), 0,
                               (const struct sockaddr *)&serveraddr, serverlen) < 0)
                    {
                        error("sendto");
                    }
                    dup_ackC = 0;
                    // restarting timer
                    stop_timer();
                    start_timer();
                }
            }
        }

        // find condition where timer needs to be stopped because of no remaining unacked packets
        // if (send_base == next_seqnoI)
        // {
        //     stop_timer();
        // }
    }

    return 0;
}
