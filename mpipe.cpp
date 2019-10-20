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
#define keysize 21
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
typedef struct entry {
	char key[keysize];
	byte data[];
} entry;
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
hash<string> hasher;

void setVal(string key, list<chunk> &chunks, int size, int hashed, int action, int tries){
	bool newkey = true;
	entry* dataPtr;
	int dataId = shmget(hashed, size+keysize, IPC_EXCL | IPC_CREAT | 0600);
	shmid_ds info;
	if (dataId < 0){
		dataId = shmget(hashed, 0, 0);
		dataPtr = (entry*) shmat(dataId, NULL, 0);
		if (dataPtr==(void*)-1) {
			perror("data access error");
			exit(1);
		}
		shmctl(dataId, IPC_STAT, &info);
		//key collision
		if (info.shm_segsz < keysize || string(dataPtr->key) != key){
			if (tries > 5) {
				cerr << "key unavailable1\n";
				exit(1);
			}
			shmdt(dataPtr);
			setVal(key, chunks, size, hashed+1, action, tries+1);
			exit(0);
		//updating preexisting value
		} else if (action == UPDATE) {
			newkey = false;
			dataId = shmget(hashed, 0, 0);
			dataPtr = (entry*) shmat(dataId, NULL, 0);
			shmdt(dataPtr);
			shmctl(dataId, IPC_RMID, NULL);
			dataId = shmget(hashed, size+keysize, IPC_CREAT | IPC_EXCL | 0600);
			if (dataId<0) {
				perror("key error");
				exit(1);
			}
		//key is taken and not updating
		} else {
			cerr << "key unavailable2\n";
			exit(1);
		}
	}

	dataPtr = (entry*) shmat(dataId, NULL, 0);
	if (dataPtr==(void*)-1) {
		perror("data access error");
		exit(1);
	}
	int i=0;
	for (list<chunk>::iterator it=chunks.begin(); it!=chunks.end(); i+=it->size, ++it)
		memcpy(dataPtr->data+i, it->data, it->size);
	sprintf(dataPtr->key, "%s", key.c_str());
	if (newkey)
		handleKeys(key, SET);
	shmdt(dataPtr);
}

void getVal(string key, int hashed, int action, int tries){
	shmid_ds info;
	entry* dataPtr;
	int dataId = shmget(hashed, 0, 0);
	if (dataId<0)
		goto trynext;
	dataPtr = (entry*) shmat(dataId, NULL, 0);
	if (dataPtr==(void*)-1) {
		perror("data access error");
		exit(1);
	}
	shmctl(dataId, IPC_STAT, &info);
	if (info.shm_segsz >= keysize && string(dataPtr->key) == key)
		goto foundit;
	else
		shmdt(dataPtr);

	trynext:
	if (tries > 5) {
		cerr << "key unused\n";
		exit(1);
	}
	getVal(key, hashed+1, action, tries+1);
	exit(0);
	foundit:

	//return size if checking
	if (action == CHECK){
		cout << info.shm_segsz-keysize << endl;
		exit(0);
	}

	//retrieve value
	if (action == GET || action == POP)
		write(1, dataPtr->data, info.shm_segsz-keysize);

	//delete
	switch (action) {
	case DELETE:
	case POP:
		handleKeys(key, DELETE);
	case DEL_NOKEY:
		memset(dataPtr, 0, info.shm_segsz);
		shmdt(dataPtr);
		shmctl(dataId, IPC_RMID, NULL);
		break;
	default:
		shmdt(dataPtr);
	}
}

void handleKeys(string key, int action){
	//attach to metadata
	int metaId = shmget(metaShmKey, sizeof(metadata), IPC_EXCL | IPC_CREAT | 0600);
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
					getVal(S, hashed, DEL_NOKEY, 0);
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
			if (key.size() >= 20) {
				cerr << "max key size is " << keysize+1 << endl;
				exit(1);
			}
			chunk temp;
			while((temp.size = read(0, temp.data, sizeof(temp.data))) > 0){
				size += temp.size;
				chunks.push_back(temp);
			}
			setVal(key, chunks, size, hashed, action, 0);
			break;
		case GET:
		case POP:
		case CHECK:
		case DELETE:
			getVal(key, hashed, action, 0);
			break;
		default:
			cerr << "Invalid command: " << command << endl;
			exit(1);
			break;
	}
	return 0;
}
