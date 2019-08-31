#include <list>
#include <map>
#include <iostream>
#include <functional>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <boost/algorithm/string.hpp>
#define metaShmKey 875784987
#define chunksize 8192
#define listsize 10000
using namespace std;

enum { SET, UPDATE, SHOW, DELETE, CLEAR, GET, POP, CHECK };
void handleKeys(string, int);
typedef struct chunk {
	int size;
	byte data[chunksize];
} chunk ;
typedef struct metadata {
	int size;
	char list[listsize];
} metadata ;
map<string, int> commands = {
	{"get", GET},
	{"set", SET},
	{"pop", POP},
	{"up", UPDATE},
	{"show", SHOW},
	{"del", DELETE},
	{"clear", CLEAR},
	{"chk", CHECK}
};

void set(string key, list<chunk> chunks, int size, int hashed, int action){
	int sizeId, dataId, *sizePtr;
	byte* dataPtr;

	//set if empty
	if (action == SET){
		sizeId = shmget(hashed, 2*sizeof(int), IPC_EXCL | IPC_CREAT | 0666);
		if (sizeId<0) {
			perror("key unavailable");
			exit(1);
		}
		dataId = shmget(hashed+1, size, IPC_EXCL | IPC_CREAT | 0666);
		if (dataId<0) {
			perror("key unavailable");
			exit(1);
		}
		sizePtr = (int*) shmat(sizeId, NULL, 0);
		if (sizePtr==(void*)-1) {
			perror("metadata error");
			exit(1);
		}
		dataPtr = (byte*) shmat(dataId, NULL, 0);
		if (dataPtr==(void*)-1) {
			perror("data error");
			exit(1);
		}

	//set even if not empty
	} else {
		sizeId = shmget(hashed, 2*sizeof(int), IPC_CREAT | 0666);
		if (sizeId<0) {
			perror("key error");
			exit(1);
		}
		sizePtr = (int*) shmat(sizeId, NULL, 0);
		if (sizePtr==(void*)-1) {
			perror("metadata error");
			exit(1);
		}
		dataId = shmget(hashed+1, size, IPC_EXCL | IPC_CREAT | 0666);
		//delete old one if necessary
		if (dataId<0) {
			dataId = shmget(hashed+1, sizePtr[1], IPC_CREAT | 0666);
			dataPtr = (byte*) shmat(dataId, NULL, 0);
			shmdt(dataPtr);
			shmctl(dataId, IPC_RMID, NULL);
			dataId = shmget(hashed+1, size, IPC_CREAT | IPC_EXCL | 0666);
			if (dataId<0) {
				perror("key error");
				exit(1);
			}
		}
		dataPtr = (byte*) shmat(dataId, NULL, 0);
		if (dataPtr==(void*)-1) {
			perror("data error");
			exit(1);
		}
	}
	sizePtr[0] = hashed;
	sizePtr[1] = size;
	int ii=0;
	for (list<chunk>::iterator it=chunks.begin(); it!=chunks.end(); ii+=it->size, ++it)
		memcpy(dataPtr+ii, it->data, it->size);
	handleKeys(key, SET);
	shmdt(sizePtr);
	shmdt(dataPtr);
}

void get(string key, int hashed, int action){
	int sizeId = shmget(hashed, 2*sizeof(int), 0);
	if (sizeId<0) {
		cerr << "key unused\n";
		exit(1);
	}
	int *sizePtr = (int*) shmat(sizeId, NULL, 0);
	if (sizePtr==(void*)-1) {
		cerr << "metadata error\n";
		exit(1);
	}
	if (sizePtr[0] != hashed){
		cerr << key << "key unused\n";
		shmctl(sizeId, IPC_RMID, NULL);
		return;
	}
	int size = sizePtr[1];

	//return size if checking
	if (action == CHECK){
		cout << size << endl;
		exit(0);
	}

	int dataId = shmget(hashed+1, size, 0);
	if (dataId<0) {
		perror("metadata error");
		exit(1);
	}
	byte *dataPtr = (byte*) shmat(dataId, NULL, 0);
	if (dataPtr==(void*)-1) {
		perror("data error");
		exit(1);
	}

	//retrieve value
	if (action == GET || action == POP)
			write(1, dataPtr, size);
	shmdt(sizePtr);
	shmdt(dataPtr);

	//delete
	if (action == DELETE || action == POP){
		shmctl(sizeId, IPC_RMID, NULL);
		shmctl(dataId, IPC_RMID, NULL);
		handleKeys(key, DELETE);
	}
}

vector<string> splitter(char* input) {
	vector<string> split;
	string text = string(input);
	if (text.length()>0) text.pop_back();
	boost::split(split, text, [](char c){return c == ':';});
	return split;
}

void handleKeys(string key, int action){
	//attach to metadata
	int metaId = shmget(metaShmKey, 10016, IPC_EXCL | IPC_CREAT | 0666);
	int init = 0;
	if (metaId<0) {
		metaId = shmget(metaShmKey, 10016, 0);
		if (metaId<0) {cerr<<"metakey error\n";exit(1);}
	} else { init = 1; }
	metadata *keys = (metadata*) shmat(metaId, NULL, 0);
	if (keys==(void*)-1) {cerr<<"metakey list error\n";exit(1);}
	hash<string> hasher;
	int hashed;
	int i=0;
	vector<string> split;

	//initialize
	if (init) {
		keys->size = 0;
		memset(keys->list, 0, listsize);
	}

	switch (action) {

	//add key
	case SET:
		i = strlen(keys->list);
		if (i + key.length() > 9999) {
			cerr << "no more key space\n";
			exit(1);
		}
		sprintf(keys->list+i, (key+":").c_str());
		break;

	//list or delete all keys
	case SHOW:
	case CLEAR:
		split = splitter(keys->list);
		for (uint ii=0; ii<split.size(); ii++){
			if (split[ii] == "") continue;
			switch (action){
			case SHOW:
				cout << split[ii] << endl;
				break;
			case CLEAR:
				hashed = hasher(split[ii]);
				get(split[ii], hashed, 3);
			}
		}
		if (action == CLEAR){
			keys->size = 0;
			memset(keys->list, 0, listsize);
		}
		break;

	//remove single key
	case DELETE:
		split = splitter(keys->list);
		string S("");
		for (uint ii=0; ii<split.size(); ii++)
			if (split[ii] != key) S += (split[ii] + ":");
		strncpy(keys->list, S.c_str(), listsize);
	}
	shmdt(keys);
}

int main(int argc, char**argv){
	string key, command;
	list<chunk> chunks;
	hash<string> hasher;

	if (argc == 2 && string(argv[1]) == "clear") {
		handleKeys("",CLEAR);
		exit(0);
	} else if (argc == 2 && string(argv[1]) == "show") {
		handleKeys("",SHOW);
		exit(0);
	} else if (argc == 3) {
		command = string(argv[1]);
		key = string(argv[2]);
	} else{
		cerr << "usage: " << argv[0] << " <set | get | pop | up | chk | del | clear> <key>\n"
			 << "	   When using set or up, pipe input to stdin\n";
		exit(1);
	}

	int hashed = hasher(key);
	int action = commands[command];
	switch (action){
		case SET:
		case UPDATE:
			if (key.find(':') != string::npos) {
				cerr << "key cannot contain ':'\n";
				exit(1);
			}
			char buf[chunksize];
			int n, size;
			chunk temp;
			while((n = read(0,buf,(long)sizeof(buf))) > 0){
				temp.size = n;
				size += n;
				memcpy(temp.data, buf, n);
				chunks.push_back(temp);
			}
			set(key, chunks, size, hashed, action);
			break;
		case GET:
		case POP:
		case CHECK:
		case DELETE:
			get(key, hashed, action);
			break;
		default:
			cerr << "Invalid command: " << command << endl;
			exit(1);
			break;
	}
	return 0;
}
