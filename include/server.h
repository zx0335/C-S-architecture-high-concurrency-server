#ifndef server_h
#define server_h

#include <iostream>
#include <cstring>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <memory>
extern "C"
{
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>
#include "sqlite3.h"
}
using namespace std;

struct clientInfo
{
    int fd;
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    char *msg = nullptr;
    clientInfo(int fd) : fd(fd) { memset(&addr, 0, sizeof(addr)); }
};

// 服务器类
class Server
{
private:
    Server(); // 构造函数

    static Server *server; // 静态对象指针
    sqlite3 *db = nullptr; // 数据库句柄指针
    char *error = nullptr; // 数据库错误码
    shared_mutex mutex_;   // 读写锁
public:
    int epfd;     // epoll实例
    int listenFd; // 监听套接字
    int udpFd;    // udp套接字

    // 单例设计模式
    Server(const Server &other) = delete;
    Server &operator=(const Server &other) = delete;
    ~Server();

    static Server *getInstance(); // 获取静态对象指针

    int getTcpSocket();    // 获取TCP监听套接字
    int getUdpSocket();    // 获取UDP通信套接字
    int addEpoll(int &fd); // 将文件描述符加入epoll实例

    int getsqliteConnfd(char *name); // 从数据库获取已连接套接字
    char *getnameByTcp(int connfd);  // 通过Tcp套接字获取用户名

    int addUser(char ip[16], unsigned short port, int connfd, char *name); // 添加用户到数据库

    char *getnameIp(char ip[16], unsigned short port); // 通过ip获取用户名

    void removeUser(char *name); // 从数据库删除用户

    // 错误信息线程,管道通信

    void acceptConnect(int listenFd); // 接受客户端连接
    void communicate(int connfd);     // 客户端发起通信
    void loadFile(int &connfd);       // 客户端下载文件
};

#endif