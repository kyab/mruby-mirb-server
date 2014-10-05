//
//  main.c
//  mirb_tcpserver
//

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "mruby.h"
#include "mruby/proc.h"

#define SUCCESS 0

int receive_bytes(int sock, void *buf, ssize_t size){
	ssize_t received_size = 0;
	uint8_t *ptr = buf;
	while (received_size < size){
		ssize_t ret = recv(sock, &ptr[received_size], size - received_size, 0);
		if ( -1 == ret){
			perror("socket recv error");
			return -1;
		}else if (0 == ret){	//EOF
			printf("disconnected from client\n");
			return -1;
		}
		received_size += ret;
	}
	return SUCCESS;
}

int send_bytes(int sock, const void *buf, ssize_t size){
	ssize_t ret = send(sock, buf, size, 0);
	if ( -1 == ret){
		perror("socket send error");
		return -1;
	}
	
	if (ret != size){
		printf("VERY BAD HAPPEN...\n");
		return -1;
	}
	return SUCCESS;
}

int send_header(int sock, const char *type, uint16_t content_size){
	uint8_t header[13];
	memset(header, 0, 13);
	strncpy((char *)header, "MIRB:", strlen("MIRB:"));
	strncpy((char *)&header[5], type, strlen("TYPE: "));
	
	header[11] = (uint8_t)content_size >> 8;
	header[12] = (uint8_t)content_size;
	
	return send_bytes(sock, header, 13);
	
}

int send_contents_str(int sock, const char *content_str){
	return send_bytes(sock, content_str, strlen(content_str));
}

int process_init_request(int sock, mrb_state **p_mrb){
	*p_mrb = mrb_open();
	
	
	if (NULL == *p_mrb){
		const char *message = "somehow mrb_open() failed...";
		int ret = send_header(sock, "INIT :", strlen(message));
		if (SUCCESS == ret){
			ret = send_contents_str(sock, message);
		}
		return ret;
	}
	
	return send_header(sock, "INIT :", 0);
	
}

int process_code_request(int sock ,mrb_state *mrb, void *code, ssize_t size){
	return SUCCESS;
}

void do_mirb(int sock){
	
	uint8_t header[12];
	uint8_t *contents = NULL;
	mrb_state *mrb = NULL;
	
	while (TRUE){
		memset(header,0,12);
		
		//receive header("MIRB:" + "type " + ":" + len(2) = 13)
		int ret = receive_bytes(sock, header, 13);
		if (SUCCESS != ret){
			goto shutdown;
		}
		
		//check SIGNATURE
		if (0 != strncmp((const char *)header, "MIRB:", strlen("MIRB:"))){
			printf("unknown packet. ignored\n");
			continue;
		}
		
		//get type and size.
		uint16_t content_size = ((uint16_t)header[11] << 8) | ((uint16_t)header[12]);
		if (0 == strncmp((const char *)&header[5], "init :", strlen("TYPE :"))){
			if (0 != content_size){
				perror("illegal <init>. ignore\n");
				continue;
			}
			ret = process_init_request(sock, &mrb);
			
		}else if (0 == strncmp((const char *)&header[5], "code :", strlen("TYPE :"))){
			contents = (uint8_t *)malloc(content_size);
			ret = receive_bytes(sock , contents, content_size);
			if (SUCCESS != ret){
				free(contents);
				goto shutdown;
			}
			
			ret = process_code_request(sock, mrb, contents, content_size);
			free(contents);
			if (SUCCESS != ret){
				goto shutdown;
			}
			
		}
	}
	return;
	
shutdown:
	if (mrb) {
		mrb_close(mrb);
	}
	int sret = close(sock);
	if (0 != sret){
		printf("[warn] bad close.. some response may not received by client\n");
	}
	return;
	
}

int main(int argc, const char * argv[]) {
	
	int sock_server = socket(AF_INET, SOCK_STREAM, 0);
	
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(5566);
	
	{
		int on = 1;
		setsockopt(sock_server, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	}
	
	int ret = bind(sock_server, (struct sockaddr *)&addr, sizeof(addr));
	if (0 != ret){
		perror("bind error");
		return -1;
	}
	
	ret = listen(sock_server, 2);
	if (0!= ret){
		perror("listen error");
		return -1;
	}
	
	printf("listening on 0.0.0.0:5566..\n");
	
	while(TRUE){
		int sock_client;
		struct sockaddr_in addr_client;
		socklen_t addrlen = sizeof(addr_client);
		memset(&addr_client, 0, sizeof(addr_client));
		
		sock_client = accept(sock_server, (struct sockaddr *)&addr_client, &addrlen);
		if (-1 == sock_client){
			perror("error on accept.");
			continue;
		}
		
		printf("connected from : %s:%u\n",inet_ntoa(addr_client.sin_addr), ntohs(addr_client.sin_port));
		
		do_mirb(sock_client);
	}
	
	return 0;
}
