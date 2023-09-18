#include "server.h"
using namespace std;

int main()
{
    // 获取静态对象指针
    Server *server = Server::getInstance();

    // 就绪文件描述符队列
    struct epoll_event readyEv[1024]{};
    int size = sizeof(readyEv) / sizeof(readyEv[0]);

    cout << "等待连接......" << endl;
    while (true)
    {
        // 监测文件描述符是否就绪
        int num = epoll_wait(server->epfd, readyEv, size, -1);
        for (int i = 0; i < num; i++)
        {
            int fd = readyEv[i].data.fd;
            if (fd == server->listenFd) // 客户端发起连接
            {
                // cout << "listen套接字: " << fd << " 已就绪!" << endl;
                //  ref包装第三个参数防止编译报错
                thread t(&Server::acceptConnect, server, ref(fd));
                t.join();
                //usleep(50000);
                //t.detach(); // 线程分离
            }
            else if (fd == server->udpFd) // 客户端通信请求
            {
                // cout << "udp套接字: " << fd << " 已就绪!" << endl;
                thread t(&Server::communicate, server, ref(fd));
                t.join();
                //usleep(50000);
                //t.detach(); // 线程分离
            }
            else // 下载文件
            {
                // cout << "tcp套接字: " << fd << " 已就绪!" << endl;
                //  暂时从实例epoll实例中删除套接字
                if (epoll_ctl(server->epfd, EPOLL_CTL_DEL, fd, nullptr) == -1)
                {
                    perror("EPOLL_CTR_DELETE()");
                    continue;
                }
                thread t(&Server::loadFile, server, ref(fd));
                t.join();
                //sleep(1);
                //t.detach(); // 线程分离
            }
        }
    }
    cout << "服务器已退出" << endl;
}