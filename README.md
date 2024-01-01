# Reliable-Video-Transfer-using-UDP
This program is designed to facilitate the reliable transfer of video files over a network using the UDP protocol in a Linux/GNU C sockets environment. 
# Sender
**Overview**
The sender reads a video file, breaks it into chunks, and sends these chunks over UDP to a receiver. The receiver, on the other hand, receives, organizes, and writes the data to a file.

**Features**
- Selective Repeat Mechanism: The sender implements a selective repeat mechanism to improve the reliability of UDP data transfer. It retransmits only those packets for - which acknowledgments have not been received.
- Threaded Acknowledgment Reception: The program utilizes a separate thread to receive acknowledgments in parallel with the main program execution, enhancing efficiency.
- Error Handling: The program incorporates robust error handling mechanisms to gracefully handle critical operations and terminate in case of errors.
- Time Delays: Time delays are introduced to manage the flow of acknowledgments and retransmissions effectively.

# Sender

# Dependencies
C Standard Library
pthread Library
# Notes
The program is designed for use in a Linux/GNU environment.
