all: mpipe

mpipe: shmstore.cpp
	g++ --std=c++2a $^ -o mpipe
