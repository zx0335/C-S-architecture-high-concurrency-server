#ifndef client_h
#define client_h

#include <iostream>
#include <string>
#include <cstring>
#include <memory>
#include <thread>
#include <mutex>
#include <chrono>
#include <ctime>
#include <random>
#include <shared_mutex>
extern "C"
{
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/epoll.h>
}
using namespace std;

#define maxTask 2

// 客户端类
class Client
{
private:
    sockaddr_in serverAddr; // 服务器地址
    socklen_t len = sizeof(serverAddr);
    shared_mutex mutex_; // 读写锁

public:
    char name[32]{}; // 用户名
    int epfd;        // epoll实例
    int clientFd;    // 客户端套接字
    int udpFd;       // UDP套接字

    FILE *txtFd = nullptr; // 聊天记录
    char txtname[64]{};

    bool isLoad = false; // tcpSocket是否被占用

    Client();  // 构造函数
    ~Client(); // 析构函数

    int getTcpSocket();    // 获取TCP套接字
    int getUdpSocket();    // 获取UDP通信套接字
    int addEpoll(int &fd); // 将文件描述符加入epoll实例

    void recvData(); // 接收数据

    void login(); // 注册用户名

    void FileLoad(const char *filename); // 从服务器下载文件

    void sendData(); // 向服务器发送数据
    void recvFile(); // 接收别人发来的文件
    void sendFile(); // 通过服务器向别人发送文件
    void txt(const char *buf);
};

#endif