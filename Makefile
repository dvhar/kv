all: mpipe

mpipe: shmstore.cpp
	g++ --std=c++2a -Wall $^ -o mpipe
