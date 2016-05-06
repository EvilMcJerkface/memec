#ifndef __COORDINATOR_SOCKET_SERVER_SOCKET_HH__
#define __COORDINATOR_SOCKET_SERVER_SOCKET_HH__

#include <unordered_map>
#include "../ds/map.hh"
#include "../../common/ds/array_map.hh"
#include "../../common/ds/key.hh"
#include "../../common/ds/load.hh"
#include "../../common/ds/metadata.hh"
#include "../../common/lock/lock.hh"
#include "../../common/socket/socket.hh"

class ServerSocket : public Socket {
private:
	static ArrayMap<int, ServerSocket> *servers;
	struct sockaddr_in recvAddr;
	char *identifier;

public:
	uint16_t instanceId;
	Map map;
	struct {
		std::unordered_set<Metadata> data;
		std::unordered_set<Metadata> parity;
		LOCK_T lock;
	} hotness;
	ServerSocket *failed;

	static void setArrayMap( ArrayMap<int, ServerSocket> *servers );
	bool init( int tmpfd, ServerAddr &addr, EPoll *epoll );
	bool start();
	void stop();
	bool setRecvFd( int fd, struct sockaddr_in *addr );
	ssize_t send( char *buf, size_t ulen, bool &connected );
	ssize_t recv( char *buf, size_t ulen, bool &connected, bool wait );
	ssize_t recvRem( char *buf, size_t expected, char *prevBuf, size_t prevSize, bool &connected );
	bool done();
};

#endif
