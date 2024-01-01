/*
 * UDP Video Transfer Client
 *
 * This program is designed for sending and receiving video files using the UDP protocol in Linux/GNU C sockets.
 * The sender reads a video file, breaks it into chunks, and sends these chunks over UDP. The receiver receives,
 * organizes, and writes the data to a file. Some enhancements have been made to improve reliability in the UDP process.
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
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>

#define SERVER_PORT "4950"            // Port for server-client communication
#define PACKET_BUFFER_SIZE 500        // Size of each data packet

// Struct for data packets
struct DataPacket {
    int sequenceNumber;
    int packetSize;
    char data[PACKET_BUFFER_SIZE];
};

// Variables for socket
int clientSocket;
struct addrinfo serverAddressInfo, *serverInfo, *ptr; // Server address information
struct sockaddr_storage serverAddress;
socklen_t serverAddressLength = sizeof(struct sockaddr_storage);
int returnVal;

// Variables for video file
int dataRead;
int totalBytes;
int inputFile;
struct stat fileStat;
int fileDescriptor;
off_t fileSize;

// Variables for transferring data packets and acknowledgments
struct DataPacket dataPackets[5]; // Window size is 5
int tempSeqNumber = 1;
int totalAcks;
int tempAck;
int acks[5];
int totalPackets = 5;

// Thread to receive acknowledgments to run in parallel with the main program
void *receiveAcks(void *vargp) {
    // Receive 5 acknowledgments
    for (int i = 0; i < totalPackets; i++) {
        RECEIVE:
        if ((totalBytes = recvfrom(clientSocket, &tempAck, sizeof(int), 0, (struct sockaddr *)&serverAddress, &serverAddressLength)) < 0) {
            perror("UDP Client: recvfrom");
            exit(1);
        }

        // Duplicate acknowledgment
        if (acks[tempAck] == 1) {
            // Receive acknowledgment again until a unique acknowledgment is received
            goto RECEIVE;
        }

        // In case of a unique acknowledgment
        printf("Acknowledgment Received: %d\n", tempAck);
        // Reorder acknowledgments according to the packet's sequence number
        // Make the value 1 in the acks[] array, where the array position is the value of acknowledgment received
        acks[tempAck] = 1;
        totalAcks++;
    }
    return NULL;
}

// Main function
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "UDP Client: Usage: ./Client hostname\n");
        exit(1);
    }

    memset(&serverAddressInfo, 0, sizeof serverAddressInfo); // Ensure the struct is empty
    serverAddressInfo.ai_family = AF_UNSPEC;
    serverAddressInfo.ai_socktype = SOCK_DGRAM; // UDP socket datagrams

    if ((returnVal = getaddrinfo(argv[1], SERVER_PORT, &serverAddressInfo, &serverInfo)) != 0) {
        fprintf(stderr, "UDP Client: getaddrinfo: %s\n", gai_strerror(returnVal));
        return 1;
    }

    // Loop through all the results and make a socket
    for (ptr = serverInfo; ptr != NULL; ptr = ptr->ai_next) {
        if ((clientSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) == -1) {
            perror("UDP Client: socket");
            continue;
        }
        break;
    }

    if (ptr == NULL) {
        fprintf(stderr, "UDP Client: Failed to create socket\n");
        return 2;
    }

    pthread_t threadId; // Create thread ID

    // Time delay variables
    struct timespec time1, time2;
    time1.tv_sec = 0;
    time1.tv_nsec = 300000000L;

    FILE *inputFile = fopen("input_video.MOV", "rb"); // Open the video file in read mode

    // If the file is not readable
    if (inputFile == NULL) {
        perror("Error in opening the video file.\n");
        return 0;
    }

    // Size of the video file
    fileDescriptor = fileno(inputFile);
    fstat(fileDescriptor, &fileStat);
    fileSize = fileStat.st_size;
    printf("Size of Video File: %d bytes\n", (int)fileSize);

    // Sending the size of the video file to the server
    FILESIZE:
    if (sendto(clientSocket, &fileSize, sizeof(off_t), 0, ptr->ai_addr, ptr->ai_addrlen) < 0) {
        // Resend the file size
        goto FILESIZE;
    }

    dataRead = 1;

    // While data left to be read
    while (dataRead > 0) {
        // Make packets
        tempSeqNumber = 0;
        for (int i = 0; i < totalPackets; i++) {
            dataRead = fread(dataPackets[i].data, 1, PACKET_BUFFER_SIZE, inputFile);
            // Sequence number
            dataPackets[i].sequenceNumber = tempSeqNumber;
            // Packet size
            dataPackets[i].packetSize = dataRead;
            tempSeqNumber++;

            // Last packet to be sent i.e., EOF
            if (dataRead == 0) {
                printf("End of file reached.\n");
                // Setting a condition for the last packet
                dataPackets[i].packetSize = -1;
                // Decrementing the remaining loops
                totalPackets = i + 1;
                break;
            }
        }

        // Send 5 packets
        for (int i = 0; i < totalPackets; i++) {
            printf("Sending packet %d\n", dataPackets[i].sequenceNumber);
            if (sendto(clientSocket, &dataPackets[i], sizeof(struct DataPacket), 0, ptr->ai_addr, ptr->ai_addrlen) < 0) {
                perror("UDP Client: sendto");
                exit(1);
            }
        }

        // Reinitialize the array
        for (int i = 0; i < totalPackets; i++) {
            acks[i] = 0;
        }

        totalAcks = 0;

        // Client starts receiving acknowledgments i.e. the thread execution starts
        pthread_create(&threadId, NULL, receiveAcks, NULL);

        // Wait for acknowledgments to be received i.e. the code sleeps for 0.03 seconds
        nanosleep(&time1, &time2);

        // Selective repeat starts

        // Send those packets ONLY whose acknowledgments have not been received
        RESEND:
        for (int i = 0; i < totalPackets; i++) {
            // If the acknowledgment has not been received
            if (acks[i] == 0) {
                // Sending that packet whose acknowledgment was not received
                printf("Sending missing packet: %d\n", dataPackets[i].sequenceNumber);
                if (sendto(clientSocket, &dataPackets[i], sizeof(struct DataPacket), 0, ptr->ai_addr, ptr->ai_addrlen) < 0) {
                    perror("UDP Client: sendto");
                    exit(1);
                }
            }
        }

        // Resend the packets again whose acknowledgments have not been received
        if (totalAcks != totalPackets) {
            // Wait for acknowledgments of the packets that were resent
            nanosleep(&time1, &time2);
            goto RESEND;
        }

        // 5 acknowledgments have been received i.e. the thread executes successfully
        pthread_join(threadId, NULL);

        // Repeat the process until the EOF is not reached
    }

    freeaddrinfo(serverInfo);
    printf("\nFile transfer completed successfully!\n");

    close(clientSocket); // Close the socket
    return 0;
}
