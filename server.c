/*
 * UDP Video Transfer Server
 *
 * This program receives video data from a client using the UDP protocol in Linux/GNU C sockets.
 * It employs a window-based selective repeat mechanism for reliable data transfer. The received packets
 * are written to an output file, forming a complete video file. Various functionalities and details are
 * commented below.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>

#define SERVER_PORT "4950"            // Port for client-server communication
#define PACKET_BUFFER_SIZE 500        // Size of each data packet

// Struct for data packets
struct DataPacket {
    int sequenceNumber;
    int packetSize;
    char data[PACKET_BUFFER_SIZE];
};

// Variables for socket
int serverSocket;                       // Socket for server
struct addrinfo serverAddressInfo, *serverInfo, *ptr; // Server address information
struct sockaddr_storage clientAddress;  // Client address information
socklen_t clientAddressLength = sizeof(struct sockaddr_storage);
char ipAddress[INET6_ADDRSTRLEN];       // Buffer for storing IP address
int returnVal;

// Variables for video file
int totalBytes = 0;
int outputFile;
int fileSize;
int remainingBytes = 0;
int receivedBytes = 0;

// Variables for transferring data packets and acknowledgments
int totalPackets = 5;                   // Window size is 5
struct DataPacket tempPacket;
struct DataPacket dataPackets[5];
int totalAcks;
int acks[5];
int tempAck;

// Function to get IP address
void *getIPAddress(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// Thread to receive packets (runs parallel with the main program)
void *receivePackets(void *vargp) {
    // Receive 5 packets
    for (int i = 0; i < totalPackets; i++) {
        RECEIVE:
        if ((totalBytes = recvfrom(serverSocket, &tempPacket, sizeof(struct DataPacket), 0, (struct sockaddr *)&clientAddress, &clientAddressLength)) < 0) {
            perror("UDP Server: recvfrom");
            exit(1);
        }

        // Duplicate packet
        if (dataPackets[tempPacket.sequenceNumber].packetSize != 0) {
            // Reallocate the array
            dataPackets[tempPacket.sequenceNumber] = tempPacket;
            // Create an acknowledgment
            tempAck = tempPacket.sequenceNumber;
            acks[tempAck] = 1;
            // Send the acknowledgment
            if (sendto(serverSocket, &tempAck, sizeof(int), 0, (struct sockaddr *)&clientAddress, clientAddressLength) < 0) {
                perror("UDP Server: sendto");
                exit(1);
            }
            printf("Duplicate Acknowledgment Sent: %d\n", tempAck);

            // Receive the packet again until a unique packet is sent
            goto RECEIVE;
        }

        // In case of the last packet
        if (tempPacket.packetSize == -1) {
            printf("Last packet found\n");
            // Decrement the counter of the remaining loops
            totalPackets = tempPacket.sequenceNumber + 1;
        }

        // Unique packet
        if (totalBytes > 0) {
            printf("Packet Received: %d\n", tempPacket.sequenceNumber);
            // Keep the correct order of packets by the index of the array
            dataPackets[tempPacket.sequenceNumber] = tempPacket;
        }
    }
    return NULL;
}

int main(void) {
    memset(&serverAddressInfo, 0, sizeof serverAddressInfo); // Ensure the struct is empty
    serverAddressInfo.ai_family = AF_UNSPEC;                // Set to AF_INET to force IPv4
    serverAddressInfo.ai_socktype = SOCK_DGRAM;              // UDP socket datagrams
    serverAddressInfo.ai_flags = AI_PASSIVE;                 // Fill in my IP

    if ((returnVal = getaddrinfo(NULL, SERVER_PORT, &serverAddressInfo, &serverInfo)) != 0) {
        fprintf(stderr, "UDP Server: getaddrinfo: %s\n", gai_strerror(returnVal));
        return 1;
    }

    // Loop through all the results in the linked list and bind to the first one
    for (ptr = serverInfo; ptr != NULL; ptr = ptr->ai_next) {
        if ((serverSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) == -1) {
            perror("UDP Server: socket");
            continue;
        }

        // Bind socket
        if (bind(serverSocket, ptr->ai_addr, ptr->ai_addrlen) == -1) {
            close(serverSocket);
            perror("UDP Server: bind");
            continue;
        }

        break;
    }

    if (ptr == NULL) {
        fprintf(stderr, "UDP Server: Failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(serverInfo); // All done with this structure

    printf("UDP Server: Waiting to receive datagrams.\n");

    pthread_t threadId; // Create thread ID

    // Time delay variables
    struct timespec time1, time2;
    time1.tv_sec = 0;
    time1.tv_nsec = 30000000L; // 0.03 seconds

    FILE *outputFile = fopen("output_video.mp4", "wb"); // Open the video file in write mode

    // Receive the size of the video file from the client
    if ((totalBytes = recvfrom(serverSocket, &fileSize, sizeof(off_t), 0, (struct sockaddr *)&clientAddress, &clientAddressLength)) < 0) {
        perror("UDP Server: recvfrom");
        exit(1);
    }
    printf("Size of Video File to be received: %d bytes\n", fileSize);

    totalBytes = 1;
    remainingBytes = fileSize;

    while (remainingBytes > 0 || (totalPackets == 5)) {
        // Reinitialize the arrays
        memset(dataPackets, 0, sizeof(dataPackets));
        for (int i = 0; i < 5; i++) {
            dataPackets[i].packetSize = 0;
        }

        for (int i = 0; i < 5; i++) {
            acks[i] = 0;
        }

        // Server starts receiving packets i.e., thread execution starts
        pthread_create(&threadId, NULL, receivePackets, NULL);

        // Wait for packets to be received, i.e., the code sleeps for 0.03 seconds
        nanosleep(&time1, &time2);

        totalAcks = 0;

        // Send acknowledgments for the packets received only
        RESEND_ACK:
        for (int i = 0; i < totalPackets; i++) {
            tempAck = dataPackets[i].sequenceNumber;
            // If the acknowledgment has not been sent before
            if (acks[tempAck] != 1) {
                // Create acknowledgments for the packets received ONLY
                if (dataPackets[i].packetSize != 0) {
                    acks[tempAck] = 1;

                    // Send acknowledgments to the client
                    if (sendto(serverSocket, &tempAck, sizeof(int), 0, (struct sockaddr *)&clientAddress, clientAddressLength) > 0) {
                        totalAcks++;
                        printf("Acknowledgment sent: %d\n", tempAck);
                    }
                }
            }
        }

        // Stop-and-wait
        // Wait for acknowledgments to be sent and processed by the client
        nanosleep(&time1, &time2);
        nanosleep(&time1, &time2);

        // If all packets were not received
        if (totalAcks < totalPackets) {
            goto RESEND_ACK;
        }

        // 5 packets have been received i.e., the thread executes successfully
        pthread_join(threadId, NULL);

        // Write packets to the output file
        for (int i = 0; i < totalPackets; i++) {
            // Data is present in the packets and it's not the last packet
            if (dataPackets[i].packetSize != 0 && dataPackets[i].packetSize != -1) {
                printf("Writing packet: %d\n", dataPackets[i].sequenceNumber);
                fwrite(dataPackets[i].data, 1, dataPackets[i].packetSize, outputFile);
                remainingBytes = remainingBytes - dataPackets[i].packetSize;
                receivedBytes = receivedBytes + dataPackets[i].packetSize;
            }
        }

        printf("Received data: %d bytes\nRemaining data: %d bytes\n", receivedBytes, remainingBytes);

        // Repeat the process for the next 5 packets
    }

    printf("\nUDP Server: Received video file from client %s\n", inet_ntop(clientAddress.ss_family, getIPAddress((struct sockaddr *)&clientAddress), ipAddress, sizeof ipAddress));
    printf("File transfer completed successfully!\n");
    close(serverSocket); // Close server socket
    return 0;
}
