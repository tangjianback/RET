CC = g++
LDFLAGS = -libverbs -pthread
all:rc_pingpong_client rc_pingpong_server cc_test
rc_pingpong_client:  rdmacc.cpp pingpong.cpp rc_client.cpp ../headers/*.hpp ../headers/*.h
	$(CC) -g  rdmacc.cpp pingpong.cpp rc_client.cpp -o  rc_pingpong_client $(LDFLAGS)

rc_pingpong_server:  pingpong.cpp rc_server.cpp ../headers/*.hpp ../headers/*.h
	$(CC) -g  rdmacc.cpp pingpong.cpp rc_server.cpp -o  rc_pingpong_server $(LDFLAGS)

cc_test: rdmacc.cpp cc.cpp ../headers/*.hpp ../headers/*.h
	$(CC) -g  rdmacc.cpp cc.cpp  -o  cc_test $(LDFLAGS)

clean:
	rm rc_pingpong_server
	rm rc_pingpong_client