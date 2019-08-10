#include <vector>
#include <iostream>
#include <functional>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <boost/algorithm/string.hpp>
using namespace std;

int metaKey; //shm key for list of all keys
void handleKeys(string, int);

void set(string key, vector<byte> v, int size, int hashed, int action){
	int sizeId, dataId, *sizePtr;
	byte* dataPtr;

	//set if empty
	if (action == 0){
		sizeId = shmget(hashed, 2*sizeof(int), IPC_EXCL | IPC_CREAT | 0666);
		if (sizeId<0) {cerr<<"key unavailable\n";exit(1);}
		dataId = shmget(hashed+1, size, IPC_EXCL | IPC_CREAT | 0666);
		if (dataId<0) {cerr<<"key  unavailable\n";exit(1);}
		sizePtr = (int*) shmat(sizeId, NULL, 0);
		if (sizePtr==(void*)-1) {cerr<<"metadata error\n";exit(1);}
		dataPtr = (byte*) shmat(dataId, NULL, 0);
		if (dataPtr==(void*)-1) {cerr<<"data error\n";exit(1);}

	//set even if not empty
	} else {
		sizeId = shmget(hashed, 2*sizeof(int), IPC_CREAT | 0666);
		if (sizeId<0) {perror("key error");exit(1);}
		sizePtr = (int*) shmat(sizeId, NULL, 0);
		if (sizePtr==(void*)-1) {perror("metadata error");exit(1);}
		dataId = shmget(hashed+1, size, IPC_EXCL | IPC_CREAT | 0666);
		//delete old one if necessary
		if (dataId<0) {
			dataId = shmget(hashed+1, sizePtr[1], IPC_CREAT | 0666);
			dataPtr = (byte*) shmat(dataId, NULL, 0);
			shmdt(dataPtr);
			shmctl(dataId, IPC_RMID, NULL);
			dataId = shmget(hashed+1, size, IPC_CREAT | IPC_EXCL | 0666);
			if (dataId<0) {perror("key error");exit(1);}
		}
		dataPtr = (byte*) shmat(dataId, NULL, 0);
		if (dataPtr==(void*)-1) {perror("data error");exit(1);}
	}
	sizePtr[0] = hashed;
	sizePtr[1] = size;
	for (int ii=0;ii<size;++ii)
		dataPtr[ii] = v[ii];
	handleKeys(key, 1);

	shmdt(sizePtr);
	shmdt(dataPtr);
}

void get(string key, int hashed, int action){
	int sizeId = shmget(hashed, 2*sizeof(int), 0);
	if (sizeId<0) {cerr<<"key unused\n";return;}
	int *sizePtr = (int*) shmat(sizeId, NULL, 0);
	if (sizePtr==(void*)-1) {cerr<<"metadata error\n";exit(1);}
	if (sizePtr[0] != hashed){
		cerr << key << "key unused\n";
		shmctl(sizeId, IPC_RMID, NULL);
		return;
	}
	int size = sizePtr[1];

	//checking presence and size
	if (action == 2){
		cout << size << endl;
		exit(0);
	}

	int dataId = shmget(hashed+1, size, 0);
	if (dataId<0) {cerr<<"metadata error\n";exit(1);}
	byte *dataPtr = (byte*) shmat(dataId, NULL, 0);
	if (dataPtr==(void*)-1) {cerr<<"data error\n";exit(1);}

	//retrieve value
	if (action < 2)
		//for (int ii=0;ii<size;++ii)
            //write(1, dataPtr+ii, 1);
            write(1, dataPtr, size);
	shmdt(sizePtr);
	shmdt(dataPtr);

	//delete
	if (action==1 || action==3){
		shmctl(sizeId, IPC_RMID, NULL);
		shmctl(dataId, IPC_RMID, NULL);
	}
}

typedef struct metadata {
	int size;
	char list[10000];
} metadata ;
void handleKeys(string key, int action){
	int metaId = shmget(875784987, 10016, IPC_EXCL | IPC_CREAT | 0666);
	int init = 0;
	if (metaId<0) {
		metaId = shmget(875784987, 10016, 0);
		if (metaId<0) {cerr<<"metakey error\n";exit(1);}
	} else { init = 1; }
	metadata *keys = (metadata*) shmat(metaId, NULL, 0);
	if (keys==(void*)-1) {cerr<<"metakey list error\n";exit(1);}
	hash<string> hasher;
	int hashed;

	//initialize
	if (init) {
		keys->size = 0;
		for (int ii=0;ii<10000;++ii)
			keys->list[ii] = 0;
	}

	//insert, list, remove
    int i=0;
    vector<string> split;
    string text = string(keys->list);
    if (text.length()>0) text.pop_back();
    boost::split(split, text, [](char c){return c == ':';});
	switch (action) {

	//add key
	case 1:
	for (i=0;keys->list[i]!=0;i++);
	for (int ii=0; ii<key.length(); ii++)
		keys->list[i++] = key.at(ii);
    keys->list[i++] = ':';
	break;

	//list or delete all keys
	case 2:
	case 3:
		for (int ii=0; ii<split.size(); ii++){
            if (split[ii] == "") continue;
            switch (action){
            case 2:
                cout << split[ii] << endl;
                break;
            case 3:
                hashed = hasher(split[ii]);
                get(split[ii], hashed, 3);
            }
        }
		if (action == 3){
			keys->size = 0;
			for (int ii=0;ii<10000;++ii)
				keys->list[ii] = 0;
		}
		break;

	//remove single key
	case 4:
        string S = "";
        for (int ii=0; ii<split.size(); ii++)
            if (split[ii] != key && split[ii] != "") S += (split[ii] + ":");
        for (int ii=0;ii<10000;++ii)
            keys->list[ii] = 0;
        auto cs = S.c_str(); i=0;
        strncpy(keys->list, cs, 10000);
	}
	shmdt(keys);
}

int main(int argc, char**argv){
	string key, command;
	vector<byte> v;
	hash<string> hasher;

	if (argc == 2 && string(argv[1]) == "clear") {
		handleKeys("",3);
		exit(0);
	} else if (argc == 2 && string(argv[1]) == "show") {
		handleKeys("",2);
		exit(0);
	} else if (argc == 3) {
		command = string(argv[1]);
		key = string(argv[2]);
	} else{
		cerr << "usage: " << argv[0] << " <set | get | pop | up | chk | del | clear> <key>\n"
			 << "       When using set or up, pipe input to stdin\n";
		exit(1);
	}
	int hashed = hasher(key);
	metaKey = ftok(argv[0], 1);
	
	//put data from stdin into a vector
	if (!isatty(0)){
		char buf[1];
		long n;
		while(n=read(0,buf,(long)sizeof(buf))>0)
			v.push_back((byte)buf[0]);
	}

	//run command
	int size = v.size();
	if (command == "set" || command == "up") {
		int action;
		if (command == "up") action = 1;
		set(key, v, size, hashed, action);
	} else if (command == "get" || command == "pop" || command == "del" || command == "chk") {
		int action;
		if (command == "get") action = 0;
		if (command == "pop") action = 1;
		if (command == "chk") action = 2;
		if (command == "del") action = 3;
		get(key, hashed, action);
		if (action == 1 || action == 3)
			handleKeys(key, 4);
	} else {
		cerr << "Invalid command: " << command << endl;
		exit(1);
	}
	return 0;
}