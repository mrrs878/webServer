#ifndef _TASK_H
#define _TASK_H

#include <iostream>
#include <string>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std;

const string path = "/root/webserver";
const int BUFFER_SIZE = 4096;

class task {
private:
	int conn_fd;
	
public:
	task(void) {}
	task(int _connfd) : conn_fd(_connfd) {}
	~task(void) {}
	
	void doit(void);
	void response(char *_msg, int _stat);  //response other method
	void responseFile(int _size, int _stat);
	void responseGET(char *_file_name);
	void responsePOST(char *_file_name, char *_argc);
};

void task::response(char *_msg, int _stat) {
	char buf[512];
	
	sprintf(buf, "HTTP/1.1 %d OK\r\nConnection: Close\r\n"
				 "content-length:%d\r\n\r\n", _stat, strlen(_msg));

	sprintf(buf, "%s%s", buf, _msg);
	write(conn_fd, buf, strlen(buf));
}

void task::responseFile(int _size, int _stat) {
	char buf[128];
	
	sprintf(buf, "HTTP/1.1 %d OK\r\nConnection: Close\r\n"
				 "content-length:%d\r\n\r\n", _stat, _size);
	write(conn_fd, buf, strlen(buf));
}
	
void task::doit(void) {
	char recv_buf[BUFFER_SIZE];
	int size = 0;
	
	//cout << "doit\n";
	while(1) {
		size = read(conn_fd, recv_buf, BUFFER_SIZE);
		if(size > 0) {
			char method[5];
			char file_name[20];
			int i=0, j=0;
			
			//printf("%s\n", recv_buf);
			//  GET / HTTP/1.1
			//  Host: 192.168.12.128:8086
			//  User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:62.0) Gecko/20100101 Firefox/62.0
			//  Accept: */*
			//  Accept-Language: en-US,en;q=0.5
			//  Accept-Encoding: gzip, deflate
			//  Referer: http://192.168.12.128:8086/
			//  Connection: keep-alive
			//  Cache-Control: max-age=0
			//  get method  GET/POST 
			while((recv_buf[j] != ' ') && (recv_buf[j] != '\0'))
				method[i++] = recv_buf[j++];
			++j;
			method[i] = '\0';
			//printf("%s\n", method);  GET
			
			//get filename
			i = 0;
			while((recv_buf[j] != ' ') && (recv_buf[j] != '\0'))
				file_name[i++] = recv_buf[j++];
			++j;
			file_name[i] = '\0';
			//printf("%s\n", file_name);  /
			
			//GET method
			if(strcasecmp(method, "GET") == 0)
				responseGET(file_name);
			//POST method
			else if(strcasecmp(method, "POST") == 0) {
				char argvs[100];
				memset(argvs, 0, sizeof(argvs));
				int k = 0;
				char *c = NULL;
				
				++j;
				while((c = strstr(argvs, "Content-Length")) == NULL) {
					k = 0;
					memset(argvs, 0, sizeof(argvs));
					while((recv_buf[j] != '\r') && (recv_buf[j] != '\0'))
						argvs[k++] = recv_buf[j++];
					++j;
				}
				//get Content-Length
				int length = 0;
				char *str = strchr(argvs, ':');
				++str;
				sscanf(str, "%d", &length); 
				
				j = strlen(recv_buf) - length;
				k = 0;
				memset(argvs, 0, sizeof(argvs));
				while((recv_buf[j] != '\r') && (recv_buf[j] != '\0'))
						argvs[k++] = recv_buf[j++];
					
				argvs[k] = '\0';
				
				responsePOST(file_name, argvs);
			}  //end of POST method
			//other method
			else {
				char message[512];
				sprintf(message, "<html><title>LightWebServer Error</title>");
				sprintf(message, "%s<body>\r\n", message);
				sprintf(message, "%s 501\r\n", message);
				sprintf(message, "%s <p>%s: Httpd does not implement this method", 
					message, method);
				sprintf(message, "%s<hr><h3>powered by LightWebServer<h3></body>", message);
				response(message, 501);
			}
			break;
		}
	}
	
	sleep(3);  //wait for client close, avoid TIMEOUT
	close(conn_fd);
}

void task::responseGET(char *_file_name) {
	char file[100];
	strcpy(file, path.c_str());
	
	int i = 0;
	bool is_dynamic = false;
	char argv[20];
	
	while((_file_name[i] != '\?') && (_file_name[i] != '\0'))
		++i;
	//有？则是动态请求
	if(_file_name[i] == '\?') {
		int j = i;
		++i;
		int k = 0;
		is_dynamic = true;
		while(_file_name[i] != '\0')  //分离参数和文件名
			argv[k++] = _file_name[i++];
		argv[k] = '\0';
		_file_name[j] = '\0';
	}
	
	if(strcmp(_file_name, "/") == 0)  //直接输入ip:port则会进入
		strcat(file, "/index.html");  //默认访问index.html
	else
		strcat(file, _file_name);
	
	struct stat filestat;
	int ret = stat(file, &filestat);
	
	if((ret < 0) || S_ISDIR(filestat.st_mode)) {  //file dosen't exit
		char message[512];
		sprintf(message, "<html><title>LightWebServer Error</title>");
		sprintf(message, "%s<body>\r\n", message);
		sprintf(message, "%s 404\r\n", message);
		sprintf(message, "%s <p>GET: Can't find the file", message);
		sprintf(message, "%s<hr><h3>powered by LightWebServer<h3></body>", 
			message);
		response(message, 404);
		return;
	}
	
	if(is_dynamic) {
		if(fork() == 0) {
			dup2(conn_fd, STDOUT_FILENO);
			execl(file, argv);
		}
		wait(NULL);
	}
	else {
		int filefd = open(file, O_RDONLY);
		responseFile(filestat.st_size, 200);
		sendfile(conn_fd, filefd, 0, filestat.st_size);
	}
}

void task::responsePOST(char *_file_name, char *_argvs) {
	char file[100];
	strcpy(file, path.c_str());
	
	strcat(file, _file_name);
	
	struct stat filestat;
	int res = stat(file, &filestat);
	printf("%s\n", file);
	if((res < 0) || S_ISDIR(filestat.st_mode)) {   //file doesn't exit
		char message[512];
		sprintf(message, "<html><title>LightWebServer Error</title>");
		sprintf(message, "%s<body>\r\n", message);
		sprintf(message, "%s 404\r\n", message);
		sprintf(message, "%s <p>GET: Can't find the file", message);
		sprintf(message, "%s<hr><h3>powered by LightWebServer<h3></body>", 
			message);
		response(message, 404);
		return;
	}
	
	char argv[20];
	int a = 0, b = 0;
	res = sscanf(_argvs, "a=%d&b=%d", &a, &b);
	if((res < 0) || (res != 2)) {
		char message[512];
		sprintf(message, "<html><title>LightWebServer Error</title>");
		sprintf(message, "%s<body>\r\n", message);
		sprintf(message, "%s 404\r\n", message);
		sprintf(message, "%s <p>GET: Parameter error", message);
		sprintf(message, "%s<hr><h3>powered by LightWebServer<h3></body>", 
			message);
		response(message, 404);
		return;
	}
	
	sprintf(argv, "%d&%d", a, b);
	if(fork() == 0) {
		dup2(conn_fd, STDOUT_FILENO);
		execl(file, argv);
	}
	wait(NULL);
}



#endif