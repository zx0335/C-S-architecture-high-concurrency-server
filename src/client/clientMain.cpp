#include "client.h"
using namespace std;

static pthread_t tid1;
static Client client;     // 创建类对象
bool isRunThread = false; // 判断线程是否在运行

// 捕获2号信号 (ctrl+c)
void out(int sig)
{
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("43.138.105.179");
    serverAddr.sin_port = htons(atoi("55555"));
    socklen_t len = sizeof(serverAddr);
    char buf[128]{};
    sprintf(buf, "exit:%s", client.name);
    if (sendto(client.udpFd, buf, strlen(buf), 0, (sockaddr *)&serverAddr, len) == -1)
    {
        perror("send()");
        return;
    }
    if (isRunThread)
    {
        pthread_cancel(tid1);
        //pthread_cancel(tid2);
    }
    cout << endl
         << "您已断开连接" << endl;
    close(client.clientFd);
    //fclose(client.txtFd);
    close(client.udpFd);
    exit(0);
}

// 接收数据线程函数
void *recvDataThead(void *arg)
{
    client.recvData();
    return nullptr;
}

// 接收数据线程函数

int main(int argc, char const *argv[])
{
    signal(SIGINT, out); // 注册2号信号

    client.login(); // 注册用户名

    pthread_create(&tid1, nullptr, recvDataThead, nullptr);  // 创建线程,接收聊天信息
    
    isRunThread = true;

    client.sendData(); // 向服务器发送数据

    pthread_cancel(tid1); // 取消线程

    cout << endl
         << "您已断开连接" << endl;
}