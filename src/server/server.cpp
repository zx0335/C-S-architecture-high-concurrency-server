#include "server.h"

// 静态对象类外初始化
Server *Server::server = nullptr;

// 获取静态对象指针
Server *Server::getInstance()
{
    if (server == nullptr)
    {
        server = new Server();
    }
    return server;
}

// 构造函数
Server::Server()
{
    // systemp("rm userData.db");
    int ret;
    // 创建数据库
    ret = sqlite3_open_v2("userData.db", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (ret != SQLITE_OK) // 不能使用perror()
    {
        fprintf(stderr, "创建数据库文件失败: %s\n", sqlite3_errmsg(db));
        exit(EXIT_FAILURE); // 异常退出
    }

    // 创建名为 user 的表,存在就不创建,使用多个字符串常量并通过拼接来实现换行的效果
    char create_str[] = "create table if not exists user("
                        "ip TEXT,"
                        "port INTEGER,"
                        "connfd INTEGER,"
                        "name TEXT PRIMARY KEY"
                        ");";
    ret = sqlite3_exec(db, create_str, nullptr, nullptr, &error);
    if (ret != SQLITE_OK)
    {
        // 函数运行失败,会调用sqlite3_malloc()为error分配内存
        fprintf(stderr, "创建表失败: %s\n", error);
        sqlite3_free(error);
        error = nullptr;
        exit(EXIT_FAILURE); // 异常退出
    }
    cout << "数据库创建成功!" << endl;

    // 创建epoll实例,参数无所谓,但必须传大于0的整数
    epfd = epoll_create(1);
    if (epfd == -1)
    {
        perror("epoll_create()");
        exit(EXIT_FAILURE);
    }

    // 获取TCP监听套接字,UDP通信套接字
    if (getTcpSocket() == -1 || getUdpSocket() == -1)
        exit(EXIT_FAILURE);
    // 将套接字添加到epoll实例中
    if (addEpoll(listenFd) == -1 || addEpoll(udpFd) == -1)
        exit(EXIT_FAILURE);
}

// 获取TCP监听套接字
int Server::getTcpSocket()
{
    // 创建监听套接字
    listenFd = socket(PF_INET, SOCK_STREAM, 0);
    if (listenFd == -1)
    {
        perror("socket()");
        return -1;
    }
    // 允许端口重用,取消time_wait状态,绑定端口时不会出现被占用的情况
    int on = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    // 设置服务端ip和端口
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(atoi("55555"));
    // 绑定套接字
    if (bind(listenFd, (sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    {
        perror("bind()");
        close(listenFd);
        return -1;
    }
    // 设置监听
    if (listen(listenFd, 20) == -1)
    {
        perror("listen()");
        close(listenFd);
        return -1;
    }
    cout << "监听套接字创建成功" << endl;
    return 1;
}

// 获取UDP通信套接字
int Server::getUdpSocket()
{
    udpFd = socket(PF_INET, SOCK_DGRAM, 0);
    if (udpFd == -1)
    {
        perror("socket()");
        return -1;
    }
    // 允许端口重用
    int enable = 1;
    setsockopt(udpFd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    // 设置地址信息
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(atoi("55555"));
    // 绑定套接字
    if (bind(udpFd, (sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    {
        perror("bind()");
        close(udpFd);
        return -1;
    }
    return 1;
}

// 将文件描述符加入epoll实例
int Server::addEpoll(int &fileFd)
{
    // 将文件描述符添加到epoll实例中
    struct epoll_event observe;
    observe.events = EPOLLIN | EPOLLET; // 监测读事件,设置边缘触发
    observe.data.fd = fileFd;
    if (epoll_ctl(this->epfd, EPOLL_CTL_ADD, fileFd, &observe) == -1) // 把参数4拷贝到参数1
    {
        //*******改用管道
        perror("epoll_ctl_ADD()");
        return -1;
    }
    return 1;
}

// 查到重复用户名,触发回调函数
int callback(void *arg, int len, char **col_val, char **col_name)
{
    bool *isRepeat = (bool *)arg;
    *isRepeat = true;
    return 0;
}

// 添加用户到数据库
int Server::addUser(char ip[16], unsigned short port, int connfd, char *name)
{
    char SQL[300]{};       // sqlite语句
    bool isRepeat = false; // 是否重复

    unique_lock<shared_mutex> lg(mutex_); // 独占读写锁

    sprintf(SQL, "SELECT * FROM user WHERE name = '%s'", name);
    int ret = sqlite3_exec(db, SQL, callback, &isRepeat, nullptr);
    if (ret != SQLITE_OK)
        return -1;
    if (isRepeat) // 重复
        return 0;

    memset(SQL, 0, sizeof(SQL));
    sprintf(SQL, "INSERT INTO user (ip,port,connfd,name) values ('%s',%d,%d,'%s')", ip, port, connfd, name);
    ret = sqlite3_exec(db, SQL, nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK)
        return -1;

    return 1;
}

int getFd(void *arg, int len, char **col_val, char **col_name)
{
    int *fd = (int *)arg;
    *fd = atoi(col_val[2]); // 第三列是套接字
    cout << "col_val[2]=" << col_val[2] << " connfd = " << *fd << endl;
    return 0;
}

// 从数据库获取已连接套接字
int Server::getsqliteConnfd(char *name)
{
    //*********改为智能指针
    char SQL[200]{};
    // 跟据ip和port找到对应的tcp套接字
    sprintf(SQL, "SELECT * FROM user WHERE name='%s'", name);
    int connfd{};
    unique_lock<shared_mutex> lg(mutex_); // 独占锁
    if (sqlite3_exec(db, SQL, getFd, &connfd, &error) != SQLITE_OK)
    {
        fprintf(stderr, "查询失败: %s\n", error);
        sqlite3_free(error);
        error = nullptr;
        return -1;
    }
    // cout << "getsqliteConnfd() connfd:" << connfd << endl;
    return connfd;
}

// 从数据库删除用户
void Server::removeUser(char *destName)
{
    //*********改为智能指针
    char SQL[200]{}; // sqlite语句
    // cout << "正在删除" << ip << "-" << port << endl;//端口号
    sprintf(SQL, "DELETE FROM user WHERE name='%s'", destName);
    unique_lock<shared_mutex> lg(mutex_); // 独占锁
    if (sqlite3_exec(db, SQL, nullptr, nullptr, &error) != SQLITE_OK)
    {
        fprintf(stderr, "查询失败: %s\n", error);
        sqlite3_free(error);
        error = nullptr;
    }
}

// 接受客户端连接
void Server::acceptConnect(int listenFd)
{
    sockaddr_in clientAddr;
    socklen_t len = sizeof(clientAddr);
    memset(&clientAddr, 0, sizeof(clientAddr));
    int clientFd = accept(listenFd, (sockaddr *)&clientAddr, &len);
    // cout << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << "]已连接到服务器!" << endl; //************

    while (1)
    {
        char name[32]{}; // 必须进行空初始化,不然有上次循环的残留数据

        // cout << "等待客户端发送用户名...." << endl; //********
        //  接收客户端发来的用户名
        sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);
        int ret = recvfrom(udpFd, name, sizeof(name), 0, (sockaddr *)&clientAddr, &len);
        // cout << "name:" << name << endl;
        if (ret == -1)
        {
            // 发送错误信息,继续等待接收用户名
            sendto(udpFd, strerror(errno), strlen(strerror(errno)), 0, (sockaddr *)&clientAddr, len);
            return;
        }
        else if (ret == 0 || strncmp(name, "exit", 4) == 0) // 断开连接
        {
            cout << "未注册成功便退出" << endl; //********
            close(udpFd);
            return;
        }

        cout << "ip:" << inet_ntoa(clientAddr.sin_addr) << " port:" << ntohs(clientAddr.sin_port) << " connfd:" << clientFd << " name:" << name << endl;
        //  将用户数据存储到数据库
        int result = addUser(inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFd, name);
        if (result == 1)
        {
            sendto(udpFd, "注册成功", 13, 0, (sockaddr *)&clientAddr, len);
            break;
        }
        else if (result == 0)
        {
            sendto(udpFd, "用户名重复!", 17, 0, (sockaddr *)&clientAddr, len);
            continue;
        }
        else
        {
            sendto(udpFd, "注册失败!", 14, 0, (sockaddr *)&clientAddr, len);
            return;
        }
    }

    // 设置非阻塞套接字
    // fcntl(clientFd, F_SETFL, O_NONBLOCK);

    // 将通信套接字加入到epoll实例中,下一轮就会被监测
    addEpoll(clientFd);
}

// 向用户发送所有连接用户的姓名
int printTable(void *arg, int len, char **col_val, char **col_name)
{
    clientInfo *client = (clientInfo *)arg;
    // cout << "用户名:" << col_val[3] << endl;
    sendto(client->fd, col_val[3], strlen(col_name[3]), 0, (sockaddr *)&client->addr, client->len);
    return 0;
}

// 向用户指定目标发送信息
int sendMsg(void *arg, int len, char **col_val, char **col_name)
{
    clientInfo *client = (clientInfo *)arg;
    // 找到目标ip和port
    sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(col_val[0]);
    addr.sin_port = htons(atoi(col_val[1]));
    cout << "sendMsg() ip:" << col_val[0] << " port:" << col_val[1] << " msg:" << client->msg << " name:" << col_val[3] << endl;
    sendto(client->fd, client->msg, strlen(client->msg), 0, (sockaddr *)&addr, addrlen);
    return 0;
}

// 客户端发起通信
void Server::communicate(int clientFd)
{
    char buf[1024]{};
    clientInfo clientAddr(udpFd);
    int ret = recvfrom(udpFd, buf, sizeof(buf), 0, (sockaddr *)&clientAddr.addr, &clientAddr.len);
    char buf2[1024]{};
    strcpy(buf2, buf);
    cout << "communicate() [" << inet_ntoa(clientAddr.addr.sin_addr) << ":" << ntohs(clientAddr.addr.sin_port) << "]:" << buf << endl; //********
    if (ret == -1)
    {
        perror("recvfrom()");
        return;
    }
    else if (strncmp(buf, "exit", 4) == 0) // 断开连接
    {
        strtok(buf, ":");
        char *destName = strtok(nullptr, ""); // 获取目标用户名
        cout << "要断开的用户:" << destName << endl;
        //*********
        // cout << "2[" << inet_ntoa(clientAddr.addr.sin_addr) << ":" << ntohs(clientAddr.addr.sin_port) << "]:断开连接" << endl;
        // 获取与该客户端连接的tcp套接字
        int connfd = getsqliteConnfd(destName);
        cout << "断开:删除已连接套接字 " << connfd << endl;
        // 从epoll实例删除tcp套接字
        epoll_ctl(epfd, EPOLL_CTL_DEL, connfd, nullptr);
        // 从数据库删除用户数据
        removeUser(destName);
        // 关闭套接字
        close(connfd);
        return;
    }
    else if (strcmp(buf, "all") == 0) // 向用户发送在线人员名单
    {
        sendto(udpFd, "begin", 6, 0, (sockaddr *)&clientAddr.addr, clientAddr.len);
        sqlite3_exec(db, "select * from user", printTable, &clientAddr, &error);
        sendto(udpFd, "end", 4, 0, (sockaddr *)&clientAddr.addr, clientAddr.len);
        return;
    }
    char *destName = strtok(buf, ":"); // 获取目标用户名
    char *msg = strtok(nullptr, "");   // 获取发送数据
    if (msg == NULL)
    {
        // sendto(udpFd, R"(请以"用户名:聊天信息"的格式发送)", 46, 0, (sockaddr *)&clientAddr.addr, clientAddr.len);
    }
    else
    {
        char temp[1064]{};
        char *srcname = getnameIp(inet_ntoa(clientAddr.addr.sin_addr), ntohs(clientAddr.addr.sin_port));
        sprintf(temp, "%s=>%s", srcname, buf2);
        clientAddr.msg = temp;
        char SQL[200]{};
        sprintf(SQL, "SELECT * FROM user WHERE name = '%s'", destName);
        // cout << "destname:" << destName << endl;
        // cout << "SQL:" << SQL << endl;
        sqlite3_exec(db, SQL, sendMsg, &clientAddr, &error);
        free(srcname);
    }
}

// 向客户端发送服务器文件列表
void showFile(int connfd)
{
    // 打开目录
    DIR *dp = opendir("./source");
    if (dp == NULL)
    {
        perror("opendir() failed");
        return;
    }

    // 读取目录项
    dirent *ep = nullptr;
    struct stat file;
    char information[512];
    char path[512];
    while (1) // 向客户端发送服务器的文件列表
    {
        memset(information, 0, sizeof(information));
        memset(&file, 0, sizeof(file));
        memset(path, 0, sizeof(path));

        ep = readdir(dp);
        if (ep == nullptr) // 读完
            break;
        // 排除隐藏文件
        if (strncmp(ep->d_name, ".", 1) == 0)
            continue;
        sprintf(path, "./source/%s", ep->d_name);
        if (stat(path, &file) < 0)
            perror("stat()");
        // 拼接文件名和大小
        sprintf(information, "%s  %ld字节\n", ep->d_name, file.st_size);
        send(connfd, information, strlen(information), 0); // 获取文件名
        usleep(30000);
    }
    sleep(1);
    send(connfd, "FINISH", 7, 0); // 发送结束标志
    closedir(dp);
}

int getName(void *arg, int len, char **col_val, char **col_name)
{
    char **name = static_cast<char **>(arg);
    *name = strdup(col_val[3]); // 使用strdup来复制字符串,
    return 1;
}

// 通过Tcp套接字获取用户名
char *Server::getnameByTcp(int connfd)
{
    char SQL[200]{};
    char *name = nullptr; // 初始化为 nullptr
    sprintf(SQL, "SELECT * FROM user WHERE connfd=%d", connfd);
    sqlite3_exec(db, SQL, getName, &name, &error);
    return name;
}

// 通过ip获取用户名
char *Server::getnameIp(char ip[16], unsigned short port)
{
    char SQL[200]{};
    char *name = nullptr; // 初始化为 nullptr
    sprintf(SQL, "SELECT * FROM user WHERE ip='%s' AND port=%d", ip, port);
    sqlite3_exec(db, SQL, getName, &name, &error);
    return name;
}

void beginLoad(char *filename, int connfd)
{
    // 打开目录
    DIR *dp = opendir("./source");
    if (dp == NULL)
    {
        perror("opendir() failed");
        return;
    }

    // 读取目录项
    dirent *ep = nullptr;
    while (1)
    {
        ep = readdir(dp);
        if (ep == nullptr) // 读完
            break;

        if (strcmp(ep->d_name, filename) == 0) // 找到下载的文件
        {
            char path[128]{};
            sprintf(path, "./source/%s", filename);
            FILE *fd = fopen(path, "r");
            if (fd == nullptr) // 向客户端发送错误信息
            {
                char *err_msg = strerror(errno);
                char err[1024]{};
                sprintf(err, "error fopen() %s", err_msg);
                send(connfd, err, strlen(err), 0);
                return;
            }

            char *buf = new char[65536];
            size_t num;
            while (1)
            {
                num = fread(buf, 1, 65536, fd);
                // cout << "num = " << num << endl;
                send(connfd, buf, num, 0);
                usleep(30000);

                if (num == 0 || feof(fd)) // 读完
                {
                    sleep(2);
                    send(connfd, "FINISH", 7, 0);
                    delete[] buf;
                    cout << "发送结束" << endl;
                    break;
                }
            }
            fclose(fd);
            closedir(dp);
            return;
        }
    }
    // 未找到文件
    send(connfd, "UNFIND", 7, 0);
    closedir(dp);
}

int sendOther(void *arg, int len, char **col_val, char **col_name)
{
    clientInfo *info = (clientInfo *)arg;
    // int *fd = (int *)arg;
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(col_val[0]);
    addr.sin_port = htons(atoi(col_val[1]));
    sendto(info->fd, info->msg, strlen(info->msg), 0, (sockaddr *)&addr, sizeof(addr));
    cout << "已通知对方:" << info->msg << endl;
    return 1;
}

void beginLoad(const char *filename, int connfd, int destFd) // 向其他人发送文件
{
}

// 客户端下载文件
void Server::loadFile(int &connfd)
{
    cout << "tid:" << pthread_self() << endl;
    char *msg = new char[1024];
    unique_ptr<char> p(msg);
    // 接收客户端信息
    int ret = recv(connfd, msg, sizeof(msg), 0);
    cout << "load() msg: " << msg << endl;
    cout << __LINE__ << endl;
    if (-1 == ret)
    {
        perror("loadFile recv()");
        return;
    }
    else if (0 == ret)
        return;

    if (strcmp(msg, "load") == 0) // 从服务器下载文件
    {
        cout << __LINE__ << endl;
        showFile(connfd); // 向用户展示服务器可下载文件列表
        char filename[128]{};
        recv(connfd, filename, sizeof(filename), 0); // 获取客户端想要下载的文件
        cout << "filename:" << filename << endl;
        cout << __LINE__ << endl;
        beginLoad(filename, connfd); // 开始下载
    }
    /*else if (strncmp(msg, "other:", 6) == 0) // 向其他用户传输文件
    {
        cout << "msg:" << msg << endl;
        char *destname = new char[32];
        char *filepath = new char[1024];
        // char *filename = new char[1024];
        unique_ptr<char> p1(destname), p2(filepath); //, p3(filename);
        strtok(msg, ":");                            // 过滤标志
        strcpy(destname, strtok(nullptr, ":"));      // 获取目标用户名
        strcpy(filepath, strtok(nullptr, ""));       // 获取文件路径
        char *filename = strrchr(filepath, '/');     // 获取文件名
        // cout << __LINE__ << " str:" << str << endl;
        // cout << __LINE__ << " str+1:" << str+1 << endl;
        if (filename == nullptr) // 没有'/'
            filename = filepath;
        else // 跳过'/'
            filename += 1;

        cout << __LINE__ << " filename: " << filename << endl;
        int destFd = getsqliteConnfd(destname);          // 获取对方的已连接套接字
        epoll_ctl(epfd, EPOLL_CTL_DEL, destFd, nullptr); // 暂时删除套接字
        char SQL[200]{};
        sprintf(SQL, "SELECT * FROM user WHERE name='%s'", destname);

        char *name = getnameByTcp(connfd); // 获取发送方姓名

        bzero(msg, sizeof(msg));
        cout << "name:" << name << " TO=>destname:" << destname << " 文件名:" << filename << endl;
        sprintf(msg, "other:用户[%s]发来文件:%s", name, filename);
        cout << __LINE__ << " msg: " << msg << endl;

        clientInfo info(udpFd);
        info.msg = msg;
        cout << __LINE__ << info.msg << endl;
        sqlite3_exec(db, SQL, sendOther, &info, &error); // 告知对方接收文件
        //*****添加不在线检查

        memset(msg, 0, sizeof(msg));
        recv(destFd, msg, sizeof(msg), 0);
        if (strcmp(msg, "yes") == 0)
        {
            send(connfd, "yes", 4, 0);
            beginLoad(filename, connfd, destFd); // 开始下载
        }
        else
        {
            send(connfd, "对方繁忙!", 29, 0);
        }

        free(name);
        addEpoll(destFd);
    }*/
    cout << __LINE__ << endl;
    addEpoll(connfd); // 添加到实例中
}

// 析构函数
Server::~Server()
{
    if (db != nullptr)
        sqlite3_close(db);
    if (error != nullptr)
        sqlite3_free(error);
    if (server != nullptr)
        delete server;
    // 关闭套接字
    close(listenFd);
    close(udpFd);
    cout << "析构函数()" << endl;
}