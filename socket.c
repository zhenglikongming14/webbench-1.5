/* $Id: socket.c 1.1 1995/01/01 07:11:14 cthuang Exp $
 *
 * This module has been modified by Radim Kolar for OS/2 emx
 */

/***********************************************************************
  module:       socket.c   模块，组件
  program:      popclient
  SCCS ID:      @(#)socket.c    1.5  4/1/94
  programmer:   Virginia Tech Computing Center
  compiler:     DEC RISC C compiler (Ultrix 4.1)
  environment:  DEC Ultrix 4.3 
  description:  UNIX sockets code.
 ***********************************************************************/
 
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>    //使用struct sockaddr_in
#include <arpa/inet.h>      //使用struct in_addr
#include <netdb.h>
#include <sys/time.h>
#include <string.h>    //使用strrchr()函数
#include <unistd.h>    //使用fork()函数
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
/***********
 功能：通过地址和端口建立网络连接
 @host:网络地址
 @clientPort:端口
 Return：建立的socket连接。
 如果返回-1，表示建立连接失败
 ************/

int Socket(const char *host, int clientPort)
{
    int sock;
    unsigned long inaddr;
    struct sockaddr_in ad;
    /*
     struct sockaddr_in {
        short int sin_family; // 地址族
        unsigned short int sin_port; // 端口号一般需要使用htons()将主机字节序转换成为网络字节序
        struct in_addr sin_addr; // Internet地址一般需要使用inet_pton()或者inet_addr()函数将字符串形式的IP地址转换为按网络字节顺序的整型值
        unsigned char sin_zero[8]; //保证与struct sockaddr一样的长度 //
     };

     struct in_addr就是32位IP地址。
    struct in_addr {
        unsigned long s_addr;
    };
     */
    struct hostent *hp;
    /*
     struct hostent{
        char h_name;     //主机的官方名
        char **h_aliases;   //主机的别名清单
        char h_addrtype;     //主机的地址类型，例如返回AF_INET,AF_INET6
        char h_length;         //地址长度
        char **h_addr_list;      // 地址清单 --------注意很多地方，引用hostent->h_addr, 其中h_addr=h_addr_list[0]
     };
     */
    
    memset(&ad, 0, sizeof(ad));//向内存空间填入内容
    ad.sin_family = AF_INET;//AF_INET 表示IPv4网络协议

    inaddr = inet_addr(host);
    /*inet_addr()的功能是将一个点分十进制的IPv4转换成一个长整数型数（u_long类型）或者理解为将字符串形式的IP地址转换为按网络字节顺序的整型值  例如：unsigned long IP = ntohl(inet_addr(192.168.0.77));*/
    if (inaddr != INADDR_NONE)
        memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
    else
    {
        hp = gethostbyname(host);
        /*
         gethostbyname()是用来解析主机名和地址的。可能会使用DNS服务或者本地主机上的其他解析机制（例如查询/etc/hosts）。返回一个指向 struct hostent的指针，这个结构体描述一个IP主机。函数使用如下参数：
         
         name 指定主机名。例如 www.wikipedia.org
         addr 指向 struct in_addr的指针，包含主机的地址。
         len 给出 addr的长度，以字节为单位。
         type 指定地址族类型 （比如 AF_INET）。
         出错返回NULL指针，可以通过检查 h_errno 来确定是临时错误还是未知主机。正确则返回一个有效的 struct hostent *。
         */
        if (hp == NULL)
            return -1;
        memcpy(&ad.sin_addr, hp->h_addr, hp->h_length);
    }
    ad.sin_port = htons(clientPort);//将主机字节序转换成为网络字节序
    /*调用socket创建一个未命名套接字，将服务器的命名套接字作为一个地址来调用connect与服务器建立连接*/
    sock = socket(AF_INET, SOCK_STREAM, 0);//根据选定的domain和type选择使用缺省协议
    if (sock < 0)
        return sock;
    if (connect(sock, (struct sockaddr *)&ad, sizeof(ad)) < 0)
        return -1;
    return sock;
}

