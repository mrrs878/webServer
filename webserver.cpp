//                                                          _ooOoo_
//                                                         o8888888o
//                                                         88" . "88
//                                                         (| -_- |)
//                                                          O\ = /O
//                                                      ____/`---'\____
//                                                    .   ' \\| |// `.
//                                                     / \\||| : |||// \
//                                                   / _||||| -:- |||||- \
//                                                     | | \\\ - /// | |
//                                                   | \_| ''\---/'' | |
//                                                    \ .-\__ `-` ___/-. /
//                                                 ___`. .' /--.--\ `. . __
//                                              ."" '< `.___\_<|>_/___.' >'"".
//                                             | | : `- \`.;`\ _ /`;.`/ - ` : | |
//                                               \ \ `-. \_ __\ /__ _/ .-` / /
//                                       ======`-.____`-.___\_____/___.-`____.-'======
//                                                          `=---='
//
//                                       .............................................
//                                              佛祖保佑             永无BUG
#include <iostream>
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "task.h"
#include "thread_pool.h"

using namespace std;

int errorQuit(const string &_err_info) {
	cout << _err_info << endl;
	return 1;
}
int main(int argc, char **argv) {
	if(argc != 2)
		errorQuit("usage: IP<port>");
	
	int listen_fd, conn_fd;
	struct sockaddr_in server_addr, clien_addr;
	int port = atoi(argv[1]);
	
	if((listen_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
		errorQuit("socket error!");
	
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if((bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr))) < 0)
		errorQuit("bind error!");
	
	if(listen(listen_fd, 10) < 0)
		errorQuit("listen error!");
	
	//创建线程池并运行
	threadpool<task> pool(20);
	pool.start();
	
	while(1) {
		socklen_t cli_len = sizeof(clien_addr);
		if((conn_fd = accept(listen_fd, (struct sockaddr*)&clien_addr, &cli_len)) < 0)
			errorQuit("accept error!");
		
		task *ta = new task(conn_fd);
		
		if(pool.append_task(ta) == false) {
			cout << "poo is full, return !!!\n";
			break;  //线程池已满
		}
	}
	
	return 0;
}