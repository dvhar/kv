all: mpipe

mpipe: mpipe.cpp
	clang++ --std=c++2a -Wall $^ -o mpipe
