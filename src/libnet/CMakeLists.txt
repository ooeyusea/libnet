INCLUDE_DIRECTORIES(
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

IF(WIN32)
INCLUDE_DIRECTORIES(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/iocp"
)

SET(NET_DETAIL
	"${CMAKE_CURRENT_SOURCE_DIR}/src/iocp/share_memory.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/iocp/share_memory.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/iocp/net.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/iocp/net.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/iocp/Connection.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/iocp/Connection.cpp"
)

ENDIF(WIN32)

IF(UNIX)
INCLUDE_DIRECTORIES(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/epoll"
)

SET(NET_DETAIL
	"${CMAKE_CURRENT_SOURCE_DIR}/src/epoll/net.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/epoll/net.cpp"
)
ENDIF(UNIX)

SET(SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/include/libnet.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/util.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/RingBuffer.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/lock_free_list.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/libnet.cpp"
)

SOURCE_GROUP(iocp FILES ${NET_DETAIL})
SOURCE_GROUP(\\ FILES ${SRC})

ADD_LIBRARY(libnet STATIC
	${SRC}
	${NET_DETAIL}
)
