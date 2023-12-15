#pragma once
#include <ntddk.h>

template<typename TLock>
struct AutoLock {
	AutoLock(TLock& lock) : _lock(lock) {
		_lock.Lock();
	}

	~AutoLock() {
		_lock.Unlock();
	}

private:
	TLock& _lock;
};

// Mutex:
class FastMutex {
public:
	void Init();

	void Lock();
	void Unlock();

private:
	FAST_MUTEX _mutex;
};

template<typename T>
struct FullItem {
	LIST_ENTRY Entry;
	T Data;
};

struct Globals {
	LIST_ENTRY ItemsHead;
	int ItemCount;
	FastMutex Mutex;
};


void FastMutex::Init() {
	ExInitializeFastMutex(&_mutex);
}

void FastMutex::Lock() {
	ExAcquireFastMutex(&_mutex);
}

void FastMutex::Unlock() {
	ExReleaseFastMutex(&_mutex);
}