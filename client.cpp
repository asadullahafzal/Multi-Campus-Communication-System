#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <cstring>
#include <chrono>
#include <vector>
// relying on 'using namespace std;' to remove all 'std::' prefixes
using namespace std;
#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN "\033[36m"
#define WHITE "\033[37m"
#define BOLD "\033[1m"
#define UNDERLINE "\033[4m"

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif
#define SERVER_IP "127.0.0.1" // Change to actual server IP for testing
#define TCP_PORT 8080
#define UDP_PORT 8081
#define CLIENT_UDP_PORT 8082 // Port for receiving broadcasts
#define BUFFER_SIZE 4096
#define HEARTBEAT_INTERVAL 10 // seconds
mutex consoleMutex;
mutex messageMutex;
vector<string> receivedMessages;
bool isRunning = true;
void waitAndClear() {
    cout << YELLOW << "\nPress any key to clear screen..." << RESET;
    cin.get(); // wait for ANY key
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}
int clientUdpPort = 0;
SOCKET clientUdpSocket = INVALID_SOCKET;
string currentCampus;
// Thread-safe console output
void printLog(const string& message) {
    lock_guard<mutex> lock(consoleMutex);
    cout << BLUE << "[" << RESET << YELLOW << message << RESET << BLUE << "]" << RESET << endl;
}

// Get current timestamp
string getCurrentTime() {
    auto now = chrono::system_clock::now();
    auto time = chrono::system_clock::to_time_t(now);
    string timeStr = ctime(&time);
    timeStr.pop_back();
    return timeStr;
}
// Store received message
void storeMessage(const string& message) {
    lock_guard<mutex> lock(messageMutex);
    receivedMessages.push_back(message);
}
// Send UDP heartbeat periodically using the shared bound UDP socket (clientUdpSocket)
void sendHeartbeat(const string& campusName) {
    if (clientUdpSocket == INVALID_SOCKET) return;

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    serverAddr.sin_port = htons(UDP_PORT);

    while (isRunning) {
        string heartbeat = "HEARTBEAT:" + campusName + ":" + to_string(clientUdpPort);

        sendto(clientUdpSocket,
               heartbeat.c_str(),
               (int)heartbeat.length(),
               0,
               (sockaddr*)&serverAddr,
               sizeof(serverAddr));

        this_thread::sleep_for(chrono::seconds(HEARTBEAT_INTERVAL));
    }
}


// Listen for UDP broadcasts from server using shared clientUdpSocket
void listenForBroadcasts() {
    if (clientUdpSocket == INVALID_SOCKET) {
        printLog("Broadcast listener: UDP socket not initialized");
        return;
    }

    printLog("Listening for broadcasts on port " + to_string(CLIENT_UDP_PORT));

    char buffer[BUFFER_SIZE];
    sockaddr_in serverAddr;
    socklen_t serverAddrLen = sizeof(serverAddr);

    while (isRunning) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytesReceived = recvfrom(clientUdpSocket, buffer, BUFFER_SIZE - 1, 0,
                                     (sockaddr*)&serverAddr, &serverAddrLen);

        if (bytesReceived > 0) {
            string message(buffer, bytesReceived);

            if (message.find("BROADCAST:") == 0) {
                string announcement = message.substr(10);
                lock_guard<mutex> lock(consoleMutex);
                cout << "\n" << MAGENTA << BOLD << "***************************" << RESET << endl;
                cout << MAGENTA << BOLD << "*** SYSTEM ANNOUNCEMENT ***" << RESET << endl;
                cout << YELLOW << announcement << RESET << endl;
                cout << MAGENTA << BOLD << "***************************" << RESET << "\n" << endl;
            }
        }
    }
}

// Listen for incoming TCP messages from server
void listenForMessages(SOCKET tcpSocket) {
    char buffer[BUFFER_SIZE];

    while (isRunning) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytesReceived = recv(tcpSocket, buffer, BUFFER_SIZE - 1, 0);

        if (bytesReceived <= 0) {
            printLog("Disconnected from server");
            isRunning = false;
            break;
        }

        string message(buffer, bytesReceived);

        // ACK message from server
        if (message.rfind("ACK:", 0) == 0) {
            printLog(message.substr(4));
        }
        // ERROR message from server
        else if (message.rfind("ERROR:", 0) == 0) {
            printLog("ERROR - " + message.substr(6));
        }
        // Incoming message from another campus
        else if (message.rfind("TARGET:", 0) == 0) {

            // Extract message parts
            size_t deptPos = message.find("|DEPT:");
            size_t fromPos = message.find("|FROM:");
            size_t msgPos = message.find("|MSG:");

            if (deptPos != string::npos &&
                fromPos != string::npos &&
                msgPos != string::npos) {

                string targetDept = message.substr(deptPos + 6, fromPos - deptPos - 6);
                string sourceCampus = message.substr(fromPos + 6, msgPos - fromPos - 6);
                string msg = message.substr(msgPos + 5);
        size_t ackPos = msg.find("ACK:");
        if (ackPos != string::npos) {
            msg = msg.substr(0, ackPos);
        }
                // Store a simplified version (just the data)
                string storedMsg =
                    "From: " + sourceCampus + "\n"
                    "To: " + targetDept + "\n"
                    "Message: " + msg;

                storeMessage(storedMsg);

                // Just notify user
                lock_guard<mutex> lock(consoleMutex);
                cout << "\n" << GREEN << BOLD
                              << "*** New message received! ***"
                              << RESET << "\n" << endl;
            }
        }
        // Anything else
        else {
            printLog("Unknown message received");
        }
    }
}

// Display menu and handle user input
void displayMenu(SOCKET tcpSocket, const string& campusName) {
    string input;
    
    while (isRunning) {
            cout << "\n" << CYAN << BOLD << "=============================" << RESET << endl;
            cout << CYAN << BOLD << "=== " << campusName << " Campus Client ===" << RESET << endl;
            cout << CYAN << BOLD << "=============================" << RESET << endl;
    cout << GREEN << "1. Send Message to Another Campus" << RESET << endl;
    cout << GREEN << "2. View Received Messages" << RESET << endl;
    cout << GREEN << "3. Exit" << RESET << endl;
            cout << "\n" << WHITE << BOLD << "Choice: " << RESET;

            getline(cin, input);
            if (input == "1") {
                string targetCampus, targetDept, message;
                
                cout << "\n" << YELLOW << "Available Campuses: Islamabad, Lahore, Karachi, Peshawar, CFD, Multan" << RESET << endl;
                cout << WHITE << BOLD << "Enter target campus: " << RESET;
                getline(cin, targetCampus);
                
                cout << YELLOW << "Available Departments: Admissions, Academics, IT, Sports" << RESET << endl;
                cout << WHITE << BOLD << "Enter target department: " << RESET;
                getline(cin, targetDept);
                cout << WHITE << BOLD << "Enter your message: " << RESET;
            
                getline(cin, message);            
                // Format: TARGET:CampusName|DEPT:DeptName|FROM:SourceCampus|MSG:Message
                string formattedMsg = "TARGET:" + targetCampus +
                                      "|DEPT:" + targetDept +
                                      "|FROM:" + campusName +
                                      "|MSG:" + message;
                
                int result = send(tcpSocket, formattedMsg.c_str(), formattedMsg.length(), 0);
                
                if (result == SOCKET_ERROR) {
                cout << RED << BOLD << "ERROR - Unable to deliver message" << RESET << endl;
                } else {
                cout << GREEN << BOLD << "Message sent successfully!" << RESET << endl;
                waitAndClear();
                }
                
            } else if (input == "2") {
                lock_guard<mutex> lock(messageMutex);
                waitAndClear();
                cout << "\n" << BLUE << BOLD << "==========================" << RESET << endl;
                cout << BLUE << BOLD << "=== RECEIVED MESSAGES ===" << RESET << endl;
                cout << BLUE << BOLD << "==========================" << RESET << endl;
                if (receivedMessages.empty()) {
                    cout << YELLOW << "No messages received yet." << RESET << endl;
                    waitAndClear();
                } else {
                    for (size_t i = 0; i < receivedMessages.size(); i++) {
                        cout << "\n" << MAGENTA << BOLD << "[Message " << (i + 1) << "]" << RESET << endl;
                        cout << CYAN << receivedMessages[i] << RESET << endl;
                        cout << BLUE << "------------------------" << RESET << endl;
                    }
                    waitAndClear();
                }
                
            } else if (input == "3") {
                cout << YELLOW << "Disconnecting from server..." << RESET << endl;
                isRunning = false;
                break;
            } else {
                cout << RED << "Invalid choice! Please try again." << RESET << endl;
                waitAndClear();
            }
    }
}
int main() {
waitAndClear();
    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << RED << "WSAStartup failed" << RESET << endl;
        return 1;
    }
    #endif
    
    cout << CYAN << BOLD << "=== NU-Information Exchange System - Campus Client ===" << RESET << endl;
    
    // Get campus information
    string campusName, password;
    
    cout << "\n" << GREEN << "Available Campuses:" << RESET << endl;
    cout << GREEN << "1. Islamabad (NU-ISB-123)" << RESET << endl;
    cout << GREEN << "2. Lahore (NU-LHR-123)" << RESET << endl;
    cout << GREEN << "3. Karachi (NU-KHI-123)" << RESET << endl;
    cout << GREEN << "4. Peshawar (NU-PEW-123)" << RESET << endl;
    cout << GREEN << "5. CFD (NU-CFD-123)" << RESET << endl;
    cout << GREEN << "6. Multan (NU-MLT-123)" << RESET << endl;
    
    cout << "\n" << WHITE << BOLD << "Enter campus name: " << RESET;
    getline(cin, campusName);
    
    cout << WHITE << BOLD << "Enter password: " << RESET;
    getline(cin, password);
    
    currentCampus = campusName;
    
    // Create TCP socket
    SOCKET tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcpSocket == INVALID_SOCKET) {
        cerr << RED << "Failed to create TCP socket" << RESET << endl;
        return 1;
    }
    
    // Connect to server
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    serverAddr.sin_port = htons(TCP_PORT);
    
    cout << "\n" << YELLOW << "Connecting to server..." << RESET << endl;
    
    if (connect(tcpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << RED << "Failed to connect to server" << RESET << endl;
        closesocket(tcpSocket);
        #ifdef _WIN32
        WSACleanup();
        #endif
        return 1;
    }
    
    cout << GREEN << "Connected to server!" << RESET << endl;
    
    // Send authentication
    string authMsg = "Campus:" + campusName + ",Pass:" + password;
    send(tcpSocket, authMsg.c_str(), authMsg.length(), 0);
    
    // Wait for authentication response
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    int bytesReceived = recv(tcpSocket, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytesReceived <= 0) {
        cerr << RED << "Server disconnected during authentication" << RESET << endl;
        closesocket(tcpSocket);
        #ifdef _WIN32
        WSACleanup();
        #endif
        return 1;
    }
    
    string response(buffer);
    
    if (response == "AUTH_SUCCESS") {
        cout << GREEN << BOLD << "Authentication successful!" << RESET << endl;
    } else if (response == "AUTH_FAILED") {
        cerr << RED << BOLD << "Authentication failed! Invalid credentials." << RESET << endl;
        closesocket(tcpSocket);
        #ifdef _WIN32
        WSACleanup();
        #endif
        return 1;
    } else if (response == "ALREADY_CONNECTED") {
        cerr << RED << BOLD << "This campus is already connected!" << RESET << endl;
        closesocket(tcpSocket);
        #ifdef _WIN32
        WSACleanup();
        #endif
        return 1;
    }
   // create and bind the shared UDP socket for heartbeats & broadcasts
// Create UDP socket
    clientUdpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientUdpSocket == INVALID_SOCKET) {
        cerr << RED << "Failed to create client UDP socket" << RESET << endl;
    } else {
        // Assign random dynamic port (0 means OS picks a free one)
        sockaddr_in localAddr;
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = INADDR_ANY;
        localAddr.sin_port = htons(0); // AUTO PORT

        if (bind(clientUdpSocket, (sockaddr*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
            printLog("Failed to bind auto UDP port");
        } else {
        // Get assigned UDP port
        socklen_t len = sizeof(localAddr);
        getsockname(clientUdpSocket, (sockaddr*)&localAddr, &len);

        clientUdpPort = ntohs(localAddr.sin_port);
        printLog("Client UDP listening on port: " + to_string(clientUdpPort));
        }
    }


    // Start background threads
    thread heartbeatThread(sendHeartbeat, campusName);
    thread broadcastThread(listenForBroadcasts);
    thread messageThread(listenForMessages, tcpSocket);
    
    heartbeatThread.detach();
    broadcastThread.detach();
    messageThread.detach();
    
    // Give threads time to start
    this_thread::sleep_for(chrono::seconds(1));
    
    // Run main menu
    waitAndClear();
    displayMenu(tcpSocket, campusName);
    
    // Cleanup
    closesocket(tcpSocket);
    if (clientUdpSocket != INVALID_SOCKET) {
    closesocket(clientUdpSocket);
    }

    #ifdef _WIN32
    WSACleanup();
    #endif
    
    return 0;
}