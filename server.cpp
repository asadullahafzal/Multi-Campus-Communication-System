#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <limits>

using namespace std;

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
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

#define TCP_PORT 8080
#define UDP_PORT 8081
#define BUFFER_SIZE 4096
//just some things for terminal design
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"
#define BLACK   "\033[30m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
#define BRIGHT_BLACK   "\033[90m"
#define BRIGHT_RED     "\033[91m"
#define BRIGHT_GREEN   "\033[92m"
#define BRIGHT_YELLOW  "\033[93m"
#define BRIGHT_BLUE    "\033[94m"
#define BRIGHT_MAGENTA "\033[95m"
#define BRIGHT_CYAN    "\033[96m"
#define BRIGHT_WHITE   "\033[97m"

//mutex for safety orpeations operations
mutex clientMutex;
mutex consoleMutex;

//to hold campus client info
struct CampusClient {
    SOCKET tcpSocket;
    string campusName;
    string lastHeartbeat;
    bool isActive;
    sockaddr_in udpAddr;
    bool hasUdpAddr = false;
};

//map to store connected campus clients (campusName -> CampusClient)
map<string, CampusClient> connectedClients;

//campus id pass
map<string, string> validCredentials = {
    {"Islamabad", "NU-ISB-123"},
    {"Lahore", "NU-LHR-123"},
    {"Karachi", "NU-KHI-123"},
    {"Peshawar", "NU-PEW-123"},
    {"CFD", "NU-CFD-123"},
    {"Multan", "NU-MLT-123"}
};

void enableANSI() {
    #ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    #endif
}
void clearScreen() {
    #ifdef _WIN32
    system("cls");
    #else
    system("clear");
    #endif
}
void waitForKey(const string& message = "Press Enter to continue...") {
    cout << "\n" << DIM << message << RESET;
    cin.get();
}

//function to get current timestamp
string getCurrentTime() {
    auto now = chrono::system_clock::now();
    auto time = chrono::system_clock::to_time_t(now);
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", localtime(&time));
    return string(buffer);
}

//a line for decoration
void printLine(const string& color = CYAN, char ch = '=', int length = 80) {
    cout << color << string(length, ch) << RESET << endl;
}
void printHeader(const string& text, const string& color = BRIGHT_CYAN) {
    int padding = (80 - static_cast<int>(text.length())) / 2;
    printLine(color, '=', 80);
    cout << color << "|" << string(max(0, padding - 1), ' ') << BOLD << text 
              << RESET << color << string(max(0, 80 - padding - static_cast<int>(text.length()) - 1), ' ') << "|" << RESET << endl;
    printLine(color, '=', 80);
}

//thread-safe output (stylish hehe)
void printLog(const string& message, const string& type = "INFO") {
    lock_guard<mutex> lock(consoleMutex);
    
    string typeColor, icon;
    
    if (type == "SUCCESS") {
        typeColor = BRIGHT_GREEN;
        icon = "[+]";
    } else if (type == "ERROR") {
        typeColor = BRIGHT_RED;
        icon = "[X]";
    } else if (type == "WARNING") {
        typeColor = BRIGHT_YELLOW;
        icon = "[!]";
    } else if (type == "CONNECT") {
        typeColor = BRIGHT_CYAN;
        icon = "[*]";
    } else if (type == "DISCONNECT") {
        typeColor = BRIGHT_MAGENTA;
        icon = "[o]";
    } else if (type == "ROUTE") {
        typeColor = BRIGHT_BLUE;
        icon = "[>]";
    } else if (type == "HEARTBEAT") {
        typeColor = GREEN;
        icon = "[<3]";
    } else if (type == "BROADCAST") {
        typeColor = YELLOW;
        icon = "[B]";
    } else {
        typeColor = WHITE;
        icon = "[i]";
    } 
    cout << DIM << "[" << BRIGHT_WHITE << getCurrentTime() << DIM << "]" << RESET 
              << " " << typeColor << icon << " " << BOLD << setw(12) << left << type << RESET 
              << " " << WHITE << message << RESET << endl;
}

//authenticating(client logging in)
bool authenticateClient(const string& authMsg, string& campusName) {
    size_t campusPos = authMsg.find("Campus:");
    size_t passPos = authMsg.find(",Pass:");
    if (campusPos == string::npos || passPos == string::npos) {
        return false;
    }
    campusName = authMsg.substr(campusPos + 7, passPos - campusPos - 7);
    string password = authMsg.substr(passPos + 6);
    auto it = validCredentials.find(campusName);
    if (it != validCredentials.end() && it->second == password) {
        return true;
    }
    return false;
}
//send message to specific campus
bool sendToClient(const string& targetCampus, const string& message) {
    lock_guard<mutex> lock(clientMutex);//synchornization     
    auto it = connectedClients.find(targetCampus);
    if (it != connectedClients.end() && it->second.isActive) {
        int result = send(it->second.tcpSocket, message.c_str(), static_cast<int>(message.length()), 0);
        return result != SOCKET_ERROR;
    }
    return false;
}
//handle individual campus client TCP connection
void handleCampusClient(SOCKET clientSocket, sockaddr_in clientAddr) {
    char buffer[BUFFER_SIZE];
    string campusName;     
    //authentication phase
    memset(buffer, 0, BUFFER_SIZE);
    int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
    if (bytesReceived <= 0) {
        printLog("Client disconnected before authentication", "WARNING");
        closesocket(clientSocket);
        return;
    }
    string authMsg(buffer);
    if (!authenticateClient(authMsg, campusName)) {
        string response = "AUTH_FAILED";
        send(clientSocket, response.c_str(), static_cast<int>(response.length()), 0);
        printLog("Authentication failed for: " + authMsg, "ERROR");
        closesocket(clientSocket);
        return;
    }
    //check and avoid already connected campus
    {
        lock_guard<mutex> lock(clientMutex);
        if (connectedClients.find(campusName) != connectedClients.end() && 
            connectedClients[campusName].isActive) {
            string response = "ALREADY_CONNECTED";
            send(clientSocket, response.c_str(), static_cast<int>(response.length()), 0);
            printLog("Campus " + campusName + " already connected", "WARNING");
            closesocket(clientSocket);
            return;
        }
    }
    string response = "AUTH_SUCCESS";
    send(clientSocket, response.c_str(), static_cast<int>(response.length()), 0);     
    //register client
    {
        lock_guard<mutex> lock(clientMutex);
        CampusClient client;
        client.tcpSocket = clientSocket;
        client.campusName = campusName;
        client.lastHeartbeat = getCurrentTime();
        client.isActive = true;
        connectedClients[campusName] = client;
    }     
    printLog(string("Campus ") + CYAN + campusName + RESET + " authenticated successfully", "CONNECT");
    
    //main message handling loop
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytesReceived <= 0) {
            printLog(string("Campus ") + CYAN + campusName + RESET + " disconnected", "DISCONNECT");
            break;
        }         
        string message(buffer, bytesReceived);
        size_t targetPos = message.find("TARGET:");
        size_t deptPos = message.find("|DEPT:");
        size_t fromPos = message.find("|FROM:");
        size_t msgPos = message.find("|MSG:");
        
        if (targetPos != string::npos && deptPos != string::npos && 
            fromPos != string::npos && msgPos != string::npos) {
            
            string targetCampus = message.substr(targetPos + 7, deptPos - targetPos - 7);
            string targetDept = message.substr(deptPos + 6, fromPos - deptPos - 6);
            string sourceCampus = message.substr(fromPos + 6, msgPos - fromPos - 6);
            string msg = message.substr(msgPos + 5);   
            printLog(string(CYAN) + sourceCampus + RESET + " -> " + YELLOW + targetCampus + RESET + 
                     " [" + GREEN + targetDept + RESET + "]", "ROUTE");         
            //forward message to target campus
            if (sendToClient(targetCampus, message)) {
                string ack = "ACK:Message delivered to " + targetCampus;
                send(clientSocket, ack.c_str(), static_cast<int>(ack.length()), 0);
            } else {
                string error = "ERROR:Unable to deliver message to " + targetCampus;
                send(clientSocket, error.c_str(), static_cast<int>(error.length()), 0);
                printLog("Failed to route message to: " + targetCampus, "ERROR");
            }
        }
    }
    
    //cleanup
    {
        lock_guard<mutex> lock(clientMutex);
        connectedClients[campusName].isActive = false;
        closesocket(clientSocket);
    }
}

void handleUDPHeartbeat() {
    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        printLog("Failed to create UDP socket", "ERROR");
        return;
    }   
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;           //IPv4 addressing
    serverAddr.sin_addr.s_addr = INADDR_ANY;//accept packets on any local network interface
    serverAddr.sin_port = htons(UDP_PORT);  //convert port to network byte order and set it
      
    if (bind(udpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printLog("Failed to bind UDP socket", "ERROR"); //port already in use,binding failed
        closesocket(udpSocket);
        return;
    }     
    printLog("UDP heartbeat listener started on port " + to_string(UDP_PORT), "SUCCESS");     
    char buffer[BUFFER_SIZE];
    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);//to store sender's ip and port number     
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytesReceived = recvfrom(udpSocket, buffer, BUFFER_SIZE - 1, 0, 
                                     (sockaddr*)&clientAddr, &clientAddrLen);         
        if (bytesReceived > 0) {
            string message(buffer);
            if (message.find("HEARTBEAT:") == 0) {
                size_t secondColon = message.find(":", 10);
                if (secondColon == string::npos) continue;

                string campusName = message.substr(10, secondColon - 10); //Extract campus name
                int udpPort = stoi(message.substr(secondColon + 1));

                {
                    lock_guard<mutex> lock(clientMutex); //thread-safe access to connectedClients map
                    auto it = connectedClients.find(campusName);

                    if (it != connectedClients.end()) {
                        it->second.lastHeartbeat = getCurrentTime();   //update last heartbeat time
                        it->second.udpAddr = clientAddr;               //update stored UDP address
                        it->second.udpAddr.sin_port = htons(udpPort);  //set correct sender port
                        it->second.hasUdpAddr = true;                  //mark UDP info as valid

                        printLog(string(CYAN) + campusName + RESET + " @ " + 
                                 string(inet_ntoa(clientAddr.sin_addr)) + ":" + 
                                 to_string(udpPort), "HEARTBEAT");
                    }
                }
            }
        }
    }
    closesocket(udpSocket);
}

//for broadcasting announcements(server to all clients)
void adminModule(SOCKET udpSocket) {
    string input;
    while (true) {
        clearScreen();
        cout << "\n";
        printLine(BRIGHT_YELLOW, '-', 80);
        cout << BRIGHT_YELLOW << "+" << string(78, '-') << "+" << RESET << endl;
        cout << BRIGHT_YELLOW << "|" << BOLD << setw(35) << right << "" 
                  << "ADMIN CONSOLE" << setw(31) << "" << RESET << BRIGHT_YELLOW << "|" << RESET << endl;
        cout << BRIGHT_YELLOW << "+" << string(78, '-') << "+" << RESET << endl;
        printLine(BRIGHT_YELLOW, '-', 80);
        
        cout << BRIGHT_CYAN << "  [1]" << RESET << " View Connected Campuses" << endl;
        cout << BRIGHT_CYAN << "  [2]" << RESET << " Broadcast Announcement" << endl;
        cout << BRIGHT_CYAN << "  [3]" << RESET << " Exit Admin" << endl;
        printLine(BRIGHT_YELLOW, '-', 80);
        cout << BRIGHT_WHITE << ">> Choice: " << RESET;
        
        getline(cin, input);     
        if (input == "1") {
            clearScreen();
            lock_guard<mutex> lock(clientMutex);
            cout << "\n";
            printHeader("CONNECTED CAMPUSES STATUS", BRIGHT_GREEN);
            
            if (connectedClients.empty()) {
                cout << YELLOW << "\n  [!] No campuses connected yet.\n" << RESET << endl;
            } else {
                cout << "\n";
                cout << BOLD << "  " << setw(20) << left << "CAMPUS" 
                          << setw(25) << "LAST HEARTBEAT" 
                          << setw(15) << "STATUS" << RESET << endl;
                printLine(CYAN, '-', 80);
                
                for (const auto& pair : connectedClients) {
                    string status;
                    if (pair.second.isActive) {
                        status = string(GREEN) + "[*] ONLINE" + RESET;
                    } else {
                        status = string(RED) + "[o] OFFLINE" + RESET;
                    }                     
                    cout << "  "
                        << CYAN  << setw(20) << left << pair.first          << RESET   //campus name
                        << WHITE << setw(25) << pair.second.lastHeartbeat << RESET   //last heartbeat time
                        << status                                                  //online/Offline text
                        << endl;
                }
            }
            cout << endl;
            printLine(CYAN, '=', 80);   
            // ensure any leftover newline is cleared
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            waitForKey();
            
        } else if (input == "2"){
            clearScreen();
            cout << "\n";
            printLine(YELLOW, '-', 80);
            cout << BRIGHT_YELLOW << "[B] Enter announcement message:\n" << RESET;
            cout << BRIGHT_WHITE << ">> " << RESET;
            string announcement;
            getline(cin, announcement);         
            string broadcastMsg = "BROADCAST:" + announcement;             
            int sentCount = 0;
            {//lock the client list so no other thread can modify it
             //while we’re iterating over it
             lock_guard<mutex> lock(clientMutex);
             for (const auto& pair : connectedClients) {
                 //only send to clients that are currently marked ACTIVE
                 //and have already sent us a heartbeat (so we know their IP/port)
                 if (pair.second.isActive && pair.second.hasUdpAddr) {
                     //send the broadcast message directly to this campus's
                     //last known UDP address (stored from its heartbeat)
                     sendto(
                         udpSocket,
                         broadcastMsg.c_str(),
                         static_cast<int>(broadcastMsg.length()),
                         0,
                         (sockaddr*)&pair.second.udpAddr,
                         sizeof(pair.second.udpAddr)
                     );
                     //track how many clients actually received the broadcast
                     sentCount++;
                 }
              }
             }

            cout << "\n";
            printLog("Broadcast sent to " + to_string(sentCount) + " campus(es): \"" + 
                     announcement + "\"", "BROADCAST");             
            waitForKey();             
        } else if (input == "3") {
            clearScreen();
            printLog("Exiting admin console...", "INFO");
            break;
        } else {
            clearScreen();
            printLog("Invalid choice! Please select 1, 2, or 3.", "WARNING");
            this_thread::sleep_for(chrono::seconds(2));
        }
    }
}

int main() {
    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << RED << "[X] WSAStartup failed" << RESET << endl;
        return 1;
    }
    #endif
    enableANSI();
    cout << "\033[2J\033[H";
    //===============print banner================
    cout << "\n";
    printLine(BRIGHT_CYAN, '=', 80);
    cout << BRIGHT_CYAN << "|" << string(78, ' ') << "|" << RESET << endl;
    cout << BRIGHT_CYAN << "|" << BRIGHT_WHITE << BOLD << setw(45) << right 
              << "NU-INFORMATION EXCHANGE SYSTEM" << setw(34) << "" << RESET 
              << BRIGHT_CYAN << "|" << RESET << endl;
    cout << BRIGHT_CYAN << "|" << CYAN << setw(47) << right 
              << "Central Server" << setw(32) << "" << RESET 
              << BRIGHT_CYAN << "|" << RESET << endl;
    cout << BRIGHT_CYAN << "|" << string(78, ' ') << "|" << RESET << endl;
    printLine(BRIGHT_CYAN, '=', 80);
    cout << "\n";
    
    printLog("Initializing server components...", "INFO");
    
    //create TCP socket
    SOCKET tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcpSocket == INVALID_SOCKET) {
        printLog("Failed to create TCP socket", "ERROR");
        return 1;
    }
    
    //create UDP socket for broadcasting
    SOCKET udpBroadcastSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    //bind TCP socket
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(TCP_PORT);
    
    if (bind(tcpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printLog("Failed to bind TCP socket on port " + to_string(TCP_PORT), "ERROR");
        closesocket(tcpSocket);
        return 1;
    }
    
    if (listen(tcpSocket, 10) == SOCKET_ERROR) {
        printLog("Failed to listen on TCP socket", "ERROR");
        closesocket(tcpSocket);
        return 1;
    }
    
    printLog("TCP Server listening on port " + to_string(TCP_PORT), "SUCCESS");
    
    //start UDP heartbeat listener thread
    thread udpThread(handleUDPHeartbeat);
    udpThread.detach();
    
    //small delay for UDP thread to start
    this_thread::sleep_for(chrono::milliseconds(500));
    
    printLog("Server ready to accept campus connections", "SUCCESS");
    cout << "\n";
    printLine(GREEN, '=', 80);
    cout << "\n";
    
    //wait for user before starting admin console
    cout << BRIGHT_YELLOW << "  Press Enter to start admin console..." << RESET << flush;
    string dummy;
    getline(cin, dummy);
    
    //start admin module in separate thread
    thread adminThread(adminModule, udpBroadcastSocket);
    adminThread.detach();
    
    //accept client connections
    while (true) {
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);         
        SOCKET clientSocket = accept(tcpSocket, (sockaddr*)&clientAddr, &clientAddrLen);         
        if (clientSocket != INVALID_SOCKET) {
            printLog("Connection attempt from " + string(inet_ntoa(clientAddr.sin_addr)), "INFO");         
           //handle each client in a separate thread
            thread clientThread(handleCampusClient, clientSocket, clientAddr);
            clientThread.detach();
        }
    }   
    closesocket(tcpSocket);
    closesocket(udpBroadcastSocket);     
    #ifdef _WIN32
    WSACleanup();
    #endif     
    return 0;
}