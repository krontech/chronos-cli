/*
 * socket.h
 *
 *  Created on: 2019-07-08
 *      Author: sanjay
 */

#ifndef SOCKET_H_
#define SOCKET_H_

#define SOCK_PATH "/tmp/pcUtil.socket"
#define UNIX_SOCKET_BUFFER_SIZE 256
#define UNIX_SOCKET_MAX_CLIENTS 20

int initSocket(void);
void *socketConnectionHandler(void *socket_desc);

#endif /* SOCKET_H_ */
