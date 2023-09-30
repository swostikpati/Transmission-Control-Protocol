# TCP Implementation Project
## Project Objective
The primary objective of this project is to construct a custom TCP (Transmission Control Protocol) implementation from the ground up. The project is structured into two distinct tasks: reliable data transfer and congestion control.

## Task 1: Reliable Data Transfer
In Task 1, our focus is on creating a "Reliable Data Transfer" protocol, in line with the specifications detailed in section 3.5.4 of the textbook. The core aim here is to design and implement a simplified TCP sender and receiver capable of effectively managing packet losses and facilitating retransmissions.

### Key Functionalities
Sending Packets: The sender will transmit packets to the network, adhering to a fixed sending window size, as depicted in Figure 3.33 in the textbook. The window size for this project is set to 10 packets.

### Cumulative Acknowledgments
The receiver will send cumulative acknowledgments to the sender, and the sender will need to interpret and respond to these acknowledgments effectively.

### Retransmission Timer
A single retransmission timer will be employed to handle situations of packet loss and the subsequent retransmissions.

### Approach
The approach used for implementing Task 1 resembles the Go-Back-N protocol in several aspects. Both utilize cumulative acknowledgments and a single timeout timer for the oldest unacknowledged packet. However, in this implementation, only the packet with the smallest sequence number in the window is retransmitted upon a timeout, contrasting with the Go-Back-N protocol, which would retransmit the entire window.

At the receiver's end, out-of-order packets must be buffered, and cumulative acknowledgments are dispatched for such packets. In cases where a packet is lost, the sender must retransmit the packet utilizing a retransmission timeout timer (RTO) that operates with a fixed timeout value tailored to the emulated network scenario using MahiMahi. The retransmission timer should restart upon receiving an ACK that acknowledges a transmitted packet, ensuring it expires after the fixed timeout value. The sender should terminate operation upon successfully transmitting the entire file, including receiving an acknowledgment for the very last packet.

## Task 2: Congestion Control
Task 2 involves the implementation of a congestion control protocol for both the sender and receiver, building upon the foundation established in Task 1. The congestion control protocol is akin to TCP Tahoe and should encompass the following key features:

### Key Features
Slow Start: The sender will initiate a slow start mechanism to cautiously ramp up its sending rate, closely monitoring network congestion.

### Congestion Avoidance
Once congestion is detected, the sender will transition into congestion avoidance mode, efficiently regulating its sending rate to alleviate congestion.

### Fast Retransmit
In the event of detecting multiple packet losses, the sender will employ fast retransmit, promptly retransmitting lost packets without entering the full-blown fast recovery mode.

___
This project offers a comprehensive exploration of the TCP protocol's inner workings, ranging from reliable data transfer mechanisms to congestion control strategies. It provides an excellent opportunity to delve deep into network protocols and gain practical experience in their implementation.

## Academic Use Notice
Kindly be aware that this project was created as part of an educational assignment within a course offered by NYU Abu Dhabi's Computer Science department. It is intended for educational and learning purposes. **Using this code for academic submissions or assignments is strictly prohibited**. I encourage students to use this repository as a learning resource and to write their own code for assignments to uphold academic integrity.