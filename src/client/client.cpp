#include "client.h"

// 构造函数
Client::Client()
{
    // 设置服务器地址
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr("43.138.105.179");
    serverAddr.sin_port = htons(atoi("55555"));

    // 创建epoll实例,参数无所谓,但必须传大于0的整数
    epfd = epoll_create(1);
    if (epfd == -1)
    {
        perror("epoll_create()");
        exit(EXIT_FAILURE);
    }

    // 获取TCP套接字,UDP通信套接字
    if (getTcpSocket() == -1 || getUdpSocket() == -1)
        exit(EXIT_FAILURE);
}

// 获取TCP套接字
int Client::getTcpSocket()
{
    // 创建套接字
    clientFd = socket(PF_INET, SOCK_STREAM, 0);
    if (clientFd == -1)
    {
        perror("socket()");
        return -1;
    }

    // 更改缓冲区上限
    int bufferSize = 16384; // 1MB
    setsockopt(clientFd, SOL_SOCKET, SO_RCVBUF, &bufferSize, sizeof(bufferSize));
    setsockopt(clientFd, SOL_SOCKET, SO_SNDBUF, &bufferSize, sizeof(bufferSize));

    // 发起连接
    if (connect(clientFd, (sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    {
        perror("connect()");
        close(clientFd);
        return -1;
    }
    return 1;
}

// 获取UDP通信套接字
int Client::getUdpSocket()
{
    // 创建套接字
    udpFd = socket(PF_INET, SOCK_DGRAM, 0);
    if (udpFd == -1)
    {
        perror("socket()");
        return -1;
    }
    random_device rd;                             // 用于获取随机数种子
    mt19937 gen(rd());                            // 使用Mersenne Twister算法生成随机数
    uniform_int_distribution<> dis(55000, 65500); // 定义一个均匀分布的整数范围

    int random_number = dis(gen); // 生成随机数

    sockaddr_in cliaddr;
    // 设置客户端地址
    bzero(&cliaddr, sizeof(cliaddr));
    cliaddr.sin_family = AF_INET;
    cliaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    cliaddr.sin_port = htons(random_number); // 选择一个可用的端口

    // 绑定套接字到客户端地址
    bind(udpFd, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
    return 1;
}

// 将文件描述符加入epoll实例
int Client::addEpoll(int &fileFd)
{
    // 将文件描述符添加到epoll实例中
    struct epoll_event observe;
    observe.events = EPOLLIN | EPOLLET; // 监测读事件,设置边缘触发
    observe.data.fd = fileFd;
    if (epoll_ctl(this->epfd, EPOLL_CTL_ADD, fileFd, &observe) == -1) // 把参数4拷贝到参数1
    {
        perror("epoll_ctl_ADD()");
        return -1;
    }
    return 1;
}

void Client::login() // 注册用户名
{
    while (1) // 注册用户名
    {
        string str{};
        cout << "请输入您的用户名:" << endl;
        getline(cin, str);
        if (str.length() > 31)
        {
            cout << "用户名太长，请重新输入!" << endl;
            continue;
        }
        else if (str == "exit")
        {
            cout << "用户名不可用" << endl;
            continue;
        }

        strcpy(name, str.c_str());
        sprintf(txtname, "%s.txt", name);
        txtFd = fopen(txtname, "w+");
        if (txtFd == nullptr)
            perror("fopen() txtFd");
        fclose(txtFd);
        txtFd = nullptr;

        sendto(udpFd, name, strlen(name), 0, (sockaddr *)&serverAddr, len);
        cout << "发送:" << name << endl; //*****

        char buf[128];
        recvfrom(udpFd, buf, sizeof(buf), 0, (sockaddr *)&serverAddr, &len);
        if (strcmp(buf, "注册成功") == 0)
        {
            cout << buf << endl;
            break;
        }
        cout << buf << endl;
    }
}

void Client::txt(const char *msg)
{
    txtFd = fopen(txtname, "a+");
    if (txtFd == nullptr)
        perror("fopen() txtFd");

    char buf[1024]{};
    auto now = chrono::system_clock::now();
    time_t time_now = chrono::system_clock::to_time_t(now);
    sprintf(buf, "%s\t%s", msg, ctime(&time_now));
    fwrite(buf, 1, sizeof(buf), txtFd);

    fclose(txtFd);
    txtFd = nullptr;
    // cout << "已成功写入" << endl;
}

// 接收发来的文件
void Client::recvFile()
{
    unique_lock<shared_mutex> lg(mutex_); // 加锁

    char buf[128]{};
    recv(clientFd, buf, sizeof(buf), 0);
    cout << buf << endl;
    isLoad = false;
}

// 接收数据
void Client::recvData()
{
    char msg[1024]{};
    socklen_t len = sizeof(serverAddr);
    while (true)
    {
        memset(msg, 0, sizeof(msg));
        if (recvfrom(udpFd, msg, sizeof(msg), 0, (sockaddr *)&serverAddr, &len) == -1)
        {
            perror("recvfrom()");
            return;
        }
        if (strncmp(msg, "begin", 5) == 0)
        {
            while (1)
            {
                memset(msg, 0, sizeof(msg));
                recvfrom(udpFd, msg, sizeof(msg), 0, (sockaddr *)&serverAddr, &len);
                if (strncmp(msg, "end", 3) == 0)
                    break;
                //cout << __LINE__ << endl;
                cout << msg << endl;
            }
        }
        else
        {
            cout << msg << endl;
            txt(msg);
            //cout << __LINE__ << endl;
        }
        /*if (strncmp(msg, "other:", 6) == 0) // 其他人发来文件
        {
            if (isLoad)
            {
                send(clientFd, "对方繁忙!", 14, 0);
                continue;
            }
            isLoad = true;
            cout << msg << endl;
            send(clientFd, "yes", 4, 0); // 开始接收
            cout << "开始下载别人发来的文件" << endl;
            // thread t(&Client::recvFile, this);
            // t.join();
            // t.detach();
            //*******
            isLoad = false;
        }*/
    }
}

// 从服务器下载文件
void Client::FileLoad(const char *filename)
{
    // cout << "2filename: " << filename << endl;
    FILE *fd = fopen(filename, "w+");
    if (fd == nullptr)
    {
        perror("fopen()");
        return;
    }
    cout << "正在下载......" << endl;
    size_t num{};
    char *msg = new char[65536];

    while (true) // 接收从服务器发来的文件数据
    {
        memset(msg, 0, 65536);
        num = recv(clientFd, msg, 65536, 0);
        //************
        // cout << "num = " << num << endl;
        if (strcmp(msg, "FINISH") == 0)
        {
            cout << "下载完毕!" << endl;
            fclose(fd);
            break;
        }
        else if (strcmp(msg, "UNFIND") == 0)
        {
            cout << "没有找到该文件!" << endl;
            fclose(fd);
            break;
        }
        else if (strncmp(msg, "error", 5) == 0)
            break;
        fwrite(msg, 1, num, fd);
    }
    delete[] msg;
    isLoad = false;
}

// 通过服务器向别人发送文件
void Client::sendFile()
{
}

// 向服务器发送数据
void Client::sendData()
{
    char buf[1024]{};
    while (true) // 发送数据
    {
        string str{};
        getline(cin, str);
        if (str.size() > 1023)
        {
            cout << "输入长度超出限制,请重新输入!" << endl;
            continue;
        }

        if (str == "exit") // 断开连接
        {
            char buf[128]{};
            sprintf(buf, "exit:%s", name);
            // 发送退出标志
            if (sendto(udpFd, buf, strlen(buf), 0, (sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
            {
                perror("sendto() exit");
                continue;
            }
            close(clientFd);
            close(udpFd);
            // fclose(txtFd);
            break;
        }
        else if (str == "cat") // 查看聊天记录
        {
            char shell[256]{};
            sprintf(shell, "cat %s", txtname);
            system(shell);
        }
        else if (str == "all") // 查看在线人员
        {
            cout << "用户名如下:" << endl;
            if (sendto(udpFd, str.c_str(), str.size(), 0, (sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
                perror("sendto()");
        }
        else if (str == "load") // 从服务器下载文件
        {
            if (isLoad)
            {
                cout << "正在下载文件,请稍后" << endl;
                continue;
            }
            isLoad = true;

            if (send(clientFd, "load", 5, 0) == -1)
                perror("load send()");
            cout << "服务器文件列表如下:" << endl;
            while (true) // 获取服务器的文件列表
            {
                char buf[512]{};
                recv(clientFd, buf, sizeof(buf), 0);
                if (strcmp(buf, "FINISH") == 0) // 文件列表打印结束
                    break;
                cout << buf;
            }

            str.clear();
            cout << "请选择您要下载的文件:";
            getline(cin, str);
            send(clientFd, str.c_str(), str.size(), 0);

            // cout << "1filename: " << str.c_str() << endl;
            thread t(&Client::FileLoad, this, str.c_str()); // 开始下载
            // t.join();
            sleep(1);   // 防止参数无法被接收
            t.detach(); // 线程分离
        }
        /*else if (strncmp(str.c_str(), "other:", 6) == 0) // 向其他用户传输文件
        {
            if (isLoad)
            {
                cout << "正在下载文件,请稍后" << endl;
                continue;
            }
            isLoad = true;

            char temp[1024]{}, src[1024]{};
            strcpy(src, str.c_str());
            strcpy(temp, str.c_str());
            strtok(temp, ":");
            char *usrname = strtok(nullptr, ":");
            if (strcmp(usrname, name) == 0)
            {
                cout << "不能给自己发送文件" << endl;
                isLoad = false;
                continue;
            }
            char *filepath = strtok(nullptr, "");
            if (filepath == nullptr || usrname == nullptr)
            {
                cout << "格式错误" << endl;
                isLoad = false;
                continue;
            }
            char *filename = strrchr(filepath, '/');
            cout << __LINE__ << "temp:" << temp << " filepath:" << filepath << " filename: " << filename << endl;
            if (filename == nullptr) // 没有'/'
                filename = filepath;
            else // 跳过'/'
                filename += 1;
            cout << "文件路径:" << filepath << " 文件名:" << filename << endl;
            cout<<"src: " << src << endl;
            if (send(clientFd, src, strlen(src), 0) == -1)
                perror("other send()");
            char msg[64];
            recv(clientFd, msg, 64, 0);
            if (strcmp(msg, "yes") == 0) // 开始发送文件
            {
                cout << "开始发送" << endl;
            }
            else
            {
                cout << "对方繁忙!" << endl;
            }

            isLoad = false;
        }*/
        else // 通信请求
        {
            if (sendto(udpFd, str.c_str(), str.size(), 0, (sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
                perror("sendto()");
            char buf[1064]{};
            sprintf(buf, "me=>%s", str.c_str());
            txt(buf);
        }
    }
}

// 析构函数
Client::~Client() {}