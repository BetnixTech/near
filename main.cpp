/*
ConnectHub Ultimate: Colored ASCII Graphics + Mini Games + Persistent Files
Compile: g++ -std=c++11 -pthread connecthub_ultimate.cpp -o connecthub_ultimate
Server: ./connecthub_ultimate server 12345
Client: ./connecthub_ultimate client 127.0.0.1 12345
*/

#include <iostream>
#include <thread>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>
#include <string>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <arpa/inet.h>

std::mutex mtx;
std::map<std::string,std::vector<int>> rooms;
std::map<std::string,char[3][3]> tictactoe_boards;
char whiteboard[10][20];
std::map<std::string,std::vector<std::string>> file_storage; // persistent text per room

// ANSI color codes
const std::string RED="\033[31m", GREEN="\033[32m", YELLOW="\033[33m", BLUE="\033[34m", RESET="\033[0m";

void broadcast(const std::string &room, const std::string &msg, int sender){
    mtx.lock();
    for(int c: rooms[room]){
        if(c!=sender) write(c,msg.c_str(),msg.size());
    }
    mtx.unlock();
}

void draw_whiteboard(std::string &room){
    std::stringstream ss;
    ss << BLUE << "[Whiteboard]\n";
    for(int i=0;i<10;i++){
        for(int j=0;j<20;j++){
            ss << whiteboard[i][j];
        }
        ss << "\n";
    }
    ss << RESET;
    broadcast(room,ss.str(),-1);
}

void draw_tictactoe(std::string &room){
    std::stringstream ss;
    ss << YELLOW << "[TicTacToe]\n";
    for(int i=0;i<3;i++){
        for(int j=0;j<3;j++){
            char c = tictactoe_boards[room][i][j];
            if(c=='X') ss << RED << c;
            else if(c=='O') ss << GREEN << c;
            else ss << '.';
        }
        ss << "\n";
    }
    ss << RESET;
    broadcast(room,ss.str(),-1);
}

void send_file_list(int client, std::string &room){
    mtx.lock();
    std::stringstream ss;
    ss << "[Files in room " << room << "]:\n";
    for(auto &f : file_storage[room]) ss << f << "\n";
    mtx.unlock();
    write(client,ss.str().c_str(),ss.str().size());
}

void client_handler(int client_socket){
    std::string current_room="lobby";
    mtx.lock(); rooms[current_room].push_back(client_socket);
    if(tictactoe_boards.find(current_room)==tictactoe_boards.end())
        for(int i=0;i<3;i++) for(int j=0;j<3;j++) tictactoe_boards[current_room][i][j]='.';
    mtx.unlock();
    char buffer[2048];
    while(true){
        memset(buffer,0,2048);
        int bytes=read(client_socket,buffer,2048);
        if(bytes<=0) break;
        std::string msg(buffer);

        if(msg.substr(0,5)=="/join"){
            std::string new_room=msg.substr(6);
            mtx.lock();
            rooms[current_room].erase(std::remove(rooms[current_room].begin(),rooms[current_room].end(),client_socket),rooms[current_room].end());
            rooms[new_room].push_back(client_socket);
            if(tictactoe_boards.find(new_room)==tictactoe_boards.end())
                for(int i=0;i<3;i++) for(int j=0;j<3;j++) tictactoe_boards[new_room][i][j]='.';
            mtx.unlock();
            current_room=new_room;
            std::string sys="[System] Joined room: "+new_room+"\n";
            write(client_socket,sys.c_str(),sys.size());
            draw_whiteboard(current_room);
            draw_tictactoe(current_room);
            send_file_list(client_socket,current_room);
            continue;
        } else if(msg.substr(0,5)=="/file"){
            std::string filename=msg.substr(6);
            std::ifstream file(filename);
            if(file){
                std::stringstream ss; ss << file.rdbuf();
                mtx.lock(); file_storage[current_room].push_back(filename); mtx.unlock();
                broadcast(current_room,"[File: "+filename+"]\n"+ss.str()+"\n",client_socket);
            }else{
                std::string err="[System] File not found\n";
                write(client_socket,err.c_str(),err.size());
            }
            continue;
        } else if(msg.substr(0,5)=="/draw"){
            int x=msg[6]-'0', y=msg[8]-'0'; char ch=msg[10];
            if(x>=0&&x<10&&y>=0&&y<20) whiteboard[x][y]=ch;
            draw_whiteboard(current_room);
            continue;
        } else if(msg.substr(0,5)=="/t3"){
            int r=msg[6]-'0', c=msg[8]-'0'; char ch=msg[10];
            if(r>=0&&r<3&&c>=0&&c<3&&(ch=='X'||ch=='O')) tictactoe_boards[current_room][r][c]=ch;
            draw_tictactoe(current_room);
            continue;
        } else if(msg.substr(0,5)=="/files"){
            send_file_list(client_socket,current_room);
            continue;
        }
        broadcast(current_room,msg+"\n",client_socket);
    }
    mtx.lock();
    rooms[current_room].erase(std::remove(rooms[current_room].begin(),rooms[current_room].end(),client_socket),rooms[current_room].end());
    mtx.unlock();
    close(client_socket);
}

int main(int argc,char* argv[]){
    if(argc<3){ std::cout<<"Usage:\nServer: "<<argv[0]<<" server <port>\nClient: "<<argv[0]<<" client <ip> <port>\n"; return 1;}
    std::string mode=argv[1];
    if(mode=="server"){
        int port=std::stoi(argv[2]);
        int server_fd=socket(AF_INET,SOCK_STREAM,0);
        if(server_fd==0){perror("socket"); return 1;}
        sockaddr_in address; address.sin_family=AF_INET; address.sin_addr.s_addr=INADDR_ANY; address.sin_port=htons(port);
        if(bind(server_fd,(struct sockaddr*)&address,sizeof(address))<0){perror("bind"); return 1;}
        listen(server_fd,10);
        std::cout<<"ConnectHub Ultimate Server running on port "<<port<<"\n";
        while(true){
            int addrlen=sizeof(address);
            int client_socket=accept(server_fd,(struct sockaddr*)&address,(socklen_t*)&addrlen);
            if(client_socket>=0) std::thread(client_handler,client_socket).detach();
        }
        close(server_fd);
    }else if(mode=="client"){
        if(argc<4){ std::cout<<"Client needs IP and port\n"; return 1;}
        std::string ip=argv[2]; int port=std::stoi(argv[3]);
        int sock=socket(AF_INET,SOCK_STREAM,0);
        if(sock<0){perror("socket"); return 1;}
        sockaddr_in serv_addr; serv_addr.sin_family=AF_INET; serv_addr.sin_port=htons(port);
        if(inet_pton(AF_INET,ip.c_str(),&serv_addr.sin_addr)<=0){perror("ip"); return 1;}
        if(connect(sock,(struct sockaddr*)&serv_addr,sizeof(serv_addr))<0){perror("connect"); return 1;}

        std::thread recv_thread([&](){
            char buffer[2048];
            while(true){
                memset(buffer,0,2048);
                int bytes=read(sock,buffer,2048);
                if(bytes<=0) break;
                std::cout<<buffer;
            }
        });

        std::string msg;
        while(true){
            std::getline(std::cin,msg);
            if(msg=="/quit") break;
            send(sock,msg.c_str(),msg.size(),0);
        }
        close(sock);
        recv_thread.join();
    }else std::cout<<"Unknown mode: "<<mode<<"\n";
    return 0;
}
