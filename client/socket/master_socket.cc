#include <cerrno>
#include "master_socket.hh"
#include "../main/client.hh"
#include "../../common/util/debug.hh"
#include "../../common/ds/instance_id_generator.hh"

#define SOCKET_COLOR YELLOW

MasterSocket::MasterSocket() {
	this->isRunning = false;
	this->tid = 0;
	this->epoll = 0;
	this->buffer.size = PROTO_HEADER_SIZE;
	this->sockets.needsDelete = true;
}

bool MasterSocket::init( int type, uint32_t addr, uint16_t port, EPoll *epoll ) {
	this->epoll = epoll;
	return (
		Socket::init( type, addr, port ) &&
		this->listen() &&
		epoll->add( this->sockfd, EPOLL_EVENT_LISTEN )
	);
}

bool MasterSocket::start() {
	if ( pthread_create( &this->tid, NULL, MasterSocket::run, ( void * ) this ) != 0 ) {
		__ERROR__( "MasterSocket", "start", "Cannot start MasterSocket thread." );
		return false;
	}
	this->isRunning = true;
	return true;
}

void MasterSocket::stop() {
	if ( this->isRunning ) {
		this->epoll->stop( this->tid );
		this->isRunning = false;
		pthread_join( this->tid, 0 );
	}
}

void MasterSocket::print( FILE *f ) {
	char buf[ 16 ];
	Socket::ntoh_ip( this->addr.sin_addr.s_addr, buf, 16 );
	fprintf( f, "[%4d] %s:%u (%slistening)\n", this->sockfd, buf, Socket::ntoh_port( this->addr.sin_port ), this->isRunning ? "" : "not " );
}

void MasterSocket::printThread( FILE *f ) {
	fprintf( f, "MasterSocket thread for epoll (#%lu): %srunning\n", this->tid, this->isRunning ? "" : "not " );
}

void *MasterSocket::run( void *argv ) {
	MasterSocket *socket = ( MasterSocket * ) argv;
	socket->epoll->start( MasterSocket::handler, socket );
	pthread_exit( 0 );
	return 0;
}

bool MasterSocket::handler( int fd, uint32_t events, void *data ) {
	MasterSocket *socket = ( MasterSocket * ) data;
	static Master *master = Master::getInstance();
	static InstanceIdGenerator *generator = InstanceIdGenerator::getInstance();

	///////////////////////////////////////////////////////////////////////////
	if ( ! ( events & EPOLLIN ) && ( ( events & EPOLLERR ) || ( events & EPOLLHUP ) || ( events & EPOLLRDHUP ) ) ) {
		// Find the socket in the lists
		int index;
		if ( socket->sockets.get( fd, &index ) ) {
			::close( fd );
			socket->sockets.removeAt( index );
		} else {
			ApplicationSocket *applicationSocket = master->sockets.applications.get( fd );
			CoordinatorSocket *coordinatorSocket = applicationSocket ? 0 : master->sockets.coordinators.get( fd );
			SlaveSocket *slaveSocket = ( applicationSocket || coordinatorSocket ) ? 0 : master->sockets.slaves.get( fd );
			if ( applicationSocket ) {
				applicationSocket->stop();
			} else if ( coordinatorSocket ) {
				coordinatorSocket->stop();
			} else if ( slaveSocket ) {
				// Wait for the coordinator's announcement
				// slaveSocket->stop();
			} else {
				__ERROR__( "MasterSocket", "handler", "Unknown socket." );
				return false;
			}
		}
	///////////////////////////////////////////////////////////////////////////
	} else if ( fd == socket->getSocket() ) {
		struct sockaddr_in *addr;
		socklen_t addrlen;
		while( 1 ) {
			addr = new struct sockaddr_in;
			fd = socket->accept( addr, &addrlen );
			if ( fd == -1 ) {
				delete addr;
				if ( errno != EAGAIN && errno != EWOULDBLOCK ) {
					__ERROR__( "MasterSocket", "handler", "%s", strerror( errno ) );
					return false;
				}
				break;
			}
			MasterSocket::setNonBlocking( fd );
			socket->sockets.set( fd, addr, false );
			socket->epoll->add( fd, EPOLL_EVENT_SET );
		}
	///////////////////////////////////////////////////////////////////////////
	} else {
		int index;
		struct sockaddr_in *addr;
		if ( ( addr = socket->sockets.get( fd, &index ) ) ) {
			// Read message immediately and add to appropriate socket list such that all "add" operations originate from the epoll thread
			// Only application register message is expected
			bool connected;
			ssize_t ret;

			ret = socket->recv( fd, socket->buffer.data, socket->buffer.size, connected, true );
			if ( ret < 0 ) {
				__ERROR__( "MasterSocket", "handler", "Cannot receive message." );
				return false;
			} else if ( ( size_t ) ret == socket->buffer.size ) {
				ProtocolHeader header;
				socket->protocol.parseHeader( header, socket->buffer.data, socket->buffer.size );
				// Register message expected
				if ( header.magic == PROTO_MAGIC_REQUEST && header.opcode == PROTO_OPCODE_REGISTER ) {
					if ( header.from == PROTO_MAGIC_FROM_APPLICATION ) {
						ApplicationSocket *applicationSocket = new ApplicationSocket();
						// fprintf( stderr, "new ApplicationSocket: 0x%p\n", applicationSocket );
						applicationSocket->init( fd, *addr );
						master->sockets.applications.set( fd, applicationSocket );

						socket->sockets.removeAt( index );

						socket->done( fd ); // The socket is valid

						ApplicationEvent event;
						uint16_t instanceId = generator->generate( applicationSocket );
						event.resRegister( applicationSocket, instanceId, header.requestId );
						master->eventQueue.insert( event );
					} else {
						::close( fd );
						socket->sockets.removeAt( index );
						__ERROR__( "MasterSocket", "handler", "Invalid register message source." );
						return false;
					}
				} else {
					__ERROR__( "MasterSocket", "handler", "Invalid register message." );
					return false;
				}
			} else {
				__ERROR__( "MasterSocket", "handler", "Message corrupted." );
				return false;
			}
		} else {
			ApplicationSocket *applicationSocket = master->sockets.applications.get( fd );
			CoordinatorSocket *coordinatorSocket = applicationSocket ? 0 : master->sockets.coordinators.get( fd );
			SlaveSocket *slaveSocket = ( applicationSocket || coordinatorSocket ) ? 0 : master->sockets.slaves.get( fd );
			if ( applicationSocket ) {
				ApplicationEvent event;
				event.pending( applicationSocket );
				master->eventQueue.insert( event );
			} else if ( coordinatorSocket ) {
				CoordinatorEvent event;
				event.pending( coordinatorSocket );
				master->eventQueue.insert( event );
			} else if ( slaveSocket ) {
				SlaveEvent event;
				event.pending( slaveSocket );
				master->eventQueue.prioritizedInsert( event );
			} else {
				__ERROR__( "MasterSocket", "handler", "Unknown socket." );
				return false;
			}
		}
	}
	return true;
}