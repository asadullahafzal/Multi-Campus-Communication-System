# Multi-Campus-Communication-System
It demonstrates how campuses in a real network environment communicate with a central node, just like routers forwarding packets. The project uses threads, mutexes, standard libraries, and timing functions to simulate real-time communication.

## Features

C++ Server that receives, processes, and displays messages. C++ Client that sends messages to the server. Multi-threading to handle communication flow.Mutex-based synchronization to avoid data conflicts. Simulated delay using chrono for realistic behavior. String and buffer handling to manage messages. Easy to compile and run on any OS (Windows/Linux).

## How It Works

Server

Waits for messages from clients.
Displays communication between campuses.
Runs continuously using threads.

Client

Sends campus messages to the server.
Can simulate multiple campuses sending data.
Uses thread functions to mimic real network traffic.
Together, they form a working model that demonstrates how campuses communicate with a central server in a distributed environment.

## Compilation Instructions
On Linux (g++):
Compile Server:
g++ server.cpp -o server -pthread

Compile Client:
g++ client.cpp -o client -pthread

On Windows (MinGW g++):
Compile Server:
g++ server.cpp -o server.exe -lws2_32 -pthread

Compile Client:
g++ client.cpp -o client.exe -lws2_32 -pthread


(Note: If Winsock2 is not used in code, remove -lws2_32)

## How to Run
Step 1: Start the Server
./server.exe

Step 2: Run the Client (one or multiple times)
./client.exe


You can run many client instances to simulate multiple campuses sending messages.

## Purpose of the Project

This project is designed to help students understand:

Basic message-passing between nodes.
Client–server architecture.
Thread-based parallel communication.
Synchronization using mutex locks.
Real-world inspiration from network communication between campuses.
It fits well as a networking or distributed systems university assignment.


