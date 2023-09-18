all: server client

server: ./src/server/*.cpp
	g++ ./src/server/*.cpp -o ./bin/server -I ./include -lsqlite3

client: ./src/client/*.cpp
	g++ ./src/client/*.cpp -o ./bin/client -I ./include -lpthread
