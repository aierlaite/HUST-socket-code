#include<stdio.h>
#include<string.h>
#include<time.h>
#include<conio.h>
#include<Windows.h>
#pragma comment(lib,"Ws2_32.lib")

#define RRQ 1
#define WRQ 2
#define DATA 3
#define ACK 4
#define ERROR_CODE 5
#define DATA_SIZE 512
#define TIME_OUT 2
#define MAX_RETRANSMISSION 100

FILE* log_file;
time_t nowtime;
clock_t start, end;

void download(int mode, const char* filename, char* buffer, SOCKET sock, sockaddr_in addr, int addrlen);
int read_request(int mode, const char* filename, char* buffer, SOCKET sock, sockaddr_in addr, int addrlen);
int receive_data(char* recv_buffer, SOCKET sock, sockaddr_in& addr, int addrlen);
int write_request(int mode, const char* filename, char* buffer, SOCKET serversock, sockaddr_in addr, int addrLen);
int receiveACK(char* buffer, SOCKET serversock, sockaddr_in &addr, int addrlen);
void upload(int mode, const char* filename, char* buffer, SOCKET serversock, sockaddr_in addr, int addrlen);
int send_data(SOCKET serversock, sockaddr_in addr, int addrlen, FILE* fp, char* buffer, char* data, int data_size, unsigned short block_num);
int send_ACK(SOCKET sock, sockaddr_in addr, int addrlen, FILE* fp, char* buffer, char* data, int data_size, unsigned short block_num);
void ending2(int recv_bytes,clock_t end,clock_t start);
void ending1(int recv_bytes,clock_t end,clock_t start);




void ending1(int recv_bytes,clock_t end,clock_t start)
{
	int result;
    printf("接收完毕 传输大小:%dbytes speed:%.3fkb/s", recv_bytes, recv_bytes / (1024 * (double)(end - start) / CLK_TCK));
    printf("\n按任意键继续...");
    result = getch();
    return;
}

void ending2(int send_bytes,clock_t end,clock_t start)
{
	int result;
    printf("传输完毕 传输大小:%dbytes speed:%.3fkb/s", send_bytes, send_bytes / (1024 * ((double)(end - start) / CLK_TCK)));
	printf("\n按任意键继续...");
	result = getch();
	return;
}

/**
 * @brief 从服务器下载文件
 * 
 * @param mode 传输模式，1表示文本模式，2表示二进制模式
 * @param filename 文件名
 * @param buffer 缓冲区
 * @param sock 套接字
 * @param addr 服务器地址结构
 * @param addrlen 地址结构长度
 */
void download(int mode, const char* filename, char* buffer, SOCKET sock, sockaddr_in addr, int addrlen) {
    int recv_bytes = 0, max_send = 0, result, data_size;
    sockaddr_in serveraddr = { 0 };
    long long int block_num = 0;
    char data[DATA_SIZE];
    char recv_buffer[DATA_SIZE*2];
    BOOL end_flag = FALSE;
    BOOL start_flag = TRUE;
    FILE* fp;
    // 根据传输模式打开文件
    if (mode == 1)
        fp = fopen(filename, "w");
    if (mode == 2)
        fp = fopen(filename, "wb");
    // 判断文件打开是否成功
    if (fp == NULL) {
        printf("文件不存在或者文件无法打开\n按任意键继续...");
        result = getch();
        return;
    }
    // 发送读取请求
    read_request(mode, filename, buffer, sock, addr, addrlen);
    while (1) {
        // 开始计时
        if (start_flag) {
            start = clock();
            start_flag = FALSE;
        }
        // 判断是否传输结束
        if (end_flag) {
            ending1(recv_bytes, end, start);
            fclose(fp);
            return;
        }
        // 接收数据包
        result = receive_data(recv_buffer, sock, serveraddr, addrlen);
        // 判断接收结果
        if (result > 0) {
            max_send = 0; // 重置重传次数
            // 判断序号是否连续
            if (block_num != ((recv_buffer[2] << 8) + recv_buffer[3] - 1))
                result = -1;
        }
        // 处理接收到的数据包
        if (result > 0) {
            recv_bytes += result - 4; // 记录传输数据大小
            max_send = 0; // 重置重传次数
            block_num++;
            data_size = fwrite(recv_buffer + 4, 1, result - 4, fp);
            if (data_size < 512) {
                // 传输结束
                end_flag = TRUE;
                end = clock();
            }
            // 发送 ACK 包
            result = send_ACK(sock, serveraddr, addrlen, fp, buffer, data, data_size, block_num);
        }
        // 处理超时或发送失败
        else if (result == -1) {
            max_send++; // 重传次数加一
            printf("...第%d个ACK包重传中...%d\n",block_num, max_send);
            if (max_send > MAX_RETRANSMISSION) {
                printf("重传次数过多\n按任意键继续...");
                result = getch();
                fprintf(log_file, "错误:重传次数过多 %s", asctime(localtime(&(nowtime = time(NULL))))); 
                return ;
            }
            // 重传 ACK 包
            if (block_num > 0) {
                fprintf(log_file, "重传ACK包 ACK序号:%d %s", block_num, asctime(localtime(&(nowtime = time(NULL)))));
                send_ACK(sock, serveraddr, addrlen, fp, buffer, data, data_size, block_num);
            }
            // 重传读取请求
            else {
                fprintf(log_file, "重传RRQ请求 %s", asctime(localtime(&(nowtime = time(NULL)))));
                read_request(mode, filename, buffer, sock, addr, addrlen);
            }
        }
        // 处理接收到的错误包
        else {
            printf("ERROR!错误码:%d %s", recv_buffer[3], recv_buffer + 4);
            printf("\n按任意键继续...");
            result = getch();
            return;
        }
    }
}

/**
 * @brief 发送读取请求到服务器
 * 
 * @param mode 传输模式，1表示文本模式，2表示二进制模式
 * @param filename 文件名
 * @param buffer 缓冲区
 * @param sock 套接字
 * @param addr 服务器地址结构
 * @param addrlen 地址结构长度
 * @return int 返回发送结果，SOCKET_ERROR表示发送失败，其他表示发送的字节数
 */
int read_request(int mode, const char* filename, char* buffer, SOCKET sock, sockaddr_in addr, int addrlen) {
    int send_size = 0;
    int result;
    memset(buffer, 0, sizeof(buffer));  // 清空缓冲区
    if (mode == 1) {
        send_size = sprintf(buffer,"%c%c%s%c%s%c",0,1,filename,0,"netascii",0);  // 生成文本模式的读取请求内容
    }
    else {
        send_size = sprintf(buffer,"%c%c%s%c%s%c",0,1,filename,0,"octet",0);  // 生成二进制模式的读取请求内容
    }
    result = sendto(sock, buffer, send_size, 0, (struct sockaddr*)&addr, addrlen);  // 发送请求到服务器
    if (result == SOCKET_ERROR) {
        printf("发送RRQ请求失败\n按任意键继续...");
        result = getch();
        // 记录发送请求失败的错误信息
        fprintf(log_file, "错误:发送RRQ请求失败	错误码:%d	%s", WSAGetLastError(), asctime(localtime(&(nowtime = time(NULL)))));
    }
    else {
        // 记录发送请求成功的信息
        fprintf(log_file, "发送RRQ请求成功	send%dbytes	文件名:%s	%s", result, filename, asctime(localtime(&(nowtime = time(NULL)))));
    }
    return result;
}


/**
 * 接收数据
 * @param recv_buffer 接收缓冲区
 * @param sock 套接字
 * @param addr 地址
 * @param addrlen 地址长度
 * @return 接收数据结果
 */
int receive_data(char* recv_buffer, SOCKET sock, sockaddr_in& addr, int addrlen) {
    memset(recv_buffer, 0, sizeof(recv_buffer));
    struct timeval tv;
    fd_set readfds;
    int result;
    int wait_time;
    for (wait_time = 0; wait_time < TIME_OUT; wait_time++) 
    {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        select(sock + 1, &readfds, NULL, NULL, &tv);
        result = recvfrom(sock, recv_buffer, DATA_SIZE*2, 0, (struct sockaddr*)&addr, (int*)&addrlen);
        if (result > 0 && result < 4) {
            printf("bad packet\n按任意键继续...");
            result = getch();
            fprintf(log_file, "错误:接收包不正确	%s", asctime(localtime(&(nowtime = time(NULL)))));
            return 0;
        }
        else if (result >= 4) {
            if (recv_buffer[1] == ERROR_CODE) {
                fprintf(log_file, "错误:接收到错误包	错误码:%d	错误信息%s	%s", recv_buffer[3], recv_buffer + 4, asctime(localtime(&(nowtime = time(NULL)))));
                return -2;
            }
            fprintf(log_file, "接收数据成功	receive%dbytes	数据包序号:%d	%s", result, recv_buffer[3] + (recv_buffer[2] >> 8), asctime(localtime(&(nowtime = time(NULL)))));
            return result;
        }
    }
    if (wait_time >= TIME_OUT) {
        fprintf(log_file, "错误:等待接收超时		%s", asctime(localtime(&(nowtime = time(NULL)))));
        return -1;
    }
}
/**
 * 发送ACK包
 * @param sock 套接字
 * @param addr 地址
 * @param addrlen 地址长度
 * @param fp 文件指针
 * @param buffer 缓冲区
 * @param data 数据
 * @param data_size 数据大小
 * @param block_num 数据块编号
 * @return 发送结果
 */
int send_ACK(SOCKET sock, sockaddr_in addr, int addrlen, FILE* fp, char* buffer, char* data, int data_size, unsigned short block_num) {
    int result;
    int send_size = 0;
    memset(buffer, 0, sizeof(buffer));
    buffer[++send_size] = ACK;
    buffer[++send_size] = (char)(block_num >> 8);
    buffer[++send_size] = (char)block_num;
    result = sendto(sock, buffer, 4, 0, (struct sockaddr*)&addr, addrlen);
    if (result == SOCKET_ERROR) {
        // 发送ACK失败处理
        printf("发送ACK失败\n按任意键继续...");
        result = getch();
        fprintf(log_file, "错误:ACK包发送失败	ACK序号:%d	错误码:%d	%s", block_num, WSAGetLastError(), asctime(localtime(&(nowtime = time(NULL)))));
        return -1;
    }
    else {
        fprintf(log_file, "ACK包发送成功	send%dbytes	ACK包序号:%d	%s", result, block_num, asctime(localtime(&(nowtime = time(NULL)))));
        return result;
    }
}


/**
 * 上传文件
 * @param mode 模式（1 - netascii, 2 - octet）
 * @param filename 文件名
 * @param buffer 缓冲区
 * @param sock 套接字
 * @param addr 地址
 * @param addrlen 地址长度
 */
void upload(int mode, const char* filename, char* buffer, SOCKET sock, sockaddr_in addr, int addrlen) {
    int send_bytes = 0, result;
    sockaddr_in serveraddr = { 0 };
    int max_send = 0;
    int data_size;
    long long int block_num = 0;
    char data[DATA_SIZE];
    char recv_buffer[DATA_SIZE * 2];
    BOOL end_flag = FALSE;
    BOOL start_flag = TRUE;
    FILE* fp;

    if (mode == 1)
        fp = fopen(filename, "r");
    if (mode == 2)
        fp = fopen(filename, "rb");

    if (fp == NULL) {
        // 打开文件失败处理
        printf("打开文件失败\n按任意键继续...");
        result = getch();
        return;
    }

    // 发送写请求
    write_request(mode, filename, buffer, sock, addr, addrlen);

    while (1) {
        result = receiveACK(recv_buffer, sock, serveraddr, addrlen);
        if (result != block_num)
            result = -1;
        if (result >= 0) {
            max_send = 0;
            if (end_flag) {
                ending2(send_bytes, end, start);
                fclose(fp);
                return;
            }
            block_num = result;
            memset(data, 0, DATA_SIZE);
            data_size = fread(data, 1, DATA_SIZE, fp);
            fprintf(log_file, "从%s文件读取了	size:%dbytes			%s", filename, data_size, asctime(localtime(&(nowtime = time(NULL)))));
            if (start_flag == 1) {
                start = clock();
                start_flag = FALSE;
            }
            result = send_data(sock, serveraddr, addrlen, fp, buffer, data, data_size, ++block_num);
            send_bytes += data_size;
            if (data_size < 512 && result != -1) 
            {
                end = clock();
                end_flag = TRUE;
            }
        }
        else if ((result == -1) || (result == -2))
        {
            max_send++;
            printf("...第%d个数据包正在重传中...%d\n", block_num, max_send);
            if (max_send > MAX_RETRANSMISSION) {
                // 重传次数超过限制处理
                printf("重传次数超过设置的限制\n按任意键继续...");
                result = getch();
                fprintf(log_file, "错误:重传次数超过限制	%s", asctime(localtime(&(nowtime = time(NULL)))));
                return;
            }
            if (block_num > 0) {
                fprintf(log_file, "重传数据包	数据包序号:%d	%s", block_num, asctime(localtime(&(nowtime = time(NULL)))));
                send_data(sock, serveraddr, addrlen, fp, buffer, data, data_size, block_num);
            }
            else {
                fprintf(log_file, "重传WRQ请求	%s", asctime(localtime(&(nowtime = time(NULL)))));
                write_request(mode, filename, buffer, sock, addr, addrlen);
            }
        }
        else if (result == -3) {
            // 错误信息处理
            printf("ERROR!错误代码:%d %s \n按任意键继续...", recv_buffer[3], recv_buffer + 4);
            result = getch();
            return;
        }
    }
}

/**
 * 发送写请求
 * @param mode 模式（1 - netascii, 0 - octet）
 * @param filename 文件名
 * @param buffer 缓冲区
 * @param sock 套接字
 * @param addr 地址
 * @param addrlen 地址长度
 * @return 发送结果
 */
int write_request(int mode, const char* filename, char* buffer, SOCKET sock, sockaddr_in addr, int addrlen) {
    int send_size = 0;
    int result;
    memset(buffer, 0, sizeof(buffer));
    if (mode == 1) {
        // netascii 模式
        send_size = sprintf(buffer, "%c%c%s%c%s%c", 0, 2, filename, 0, "netascii", 0); 
    } else {
        // octet 模式
        send_size = sprintf(buffer, "%c%c%s%c%s%c", 0, 2, filename, 0, "octet", 0); 
    }
    result = sendto(sock, buffer, send_size, 0, (struct sockaddr*)&addr, addrlen);
    if (result == SOCKET_ERROR) {
        // 发送失败处理
        printf("发送WRQ请求失败\n按任意键继续...");
        result = getch();
        fprintf(log_file, "错误:发送WRQ请求失败	错误码:%d	%s", WSAGetLastError(), asctime(localtime(&(nowtime = time(NULL)))));
    } else {
        // 发送成功处理
        fprintf(log_file, "发送WRQ请求成功	send%dbytes	文件名:%s	%s", result, filename, asctime(localtime(&(nowtime = time(NULL)))));
    }
    return result;
}

/**
 * 接收 ACK 数据包
 * @param recv_buffer 接收缓冲区
 * @param sock 套接字
 * @param addr 地址
 * @param addrlen 地址长度
 * @return 接收结果
 */
int receiveACK(char* recv_buffer, SOCKET sock, sockaddr_in& addr, int addrlen) {
    memset(recv_buffer, 0, sizeof(recv_buffer));
    struct timeval tv;
    fd_set readfds;
    int result;
    int wait_time;
    for (wait_time = 0; wait_time < TIME_OUT; wait_time++) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        select(sock + 1, &readfds, NULL, NULL, &tv);
        result = recvfrom(sock, recv_buffer, 4, 0, (struct sockaddr*)&addr, (int*)&addrlen);
        if (result > 0 && result < 4) {
            // 不完整数据包
            printf("bad packet\n按任意键继续...");
            result = getch();
            fprintf(log_file, "错误:接收包不正确	%s", asctime(localtime(&(nowtime = time(NULL)))));
            return -2;
        }
        else if (result >= 4) {
            // 接收到完整数据包
            if (recv_buffer[1] == ERROR_CODE) {
                // 错误包处理
                fprintf(log_file, "错误:接收到错误包	错误码:%d	错误信息%s	%s", recv_buffer[3], recv_buffer + 4, asctime(localtime(&(nowtime = time(NULL)))));
                return -3;
            }
            // 接收成功处理
            fprintf(log_file, "接收ACK包成功	receive%dbytes	ACK序号:%d	%s", result, recv_buffer[3] + (recv_buffer[2] << 8), asctime(localtime(&(nowtime = time(NULL)))));
            return recv_buffer[3] + (recv_buffer[2] << 8); // 返回 ACK 序号
        }
    }
    if (wait_time >= TIME_OUT) {
        // 接收超时
        fprintf(log_file, "错误:等待接收超时		%s", asctime(localtime(&(nowtime = time(NULL)))));
        return -1;
    }
}

/**
 * 发送数据至指定套接字
 * @param sock 套接字
 * @param addr 目标地址
 * @param addrlen 地址长度
 * @param fp 文件指针
 * @param buffer 缓冲区
 * @param data 数据
 * @param data_size 数据大小
 * @param block_num 数据块编号
 * @return 发送结果
 */
int send_data(SOCKET sock, sockaddr_in addr, int addrlen, FILE* fp, char* buffer, char* data, int data_size, unsigned short block_num) {
    int result, send_size = 0;
    memset(buffer, 0, sizeof(buffer));
    buffer[++send_size] = DATA; // 设置数据标识
    buffer[++send_size] = (char)(block_num >> 8); // 数据块编号高 8 位
    buffer[++send_size] = (char)block_num; // 数据块编号低 8 位
    ++send_size;
    memcpy(buffer + send_size, data, data_size); // 复制数据至缓冲区
    send_size += data_size;
    buffer[send_size] = 0;
    // 发送数据
    result = sendto(sock, buffer, send_size, 0, (struct sockaddr*)&addr, addrlen);
    if (result == SOCKET_ERROR) {
        // 发送失败处理
        printf("发送数据失败\n按任意键继续...");
        result = getch();
        fprintf(log_file, "错误:发送数据失败	数据包序号:%d 错误码:%d	%s", block_num, WSAGetLastError(), asctime(localtime(&(nowtime = time(NULL)))));
        return -1;
    }
    else {
        // 发送成功处理
        fprintf(log_file, "发送数据包成功	send%dbytes	数据包序号:%d	%s", result, block_num, asctime(localtime(&(nowtime = time(NULL)))));
        return result;
    }
}

void unbind(SOCKET socket) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = 0;  
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(socket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) 
	{printf("Socket unbound successfully.\n");
    } else {
        printf("Socket unbound successfully.\n");
    }
}

int main() {
	//初始化日志文件
	log_file = fopen("log.txt", "w+");
	char filename[128],buffer[DATA_SIZE*2];//文件名
	int Result;//保存返回值

	//启动Winsocket
	WSADATA wsaData;
	Result = WSAStartup(0x0202, &wsaData);//WSAS的版本号 
	if (Result!=0)
	{
		printf("WSAStartup failed with error: %d", Result);
		fprintf(log_file, "错误：无法启动Winsocket	错误码:%d	%s", WSAGetLastError(), asctime(localtime(&(nowtime = time(NULL)))));
		return 0;
	}
	else{
		fprintf(log_file, "已经成功启动Winsocket	%s", asctime(localtime(&(nowtime = time(NULL)))));
	} 
	

	//创建套接字
	iny:
	SOCKET client_sock;
	client_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (client_sock == INVALID_SOCKET) {
		printf("创建套接字失败！\n");
		fprintf(log_file, "错误:无法创建套接字	错误码:%d	%s", WSAGetLastError(), asctime(localtime(&(nowtime = time(NULL)))));
		return 0;
	}
	else
	{
		fprintf(log_file, "已经创建套接字	%s", asctime(localtime(&(nowtime = time(NULL)))));
	} 
	

	//服务端 ip和端口 客户端ip和端口
	struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;
	int clientport = 0;
	int serverport = 69;
	char serverip[20];
	char clientip[20]="127.0.0.1";
	printf("请输入服务端地址:");
	scanf("%s", serverip);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(serverport);
	server_addr.sin_addr.S_un.S_addr = inet_addr(serverip);
	client_addr.sin_family = AF_INET;
	client_addr.sin_port = htons(clientport);
	client_addr.sin_addr.S_un.S_addr = inet_addr(clientip);


	unsigned long Opt = 1;
	Result = ioctlsocket(client_sock, FIONBIO, &Opt);
	if (Result == SOCKET_ERROR) {
		printf("设置非阻塞模式失败\n");
		fprintf(log_file, "ERROR:无法设置非阻塞模式	错误码:%d	%s", WSAGetLastError(), asctime(localtime(&(nowtime = time(NULL)))));
		return 0;
	}
	Result = bind(client_sock, (LPSOCKADDR)&client_addr, sizeof(client_addr));
	if (Result == SOCKET_ERROR)
	{
		// 绑定失败
		printf("绑定接口时发生错误!\n按任意键继续...");
		Result = getch();
		fprintf(log_file, "ERROR:无法绑定接口	错误码:%d	%s", WSAGetLastError(), asctime(localtime(&(nowtime = time(NULL)))));
		return 0;
	}

	//主界面
	char choice;
	char mode;
	inx: 
	while (1) {
		system("cls");
		printf("目前发送的服务器地址为%s\n",serverip);
		printf("+------------------------------+\n");
		printf("|0.关闭TFTP客户端   1.上传文件 |\n");
		printf("|2.下载文件  3.返回输出ip地址  | \n");
		printf("+------------------------------+\n");
		choice = getch();
		if (choice == '1') {
			printf("目前模式为：上传文件\n请输入文件名:\n");
			scanf("%s", filename);
			printf("目前状态为：上传文件 %s \n",filename);
			printf("1.netascii\n2.octet\n请选择传输模式:\n");
			mode = getch();
			printf("...上传中...\n");
			upload(mode-48, filename, buffer, client_sock, server_addr, sizeof(sockaddr_in));
		}
		if (choice == '2') {
			printf("目前模式为：下载文件\n请输入文件名\n");
			scanf("%s", filename);
			printf("目前状态为：下载文件 %s \n",filename);
			printf("1.netascii\n2.octet\n请选择传输模式:\n");
			mode = getch();
			printf("...下载中...\n");
			download(mode-48, filename, buffer, client_sock, server_addr, sizeof(sockaddr_in));
		}
	if (choice == '0')
	{
		printf("确定退出吗？（y/n）\n") ;
		char mode1;
			while(1)
			{
				mode1=getch();
				if(mode1=='y')
				{
				printf("+------------------------------+\n");
				printf("|      欢迎您下次进行使用      |\n");
				printf("|     华中科技大学网安学院     |\n");
				printf("+------------------------------+\n");
				return 0;
				}
				else if(mode1=='n')
				{
					goto inx; 
				}
			}
			fclose(log_file);
	}
	if(choice == '3') 
	{
		system("cls"); 
		unbind(client_sock);
		closesocket(client_sock);
		goto iny;
	}
}
}




