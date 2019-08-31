all: mpipe

mpipe: mpipe.cpp
	g++ --std=c++2a -Wall $^ -o mpipe
