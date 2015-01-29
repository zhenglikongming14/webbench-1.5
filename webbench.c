/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h> //使用struct option结构体和getopt_long函数
#include <strings.h>
#include <time.h>
#include <signal.h>

/* values */
volatile int timerexpired=0;//根据命令行参数-t指定的测试时间判断是否超时
int speed=0;//子进程成功得到服务器响应的总数
int failed=0;//子进程请求失败总数
int bytes=0;//读取到的字节数
/* globals  */
//http协议的版本号
int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */ //HTTP协议版本定义
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5"
int method=METHOD_GET;//全局变量，定义HTTP请求方法GET 此外还支持OPTIONS、HEAD、TRACE方法，在main函数中用switch判断
int clients=1;//默认并发数为1，也就是子进程个数 可以由命令行参数-c指定
int force=0;//是否等待从服务器获取数去数据 0为获取
int force_reload=0;//是否使用cache  0为使用
int proxyport=80;//代理服务器端口号。默认为80
char *proxyhost=NULL;//代理服务器IP，默认为NULL
int benchtime=30;/*运行多久。默认为30s。可以通过-t指定执行时间，当子进程执行时间到过这个秒数之后，发送 SIGALRM 信号，将 timerexpired 设置为 1，让所有子进程退出*/
/* internal */
int mypipe[2];/*创建管道(半双工) 父子进程间通信，读取/写入数据，子进程完成任务后，向写端写入数据，主进程从读端读取数据*/
char host[MAXHOSTNAMELEN];//定义服务器IP
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];//HTTP请求信息

//struct option结构体，配合getopt_long函数使用
static const struct option long_options[]=
{
 {"force",no_argument,&force,1},
 {"reload",no_argument,&force_reload,1},
 {"time",required_argument,NULL,'t'},
 {"help",no_argument,NULL,'?'},
 {"http09",no_argument,NULL,'9'},
 {"http10",no_argument,NULL,'1'},
 {"http11",no_argument,NULL,'2'},
 {"get",no_argument,&method,METHOD_GET},
 {"head",no_argument,&method,METHOD_HEAD},
 {"options",no_argument,&method,METHOD_OPTIONS},
 {"trace",no_argument,&method,METHOD_TRACE},
 {"version",no_argument,NULL,'V'},
 {"proxy",required_argument,NULL,'p'},
 {"clients",required_argument,NULL,'c'},
 {NULL,0,NULL,0}//表示结束
};

/* prototypes */
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);
/*将 timerexpired 设置为1，让所有子进程退出*/
static void alarm_handler(int signal)
{
   timerexpired=1;
}	
/*程序的使用方法*/
static void usage(void)
{
   fprintf(stderr,
	"webbench [option]... URL\n"
	"  -f|--force               Don't wait for reply from server.\n"
	"  -r|--reload              Send reload request - Pragma: no-cache.\n"
	"  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	"  -p|--proxy <server:port> Use proxy server for request.\n"
	"  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	"  -9|--http09              Use HTTP/0.9 style requests.\n"
	"  -1|--http10              Use HTTP/1.0 protocol.\n"
	"  -2|--http11              Use HTTP/1.1 protocol.\n"
	"  --get                    Use GET request method.\n"
	"  --head                   Use HEAD request method.\n"
	"  --options                Use OPTIONS request method.\n"
	"  --trace                  Use TRACE request method.\n"
	"  -?|-h|--help             This information.\n"
	"  -V|--version             Display program version.\n"
	);
};
int main(int argc, char *argv[])
{
 int opt=0;
 int options_index=0;
 char *tmp=NULL;

 if(argc==1)
 {
	  usage();
          return 2;//为什么return 2；
 } 

 while((opt=getopt_long(argc,argv,"912Vfrt:p:c:?h",long_options,&options_index))!=EOF )///EOF可以改成-1
 {
  switch(opt)
  {
   case  0 : break;
   case 'f': force=1;break;
   case 'r': force_reload=1;break; 
   case '9': http10=0;break;
   case '1': http10=1;break;
   case '2': http10=2;break;
   case 'V': printf(PROGRAM_VERSION"\n");exit(0);//无break由于有宏定义#define PROGRAM_VERSION "1.5"所以可以这么用
   case 't': benchtime=atoi(optarg);break;//atoi (表示 alphanumeric to integer)是把字符串转换成长整型数的一个函数
   case 'p': 
	     /* proxy server parsing server:port */
	     tmp=strrchr(optarg,':');
          /*
           函数原型：char *strrchr(const char *str, char c);
           所属库： string.h
           函数功能：查找一个字符c在另一个字符串str中末次出现的位置（也就是从str的右侧开始查找字符c首次出现的位置），并返回从字符串中的这个位置起，一直到字符串结束的所有字符。如果未能找到指定字符，那么函数将返回NULL。

           */
	     proxyhost=optarg; //初始化代理服务器的地址    proxy n.代理
	     if(tmp==NULL)
	     {
		     break;
	     }
	     if(tmp==optarg)
	     {
		     fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
		     return 2;
	     }
	     if(tmp==optarg+strlen(optarg)-1)
	     {
		     fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
		     return 2;
	     }
	     *tmp='\0';
	     proxyport=atoi(tmp+1);break;//重新设定端口号
   case ':':
   case 'h':
   case '?': usage();return 2;break;
   case 'c': clients=atoi(optarg);break;//根据参数设置并发数
  }
 }//while循环执行结束
 
 if(optind==argc) {//optind为对应参数的下标位置
                      fprintf(stderr,"webbench: Missing URL!\n");
		      usage();
		      return 2;
                    }

 if(clients==0) clients=1;
 if(benchtime==0) benchtime=60;
 /* Copyright */
 fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION"\n"
	 "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	 );
 build_request(argv[optind]);
 /* print bench info */
 printf("\nBenchmarking: ");
 switch(method)
 {
	 case METHOD_GET:
	 default:
		 printf("GET");break;
	 case METHOD_OPTIONS:
		 printf("OPTIONS");break;
	 case METHOD_HEAD:
		 printf("HEAD");break;
	 case METHOD_TRACE:
		 printf("TRACE");break;
 }
 printf(" %s",argv[optind]);
 switch(http10)
 {
	 case 0: printf(" (using HTTP/0.9)");break;
	 case 2: printf(" (using HTTP/1.1)");break;
 }
 printf("\n");
 if(clients==1) printf("1 client");
 else
   printf("%d clients",clients);

 printf(", running %d sec", benchtime);
 if(force) printf(", early socket close");
 if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
 if(force_reload) printf(", forcing reload");
 printf(".\n");
 return bench();
}



/*
 void build_request(const char *url)是用来创建http连接请求的，生成 HTTP 头一般url的格式为protocol :// hostname[:port] / path / [;parameters][?query]#fragment通过这个函数将请求链接存到request中
 */
void build_request(const char *url)
{
  char tmp[10];
  int i;

  bzero(host,MAXHOSTNAMELEN);//置字节字符串host的前MAXHOSTNAMELEN个字节为\0且包括\0
  bzero(request,REQUEST_SIZE);//置字节字符串request的前REQUEST_SIZE个字节为\0且包括\0
    //bzero()所属文件库位string.h
    /*协议适配*/
  if(force_reload && proxyhost!=NULL && http10<1) http10=1;
  if(method==METHOD_HEAD && http10<1) http10=1;
  if(method==METHOD_OPTIONS && http10<2) http10=2;
  if(method==METHOD_TRACE && http10<2) http10=2;

  switch(method)
  {
	  default:
	  case METHOD_GET: strcpy(request,"GET");break;
	  case METHOD_HEAD: strcpy(request,"HEAD");break;
	  case METHOD_OPTIONS: strcpy(request,"OPTIONS");break;
	  case METHOD_TRACE: strcpy(request,"TRACE");break;
  }
		  
  strcat(request," ");//连接字符串

  if(NULL==strstr(url,"://"))//strstr()搜索一个字符串在另一个字符串中的第一次出现
  {
	  fprintf(stderr, "\n%s: is not a valid URL.\n",url);
	  exit(2);
  }
  if(strlen(url)>1500)
  {
         fprintf(stderr,"URL is too long.\n");
	 exit(2);
  }
  if(proxyhost==NULL)//代理服务器是否为空
	   if (0!=strncasecmp("http://",url,7))//strncasecmp()用来比较参数“http://”和url字符串前7个字符，比较时会自动忽略大小写的差异，字符串相同则返回0
	   {
           fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
             exit(2);
       }
  /* protocol/host delimiter */
  i=strstr(url,"://")-url+3;//i指向http://后第一个位置
  /* printf("%d\n",i); */

  if(strchr(url+i,'/')==NULL) {//查找字符串url+i中首次出现字符'/'的位置
      fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
      exit(2);
                              }
  if(proxyhost==NULL)
  {
   /* get port from hostname */
    if(index(url+i,':')!=NULL && index(url+i,':')<index(url+i,'/'))//判断url中是否指定了端口号
        {
        strncpy(host,url+i,strchr(url+i,':')-url-i);//取出主机地址
            /*
             char * strchr(char * str, int ch); 功能就是找出在字符串str中第一次出项字符ch的位置，找到就返回该字符位置的指针(也就是返回该字符在字符串中的地址的位置)，找不到就返回空指针(就是 null)。
             */
        bzero(tmp,10);
        strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
            /* printf("tmp=%s\n",tmp); */
                //从URL中提取端口号
        proxyport=atoi(tmp);//atoi函数将字符转换成整型
        if(proxyport==0) proxyport=80;
    }
    else{
        strncpy(host,url+i,strcspn(url+i,"/"));
    }
    // printf("Host=%s\n",host);
    strcat(request+strlen(request),url+i+strcspn(url+i,"/"));//strcspn返回int类型
  }
  else{
   // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
      strcat(request,url);///?
  }
  if(http10==1)
	  strcat(request," HTTP/1.0");
  else if (http10==2)
	  strcat(request," HTTP/1.1");
  strcat(request,"\r\n");
  if(http10>0)
	  strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
  if(proxyhost==NULL && http10>0)
  {
	  strcat(request,"Host: ");
	  strcat(request,host);
	  strcat(request,"\r\n");
  }
  if(force_reload && proxyhost!=NULL)
  {
	  strcat(request,"Pragma: no-cache\r\n");
  }
  if(http10>1)
	  strcat(request,"Connection: close\r\n");
  /* add empty line at end */
  if(http10>0) strcat(request,"\r\n"); 
  // printf("Req=%s\n",request);
}


/* vraci system rc error kod */
static int bench(void)
{
  int i,j,k;	
  pid_t pid=0;//进程标示符
  FILE *f;

  /* check avaibility of target server */
    /* 测试远程主机是否能够连通 */
  i=Socket(proxyhost==NULL?host:proxyhost,proxyport);
  if(i<0) { 
	   fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
           return 1;
         }
  close(i);//关闭
  /* create pipe */
  if(pipe(mypipe))//前面已经声明int mypipe[2];
  {
	  perror("pipe failed.");
	  return 3;
  }

  /* not needed, since we have alarm() in childrens */
  /* wait 4 next system clock tick */
  /*
  cas=time(NULL);
  while(time(NULL)==cas)
        sched_yield();
  */

  /* fork childs */
  for(i=0;i<clients;i++)
  {
	   pid=fork();
	   if(pid <= (pid_t) 0)
	   {
		   /* child process or error*/
	           sleep(1); /* make childs faster */
           break;/* 这个 break 很重要，它主要让子进程只能从父进程生成，
                  否则子进程会再生成子进程，子子孙孙很庞大的 */
	   }
  }

  if( pid< (pid_t) 0)//类型转换(pid_t) 0
  {
          fprintf(stderr,"problems forking worker no. %d\n",i);
	  perror("fork failed.");
	  return 3;
  }

  if(pid== (pid_t) 0)
  {
    /* I am a child */
    /* 子进程执行请求：尽可能多的发送请求，直到超时返回为止 */
    if(proxyhost==NULL)
      benchcore(host,proxyport,request);
         else
      benchcore(proxyhost,proxyport,request);

      /* write results to pipe */
	 f=fdopen(mypipe[1],"w");
	 if(f==NULL)
	 {
		 perror("open pipe for writing failed.");
		 return 3;
	 }
	 /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
      /* 将子进程执行任务的结果写入到管道中，以便父进程读取 */
	 fprintf(f,"%d %d %d\n",speed,failed,bytes);
      /* 子进程完成任务，返回退出 */
	 fclose(f);
	 return 0;
  } else
  {
      /* 父进程读取管道，打印结果 */
	  f=fdopen(mypipe[0],"r");
	  if(f==NULL) 
	  {
		  perror("open pipe for reading failed.");
		  return 3;
	  }
	  setvbuf(f,NULL,_IONBF,0);//设置文件缓冲区
	  speed=0;
      failed=0;
      bytes=0;

	  while(1)
	  {
		  pid=fscanf(f,"%d %d %d",&i,&j,&k);
		  if(pid<2)
                  {
                       fprintf(stderr,"Some of our childrens died.\n");
                       break;
                  }
		  speed+=i;
		  failed+=j;
		  bytes+=k;
		  /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
		  if(--clients==0) break;
	  }
	  fclose(f);

  printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
		  (int)((speed+failed)/(benchtime/60.0f)),
		  (int)(bytes/(float)benchtime),
		  speed,
		  failed);
  }
  return i;
}



/*对http请求进行测试。*/
void benchcore(const char *host,const int port,const char *req)
{
 int rlen;
 char buf[1500];
 int s,i;
 struct sigaction sa;
/*
 union __sigaction_u {
	void    (*__sa_handler)(int);
	void    (*__sa_sigaction)(int, struct __siginfo *,
 void *);
    struct	sigaction {
        union __sigaction_u __sigaction_u;  //signal handler
        sigset_t sa_mask;		// signal mask to apply
        int	sa_flags;		// see signal options below
    };
 };
 */

 /* setup alarm signal handler */
 sa.sa_handler=alarm_handler;//alarm_handler为一个函数
 sa.sa_flags=0;
 if(sigaction(SIGALRM,&sa,NULL))//sigaction()函数返回0表示成功，-1表示失败
    exit(3);
    /* 这个是关键，当程序执行到指定的秒数之后，发送 SIGALRM 信号 */
 alarm(benchtime);

 rlen=strlen(req);
    /* 无限执行请求，直到接收到 SIGALRM 信号将 timerexpired 设置为 1 时 */
 nexttry://配合goto语句使用
    while(1)
 {
    if(timerexpired)
    {
       if(failed>0)
       {
          /* fprintf(stderr,"Correcting failed by signal\n"); */
          failed--;
       }
       return;
    }
     /* 连接远程服务器 */
    s=Socket(host,port);                          
    if(s<0) { failed++;continue;}
     /* 发送请求 */
    if(rlen!=write(s,req,rlen)) {failed++;close(s);continue;}
     /* 如果是 http/0.9 则关闭 socket 的写操作 */
    if(http10==0) 
	    if(shutdown(s,1)) { failed++;close(s);continue;}
     /* 如果等待响应数据返回，则读取响应数据，计算传输的字节数 */
    if(force==0) 
    {
            /* read all available data from socket */
	    while(1)
	    {
              if(timerexpired) break; 
            i=read(s,buf,1500);
              /* fprintf(stderr,"%d\n",i); */
	      if(i<0) {
                 failed++;
                 close(s);
                 goto nexttry;
          }
          else
		       if(i==0) break;
		       else
			       bytes+=i;
	    }
    }
     /* 关闭连接 */
     if (close(s)) {
         failed++;
         continue;
     }
     /* 成功完成一次请求，并计数，继续下一次相同的请求，直到超时为止 */
     speed++;
 }
}