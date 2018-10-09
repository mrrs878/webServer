#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fstream>
#include <fcntl.h>
#include <wait.h>
#include <unistd.h>
#include <errno.h>
#include <string>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <arpa/inet.h>
#include <netinet/in.h>

using namespace std;

void errorExit(const string &_errinfo) {
	cout << _errinfo << "    exit!\n";
	exit(1);
}

const string path = "/root/webserver";

class webServer {
private:
#define MAX_EVENTS 1024
	struct my_event_s {
		int fd;
		void(*call_back)(int _fd, int _events, void *_arg);
		int events;  
		void *arg;
		int status;  //1:in epoll wait list    0:not in
		char *recv_buf;  //recv data buffer
		int len, s_offset;
		long last_active;
		my_event_s(void) { recv_buf = new char[4096]; }
		~my_event_s(void) { delete[] recv_buf; }
	};
	int epollfd_g;
	my_event_s my_events_g[MAX_EVENTS + 1];

	int port;

	static webServer *tmp_cb;

public:
	webServer(int _port = 8086) : port(_port) {}
	~webServer(void) {}
	void cfgEpoll(void);

private:
	void cfgServer(void);

	void setTmp_cb(void);
	static void acceptConn_cb(int _fd, int _events, void *arg) { tmp_cb->acceptConn(_fd, _events, arg); }
	static void recvData_cb(int _fd, int _events, void *_arg) { tmp_cb->recvData(_fd, _events, _arg); }
	static void sendData_cb(int _fd, int _events, void *_arg) { tmp_cb->sendData(_fd, _events, _arg); }
	void acceptConn(int _fd, int _events, void *arg);
	void recvData(int _fd, int _events, void *_arg);
	void sendData(int _fd, int _events, void *_arg);

	void eventSet(my_event_s *_ev, int _fd, void(*call_back)(int, int, void*), void *_arg);
	void eventAdd(int _epollfd, int _events, my_event_s *_ev);
	void eventDel(int _epollfd, my_event_s *_ev);

	void responseFile(my_event_s *_ev, const char* _file_name, int _stat);
	void responseGET(char *_file_name, void* _arg);
	//void responsePOST(const *_file_name, const string *_argvs);
};

int main(void) {
	webServer server;

	server.cfgEpoll();

	return 0;
}

webServer *webServer::tmp_cb = NULL;
void webServer::setTmp_cb() { tmp_cb = this; }

void webServer::cfgEpoll(void) {
	//创建实例
	epollfd_g = epoll_create(MAX_EVENTS);
	if (epollfd_g <= 0)
		perror("epoll_create error");

	//配置服务器相关--IP/PORT
	cfgServer();
	cout << "server running:port[" << port << "]\n";

	//event loop
	struct epoll_event ep_events[MAX_EVENTS];
	int check_pos = 0;
	while (1) {
		long now = time(NULL);
		for (int i = 0; i < 100; ++i, ++check_pos) {
			if (check_pos == MAX_EVENTS)
				check_pos = 0;
			if (my_events_g[check_pos].status != 1)
				continue;
			long duration = now - my_events_g[check_pos].last_active;
			if (duration >= 60) {
				//60s timeout
				close(my_events_g[check_pos].fd);
				cout << "[fd=" << my_events_g[check_pos].fd << "] timeout[" << my_events_g[check_pos].last_active - now << "] closed!\n";
				eventDel(epollfd_g, &my_events_g[check_pos]);
			}
		}
		//wait for events to happen
		int fds = epoll_wait(epollfd_g, ep_events, MAX_EVENTS, 1000);
		if (fds < 0) {
			perror("epoll_wait error!, exit\n");
			break;
		}
		for (int i = 0; i < fds; ++i) {
			my_event_s *ev = (struct my_event_s*)ep_events[i].data.ptr;
			if ((ep_events[i].events&EPOLLIN) && (ev->events&EPOLLIN)) //read event
				ev->call_back(ev->fd, ep_events[i].events, ev->arg);

			if ((ep_events[i].events&EPOLLOUT) && (ev->events&EPOLLOUT))
				ev->call_back(ev->fd, ep_events[i].events, ev->arg);
		}
	}
}

void webServer::cfgServer(void) {
	int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0)
		errorExit("socket error");
	cout << "server listening fd=" << listen_fd << endl;

	//set no-blocking
	fcntl(listen_fd, F_SETFL, O_NONBLOCK);
	//add a listen event
	setTmp_cb();
	eventSet(&my_events_g[MAX_EVENTS], listen_fd, acceptConn_cb, &my_events_g[MAX_EVENTS]);
	eventAdd(epollfd_g, EPOLLIN, &my_events_g[MAX_EVENTS]);

	sockaddr_in serv_addr;
	bzero(&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	listen(listen_fd, 10);
}

void webServer::acceptConn(int _fd, int _events, void *arg) {
	struct sockaddr_in cli_addr;
	socklen_t len = sizeof(struct sockaddr_in);
	int nfd = 0, i = 0;

	if ((nfd = accept(_fd, (struct sockaddr*)&cli_addr, &len)) == -1) {
		if (errno != EAGAIN && errno != EINTR) {}
		printf("%s: accept, %d", __FUNCTION__, errno);
		return;
	}

	do
	{
		for (i = 0; i < MAX_EVENTS; i++) {
			if (my_events_g[i].status == 0)
				break;
		}
		if (i == MAX_EVENTS)
		{
			printf("%s:max connection limit[%d].", __FUNCTION__, MAX_EVENTS);
			break;
		}
		// set nonblocking
		int iret = 0;
		if ((iret = fcntl(nfd, F_SETFL, O_NONBLOCK)) < 0) {
			printf("%s: fcntl nonblocking failed:%d", __FUNCTION__, iret);
			break;
		}
		// add a read event for receive data 
		setTmp_cb();
		eventSet(&my_events_g[i], nfd, recvData_cb, &my_events_g[i]);
		eventAdd(epollfd_g, EPOLLIN, &my_events_g[i]);
	} while (0);
	printf("new conn[%s:%d][time:%d], pos[%d]\n", inet_ntoa(cli_addr.sin_addr),
		ntohs(cli_addr.sin_port), my_events_g[i].last_active, i);
}

void webServer::recvData(int _fd, int _events, void *_arg) {
	struct my_event_s *ev = (struct my_event_s*)_arg;
	int len;

	// receive data
	len = recv(_fd, ev->recv_buf + ev->len, sizeof(ev->recv_buf) - 1 - ev->len, 0);
	if (len > 0) {
		char method[5];
		char file_name[20];
		int i = 0, j = 0;

		while ((ev->recv_buf[j] != ' ') && (ev->recv_buf[j] != '\0'))
			method[i++] = ev->recv_buf[j++];
		++j;
		method[i] = '\0';

		//get filename
		i = 0;
		while ((ev->recv_buf[j] != ' ') && (ev->recv_buf[j] != '\0'))
			file_name[i++] = ev->recv_buf[j++];
		++j;
		file_name[i] = '\0';

		bzero(ev->recv_buf, sizeof(ev->recv_buf));

		//GET method
		if (strcasecmp(method, "GET") == 0)
			responseGET(file_name, _arg);
		//POST method
		else if (strcasecmp(method, "POST") == 0) {
			char argvs[100];
			memset(argvs, 0, sizeof(argvs));
			int k = 0;
			char *c = NULL;

			++j;
			while ((c = strstr(argvs, "Content-Length")) == NULL) {
				k = 0;
				memset(argvs, 0, sizeof(argvs));
				while ((ev->recv_buf[j] != '\r') && (ev->recv_buf[j] != '\0'))
					argvs[k++] = ev->recv_buf[j++];
				++j;
			}
			//get Content-Length
			int length = 0;
			char *str = strchr(argvs, ':');
			++str;
			sscanf(str, "%d", &length);

			j = strlen(ev->recv_buf) - length;
			k = 0;
			memset(argvs, 0, sizeof(argvs));
			while ((ev->recv_buf[j] != '\r') && (ev->recv_buf[j] != '\0'))
				argvs[k++] = ev->recv_buf[j++];

			argvs[k] = '\0';

			//responsePOST(_fd, file_name, argvs);
		}
	}
	else if (len == 0) {
		close(ev->fd);
		printf("[fd=%d] pos[%d], closed gracefully.\n", _fd, ev - my_events_g);
	}
	else {
		close(ev->fd);
		printf("recv[fd=%d] error[%d]:%s\n", _fd, errno, strerror(errno));
	}
}

void webServer::responseGET(char *_file_name, void* _arg) {
	char file[100];
	strcpy(file, path.c_str());

	int i = 0;
	bool is_dynamic = false;
	char argv[20];
	struct my_event_s *ev = (struct my_event_s*)_arg;

	while ((_file_name[i] != '\?') && (_file_name[i] != '\0'))
		++i;
	//有？则是动态请求
	if (_file_name[i] == '\?') {
		int j = i;
		++i;
		int k = 0;
		is_dynamic = true;
		while (_file_name[i] != '\0')  //分离参数和文件名
			argv[k++] = _file_name[i++];
		argv[k] = '\0';
		_file_name[j] = '\0';
	}

	if (strcmp(_file_name, "/") == 0)  //直接输入ip:port则会进入
		strcat(file, "/index.html");  //默认访问index.html
	else
		strcat(file, _file_name);

	struct stat filestat;
	int ret = stat(file, &filestat);

	if ((ret < 0) || S_ISDIR(filestat.st_mode)) {  //file dosen't exit
		responseFile(ev, "./404.html", 404);
		return;
	}

	if (is_dynamic) {
		if (fork() == 0) {
			dup2(ev->fd, STDOUT_FILENO);
			execl(file, argv);
		}
		wait(NULL);
	}
	else
		responseFile(ev, file, 200);
}

//void webServer::responsePOST(const *_file_name, const string *_argvs)

void webServer::responseFile(my_event_s *_ev, const char* _file_name, int _stat) {
	struct stat filestat;
	int ret = stat(_file_name, &filestat);
	char *head_buf = new char[128];

	bzero(head_buf, sizeof(head_buf));
	sprintf(head_buf, "HTTP/1.1 %d OK\r\nConnection: Close\r\n"
		"content-length:%d\r\n\r\n", _stat, filestat.st_size);
	strcat(_ev->recv_buf, head_buf);
	_ev->len += strlen(head_buf);
	delete[] head_buf;

	send(_ev->fd, _ev->recv_buf + _ev->s_offset, _ev->len - _ev->s_offset, 0);
	int filefd = open(_file_name, O_RDONLY);
	sendfile(_ev->fd, filefd, 0, filestat.st_size);

	eventDel(epollfd_g, _ev);
	setTmp_cb();
	eventSet(_ev, _ev->fd, recvData_cb, _ev);
	eventAdd(epollfd_g, EPOLLIN, _ev);
}

void webServer::sendData(int _fd, int _events, void *_arg) {
	my_event_s *ev = (struct my_event_s*)_arg;
	int len;

	// send data
	len = send(_fd, ev->recv_buf + ev->s_offset, ev->len - ev->s_offset, 0);
	if (len > 0) {
		printf("send[fd=%d], [%d<->%d]%s\n", _fd, len, ev->len, ev->recv_buf);
		ev->s_offset += len;
		if (ev->s_offset == ev->len) {
			// change to receive event
			eventDel(epollfd_g, ev);
			setTmp_cb();
			eventSet(ev, _fd, recvData_cb, ev);
			eventAdd(epollfd_g, EPOLLIN, ev);
		}
	}
	else {
		//cout << ev->buff << endl;
		close(ev->fd);
		eventDel(epollfd_g, ev);
		//cout << len << endl;
		printf("send[fd=%d] error[%d]\n", _fd, errno);
	}
}

void webServer::eventSet(my_event_s *_ev, int _fd, void(*_call_back)(int, int, void*), void *_arg) {
	_ev->fd = _fd;
	_ev->call_back = _call_back;
	_ev->events = 0;
	_ev->arg = _arg;
	_ev->status = 0;
	//_ev->buff
	bzero(_ev->recv_buf, sizeof(_ev->recv_buf));
	_ev->s_offset = 0;
	_ev->len = 0;
	_ev->last_active = time(NULL);
}

void webServer::eventAdd(int _epollfd, int _events, my_event_s *_ev) {
	struct epoll_event epv = { 0,{ 0 } };
	int op = 0;

	epv.data.ptr = _ev;
	epv.events = _ev->events = _events;
	if (_ev->status == 1)
		op = EPOLL_CTL_MOD;
	else {
		op = EPOLL_CTL_ADD;
		_ev->status = 1;
	}

	if (epoll_ctl(epollfd_g, op, _ev->fd, &epv) < 0)
		cout << "Event Add failed[fd=" << _ev->fd << "], evnets[" << _events << "]\n";
	else
		cout << "Event Add OK[fd=" << _ev->fd << "], op=" << op << ", evnets[" << _events << "]\n";
}

void webServer::eventDel(int _epollfd, my_event_s *_ev) {
	struct epoll_event epv = { 0,{ 0 } };

	if (_ev->status != 1)
		return;
	epv.data.ptr = _ev;
	_ev->status = 0;
	epoll_ctl(_epollfd, EPOLL_CTL_DEL, _ev->fd, &epv);
}

