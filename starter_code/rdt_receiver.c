// Importing libraries
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>

#include "common.h"
#include "packet.h"

/*
 * You are required to change the implementation to support
 * window size greater than one.
 * In the current implementation the window size is one, hence we have
 * only one send and receive packet
 */
tcp_packet *recvpkt;
tcp_packet *sndpkt;

// using long static arrays for keeping the receiver window
tcp_packet *packets[2000] = {NULL};

// index of the packets
int exp_packet = 0;
int exp_packetI = 0;
int curr_packetI = 0;
int max_packetI = 0;

int window_size = 10; // window size of the receiver for buffering out of order packets

int main(int argc, char **argv)
{
    int sockfd;                    /* socket */
    int portno;                    /* port to listen on */
    int clientlen;                 /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval;                    /* flag value for setsockopt */
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;

    /*
     * check command line arguments
     */
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp = fopen(argv[2], "w");
    if (fp == NULL)
    {
        error(argv[2]);
    }

    /*
     * socket: create the parent socket
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets
     * us rerun the server immediately after we kill it;
     * otherwise we have to wait about 20 secs.
     * Eliminates "ERROR on binding: Address already in use" error.
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /*
     * bind: associate the parent socket with a port
     */
    if (bind(sockfd, (struct sockaddr *)&serveraddr,
             sizeof(serveraddr)) < 0)
        error("ERROR on binding");

    /*
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);
    while (1)
    {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        // VLOG(DEBUG, "waiting from server \n");
        bzero(buffer, sizeof(buffer));
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                     (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen) < 0)
        {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *)buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        // receiving the ack with 0 data_size indicating that there is no more data to be received from the sender side
        if (recvpkt->hdr.data_size == 0)
        {
            VLOG(INFO, "End Of File has been reached");
            exp_packet += recvpkt->hdr.data_size;
            exp_packetI++;
            VLOG(INFO, "%d", exp_packetI);
            // making a special packet of 1 len and sending
            // this is a special ACK packet to indicate that the receiver acknoledges the end of file
            sndpkt = make_packet(1);
            sndpkt->hdr.ackno = exp_packet;
            sndpkt->hdr.ctr_flags = ACK;
            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                       (struct sockaddr *)&clientaddr, clientlen) < 0)
            {
                error("ERROR in sendto");
            }
            fclose(fp);
            break;
        }
        /*
         * sendto: ACK back to the client
         */
        gettimeofday(&tp, NULL);
        VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);

        // getting packet index
        int curr_packetI = recvpkt->hdr.seqno / DATA_SIZE;

        // checking if the packet received is beyond the buffer size
        if (curr_packetI - exp_packetI > window_size)
        {

            // packet dropped
            VLOG(DEBUG, "Window size exceeded. Packet dropped");
            continue;
        }

        // putting packet inside array
        packets[curr_packetI] = recvpkt;
        packets[curr_packetI] = malloc(TCP_HDR_SIZE + recvpkt->hdr.data_size);
        memcpy(packets[curr_packetI], recvpkt, TCP_HDR_SIZE + recvpkt->hdr.data_size);

        // checking for whether the received packet is out of order or not
        if (curr_packetI > exp_packetI)
        {
            VLOG(DEBUG, "Out of order packet received. Packet no %d buffered", curr_packetI);
        }

        // storing the maximum packet index
        max_packetI = (curr_packetI > max_packetI) ? curr_packetI : max_packetI;

        // if inorder packet is received
        if (curr_packetI == exp_packetI)
        {
            // checking for all the packets that get inorder due to the receipt of the packet
            for (int i = curr_packetI; i <= max_packetI; i++)
            {
                // if any of the indexes are NULL, means there are no more inorder packets
                if (packets[i] != NULL)
                {
                    VLOG(DEBUG, "packet received and written: %d", i);
                    // VLOG(DEBUG, "Header data size: %d", packets[i]->hdr.data_size);
                    // VLOG(DEBUG, "packet contents of seqno %d:\n %s", packets[i]->hdr.seqno, packets[i]->data);
                    fseek(fp, packets[i]->hdr.seqno, SEEK_SET);
                    fwrite(packets[i]->data, 1, packets[i]->hdr.data_size, fp);

                    // incrementing expected packet and expected packet index
                    exp_packetI++;
                    exp_packet += recvpkt->hdr.data_size;
                }
                else
                {
                    break;
                }
            }
        }

        // sending ack
        sndpkt = make_packet(0);
        sndpkt->hdr.ackno = exp_packet;
        sndpkt->hdr.ctr_flags = ACK;
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                   (struct sockaddr *)&clientaddr, clientlen) < 0)
        {
            error("ERROR in sendto");
        }
    }

    return 0;
}
