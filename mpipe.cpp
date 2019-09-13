#include <list>
#include <map>
#include <iostream>
#include <functional>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <string.h>
#define metaShmKey 875784987
#define chunksize 8192
#define keylistsize 10000
using namespace std;

enum { SET, UPDATE, SHOW, DELETE, DEL_NOKEY, CLEAR, GET, POP, CHECK };
void handleKeys(string, int);
typedef struct chunk {
	int size;
	byte data[chunksize];
} chunk ;
typedef struct metadata {
	char keylist[keylistsize];
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

void setVal(string key, list<chunk> chunks, int size, int hashed, int action){
	bool newkey = true;
	byte* dataPtr;
	int dataId = shmget(hashed, size, IPC_EXCL | IPC_CREAT | 0666);

	if (action == SET && dataId < 0){
		perror("key unavailable");
		exit(1);
	} else if (action == UPDATE && dataId < 0) {
		newkey = false;
		dataId = shmget(hashed, 0, 0);
		dataPtr = (byte*) shmat(dataId, NULL, 0);
		shmdt(dataPtr);
		shmctl(dataId, IPC_RMID, NULL);
		dataId = shmget(hashed, size, IPC_CREAT | IPC_EXCL | 0666);
		if (dataId<0) {
			perror("key error");
			exit(1);
		}
	}

	dataPtr = (byte*) shmat(dataId, NULL, 0);
	if (dataPtr==(void*)-1) {
		perror("data access error");
		exit(1);
	}
	int ii=0;
	for (list<chunk>::iterator it=chunks.begin(); it!=chunks.end(); ii+=it->size, ++it)
		memcpy(dataPtr+ii, it->data, it->size);
	if (newkey)
		handleKeys(key, SET);
	shmdt(dataPtr);
}

void getVal(string key, int hashed, int action){
	int dataId = shmget(hashed, 0, 0);
	if (dataId<0) {
		cerr << "key unused\n";
		exit(1);
	}
	shmid_ds info;
	shmctl(dataId, IPC_STAT, &info);

	//return size if checking
	if (action == CHECK){
		cout << info.shm_segsz << endl;
		exit(0);
	}

	byte *dataPtr = (byte*) shmat(dataId, NULL, 0);
	if (dataPtr==(void*)-1) {
		perror("data access error");
		exit(1);
	}

	//retrieve value
	if (action == GET || action == POP)
		write(1, dataPtr, info.shm_segsz);
	shmdt(dataPtr);

	//delete
	switch (action) {
	case DELETE:
	case POP:
		shmctl(dataId, IPC_RMID, NULL);
		handleKeys(key, DELETE);
		break;
	case DEL_NOKEY:
		shmctl(dataId, IPC_RMID, NULL);
		break;
	}
}

void handleKeys(string key, int action){
	//attach to metadata
	int metaId = shmget(metaShmKey, sizeof(metadata), IPC_EXCL | IPC_CREAT | 0666);
	int init = 0;
	if (metaId<0) {
		metaId = shmget(metaShmKey, sizeof(metadata), 0);
		if (metaId<0) {
			cerr << "keylist key error\n";
			exit(1);
		}
	} else {
		init = 1;
	}
	metadata *keys = (metadata*) shmat(metaId, NULL, 0);
	if (keys==(void*)-1) {
		cerr << "keylist error\n";
		exit(1);
	}
	hash<string> hasher;
	string S("");
	int hashed;
	int i=0;

	//initialize
	if (init)
		memset(keys->keylist, 0, keylistsize);

	switch (action) {

	//add key
	case SET:
		i = strlen(keys->keylist);
		if (i + key.length() >= keylistsize) {
			cerr << "no more key space\n";
			exit(1);
		}
		sprintf(keys->keylist+i, "%s:", key.c_str());
		break;

	//list or delete all keys
	case SHOW:
	case CLEAR:
		for (int i=0; keys->keylist[i] != 0; ++i)
			if (keys->keylist[i] == ':') {
				switch (action){
				case SHOW:
					cout << S << endl;
					break;
				case CLEAR:
					hashed = hasher(S);
					getVal(S, hashed, DEL_NOKEY);
				}
				S.clear();
			} else
				S.push_back(keys->keylist[i]);
		if (action == CLEAR)
			memset(keys->keylist, 0, keylistsize);
		break;

	//remove single key
	case DELETE:
		int i=0, j=0;
		for (; keys->keylist[i] != 0; ++i, ++j){
			if (keys->keylist[i] == ':') {
				if (key == S)
					j -= S.length()+1;
				S.clear();
			} else
				S.push_back(keys->keylist[i]);
			if (j >= 0)
				keys->keylist[j] = keys->keylist[i];
		}
		keys->keylist[j] = 0;
	}
	shmdt(keys);
}

int main(int argc, char**argv){
	string key, command;
	list<chunk> chunks;
	hash<string> hasher;

	if (argc > 1) command = string(argv[1]);
	if (argc == 2 && command == "clear") {
		handleKeys("",CLEAR);
		exit(0);
	} else if (argc == 2 && command == "show") {
		handleKeys("",SHOW);
		exit(0);
	} else if (argc == 3) {
		key = string(argv[2]);
	} else{
		cout << "usage: " << argv[0] << " <set | get | pop | up | chk | del | clear> <key>\n"
			 << "	   When using set or up, send input to stdin\n";
		exit(1);
	}

	int hashed = hasher(key),
		action = commands[command],
		size = 0;

	switch (action){
		case SET:
		case UPDATE:
			if (key.find(':') != string::npos) {
				cerr << "key cannot contain ':'\n";
				exit(1);
			}
			chunk temp;
			while((temp.size = read(0, temp.data, sizeof(temp.data))) > 0){
				size += temp.size;
				chunks.push_back(temp);
			}
			setVal(key, chunks, size, hashed, action);
			break;
		case GET:
		case POP:
		case CHECK:
		case DELETE:
			getVal(key, hashed, action);
			break;
		default:
			cerr << "Invalid command: " << command << endl;
			exit(1);
			break;
	}
	return 0;
}
