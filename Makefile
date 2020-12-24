all: kv

mpipe: kv.cpp
	c++ --std=c++2a -Wall $^ -o kv
