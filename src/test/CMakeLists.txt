INCLUDE_DIRECTORIES(
    "${PROJECT_SOURCE_DIR}/src/libnet/include"
)

SET(SRC
	"${CMAKE_CURRENT_SOURCE_DIR}/test.cpp"
)

SOURCE_GROUP(\\ FILES ${SRC})

ADD_EXECUTABLE(test
	${SRC}
)

IF(WIN32)
TARGET_LINK_LIBRARIES(test
	"${PROJECT_SOURCE_DIR}/bin/$<CONFIGURATION>/libnet.lib"
)
ADD_DEPENDENCIES(test libnet)
ENDIF(WIN32)

IF(UNIX)
TARGET_LINK_LIBRARIES(test
	"${PROJECT_SOURCE_DIR}/bin/libnet.a"
	-lpthread
	-lrt
)
ENDIF(UNIX)
