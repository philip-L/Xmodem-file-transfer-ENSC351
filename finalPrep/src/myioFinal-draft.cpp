/* myioFinal-draft.cpp -- Dec. 12 -- Copyright 2017 Craig Scratchley and others */
#include <sys/socket.h>		// for AF_LOCAL, SOCK_STREAM
#include <thread>
#include <condition_variable>
#include <vector>
#include <cstring>			// for memcpy ?
#include <atomic>
#include <iostream>

/* Lock-free circular buffer.  This should be threadsafe if one thread is reading
 * and another is writing. */
template<class T>
class CircBuf {
	T *buf;
	/* If read_pos == write_pos, the buffer is empty.
	 *
	 * There will always be at least one position empty, as a completely full
	 * buffer (read_pos == write_pos) is indistinguishable from an empty buffer.
	 *
	 * Invariants: read_pos < size, write_pos < size. */
	unsigned size;
	std::atomic<unsigned> read_pos, write_pos;
public:
	CircBuf() {
		buf = nullptr;
		read_pos = write_pos = 0;
		size = 0;
	}

	~CircBuf() {
		delete[] buf;
	}

	void reserve( unsigned n ) {
		read_pos = write_pos = 0;
		delete[] buf;
		buf = nullptr;
		/* Reserve an extra byte.  We'll never fill more than n bytes; the extra
		 * byte is to guarantee that read_pos != write_pos when the buffer is full,
		 * since that would be ambiguous with an empty buffer. */
		if( n != 0 ) {
			size = n+1;
			buf = new T[size];
		}
		else
			size = 0;
	}

	void get_write_pointers( T *pPointers[2], unsigned pSizes[2] ) {
		// wcs -- I have re-ordered the below two lines to optimize the code.
		const int wpos = write_pos.load( std::memory_order_relaxed );
		const int rpos = read_pos.load( std::memory_order_acquire );   // acquire this as late as possible.
		if( rpos <= wpos ) {
			/* The buffer looks like "eeeeDDDDeeee" or "eeeeeeeeeeee" (e = empty, D = data). */
			pPointers[0] = buf + wpos;
			pPointers[1] = buf;
			pSizes[0] = size - wpos;
			pSizes[1] = rpos;
		} else if( rpos > wpos ) {
			/* The buffer looks like "DDeeeeeeeeDD" (e = empty, D = data). */
			pPointers[0] = buf + wpos;
			pPointers[1] = nullptr;
			pSizes[0] = rpos - wpos;
			pSizes[1] = 0;
		}
		/* Subtract 1, to account for the element that we never fill. */
		if( pSizes[1] )
			pSizes[1] -= 1;
		else
			pSizes[0] -= 1;
	}

	void get_read_pointers( T *pPointers[2], unsigned pSizes[2] ) {
		const int rpos = read_pos.load( std::memory_order_relaxed );
		const int wpos = write_pos.load( std::memory_order_acquire );
		if( rpos <= wpos ) {
			/* The buffer looks like "eeeeDDDDeeee" or "eeeeeeeeeeee" (e = empty, D = data). */
			pPointers[0] = buf + rpos;
			pPointers[1] = nullptr;
			pSizes[0] = wpos - rpos;
			pSizes[1] = 0;
		} else if( rpos > wpos ) {
			/* The buffer looks like "DDeeeeeeeeDD" (e = empty, D = data). */
			pPointers[0] = buf + rpos;
			pPointers[1] = buf;
			pSizes[0] = size - rpos;
			pSizes[1] = wpos;
		}
	}

	/* Write buffer_size elements from buffer into the circular buffer object,
	 * and advance the write pointer.  Return the number of elements that were
	 * able to be written.  If
	 * the data will not fit entirely, as much data as possible will be fit
	 * in. */
	unsigned write( const T *buffer, unsigned buffer_size ) {
		using std::min;
		using std::max;
		T *p[2];
		unsigned sizes[2];
		get_write_pointers( p, sizes );
		buffer_size = min( buffer_size, sizes[0] + sizes[1]); // max_write_size = sizes[0] + sizes[1];
		const int from_first = min( buffer_size, sizes[0] );
		std::memcpy( p[0], buffer, from_first*sizeof(T) );
		if( buffer_size > sizes[0] )
			std::memcpy( p[1], buffer+from_first, max(buffer_size-sizes[0], 0u)*sizeof(T) );
		write_pos.store( (write_pos.load( std::memory_order_relaxed ) + buffer_size) % size, std::memory_order_release );
		return buffer_size;
	}

	/* Read buffer_size elements into buffer from the circular buffer object,
	 * and advance the read pointer.  Return the number of elements that were
	 * read.  If buffer_size elements cannot be read, as many elements as
	 * possible will be read */
	unsigned read( T *buffer, unsigned buffer_size ) {
		using std::max;
		using std::min;
		T *p[2];
		unsigned sizes[2];
		get_read_pointers( p, sizes );
		buffer_size = min( buffer_size, sizes[0] + sizes[1]); // max_read_size = sizes[0] + sizes[1];
		const int from_first = min( buffer_size, sizes[0] );
		std::memcpy( buffer, p[0], from_first*sizeof(T) );
		if( buffer_size > sizes[0] )
			std::memcpy( buffer+from_first, p[1], max(buffer_size-sizes[0], 0u)*sizeof(T) );
		read_pos.store( (read_pos.load( std::memory_order_relaxed ) + buffer_size) % size, std::memory_order_release );
		return buffer_size;
	}
}; /* Copyright (c) 2004 Glenn Maynard.  See Craig for more details and original code. */

using namespace std;

class spinlock_mutex {
    atomic_flag flag;
public:
    spinlock_mutex():
        flag(ATOMIC_FLAG_INIT)
    {}
    void lock()
    {
        while(flag.test_and_set(memory_order_acquire));
    }
    bool try_lock()
    {
        return (!flag.test_and_set(memory_order_acquire));
    }
    void unlock()
    {
        flag.clear(memory_order_release);
    }
};

namespace{ //Unnamed (anonymous) namespace

class socketDrainClass;
typedef shared_ptr<socketDrainClass> socketDrainClassSP; // shared pointer
typedef vector<socketDrainClassSP > sdcVectType;
sdcVectType desInfoVect(3); // start with size of 3 due to stdin, stdout, stderr

// A spinlock_mutex used to protect desInfoVect so only a single thread can modify the vector at a
// time.  This spinlock_mutex is also used to prevent a socket from being closed
// at the beginning of a myReadcond, myWrite, or myTcdrain function.
spinlock_mutex vectMutex;

class socketDrainClass {
	int buffered = 0;
	CircBuf<char> buffer;
	condition_variable_any cvDrain;
	condition_variable_any cvRead;
	spinlock_mutex socketDrainMutex;
public:
	int pair; // Cannot be private because myWrite and myTcdrain using it

	socketDrainClass(unsigned pairInit)
	:buffered(0), pair(pairInit) {
		buffer.reserve(300); // note constant of 300
	}

	int waitForDraining(unique_lock<spinlock_mutex>& passedVectlk)
	{
		unique_lock<spinlock_mutex> condlk(socketDrainMutex);
		passedVectlk.unlock();
		if (buffered > 0) { // this if statement is optional
			//Wait for a reading thread to drain out the data
			cvDrain.wait(condlk, [this] {return buffered <= 0 || pair == -1;});
			if (pair == -1) {
				errno = EBADF; // check this error code
				return -1;
			}
		}
		return 0;
	}

	int writing(int des, const void* buf, size_t nbyte)
	{
		lock_guard<spinlock_mutex> condlk(socketDrainMutex);
		int written = buffer.write((const char*) buf, nbyte);
		buffered += written;
		if (buffered >= 0)
			cvRead.notify_one();
		return written;
	}

	int reading(int des, void * buf, int n, int min, int time, int timeout, unique_lock<spinlock_mutex>& passedVectlk)
	{
		int bytesRead;
		unique_lock<spinlock_mutex> condlk(socketDrainMutex);
		passedVectlk.unlock();
		if (buffered >= min || pair == -1) {
			bytesRead = buffer.read((char *) buf, n);
			buffered -= bytesRead;
			if (!buffered)
				cvDrain.notify_all();
		}
		else {
			if (buffered < 0) {
				cerr << "Currently only supporting one reading call at a time" << endl;
				exit(EXIT_FAILURE);
			}
			if (time != 0 || timeout != 0) {
				cerr << "Currently only supporting no timeouts or immediate timeout" << endl;
				exit(EXIT_FAILURE);
			}
			buffered -= min;
			cvDrain.notify_all(); // buffered must now be <= 0

			cvRead.wait(condlk, [this] {return (buffered) >= 0 || pair == -1;});
			if (pair != -1 && desInfoVect[pair]->pair == -1) {
				errno = EBADF; return -1;
			}
			bytesRead = buffer.read((char *) buf, n);
			buffered -= (bytesRead - min);
		}
		return bytesRead;
	}

	int closing(int des)
	{
		// vectMutex already locked at this point.
		if (pair != -1) {
			socketDrainClassSP des_pair = desInfoVect[pair];
			unique_lock<spinlock_mutex> condPairlk(des_pair->socketDrainMutex, defer_lock);
			unique_lock<spinlock_mutex> condlk(socketDrainMutex, defer_lock);
			lock(condPairlk, condlk);
			des_pair->pair = -1;
			if (des_pair->buffered < 0) {
				// no more data will be written from des
				// notify thread waiting on reading on paired descriptor
				des_pair->cvRead.notify_all(); // We currently...
				// ... should not have more than one thread to notify.
			}
			else if (des_pair->buffered > 0) {
				// shouldn't be threads waiting in myTcdrain() on des, but just in case.
				des_pair->cvDrain.notify_all();
			}
			if (buffered > 0) {
				// by closing socket we are throwing away any buffered data. Notification
				// sent immediately below to myTcdrain waiters on paired descriptor
				buffered = 0;
				cvDrain.notify_all();
			}
			else if (buffered < 0) {
				// shouldn't be threads waiting in myReadcond() on des, but just in case.
				buffered = 0;
				cvRead.notify_all();
			}
		}
		// delete desInfoVect[des]; // not needed with Shared Pointers
		desInfoVect[des] = nullptr;
		return 0;
	}
};
} // unnamed namespace

int mySocketpair(int domain, int type, int protocol, int des[2]) {
	lock_guard<spinlock_mutex> vectlk(vectMutex);
	des[0] = desInfoVect.size();
	des[1] = desInfoVect.size()+1;
	desInfoVect.resize(desInfoVect.size()+2);
	// dynamically allocate a new instances of socketDrainClass
	desInfoVect[des[0]] = make_shared<socketDrainClass>(des[1]);
	desInfoVect[des[1]] = make_shared<socketDrainClass>(des[0]);
	return 0;
}

int myReadcond(int des, void * buf, int n, int min, int time, int timeout) {
	unique_lock<spinlock_mutex> vectlk(vectMutex);
	socketDrainClassSP desInfoSP = desInfoVect.at(des);
	if (!desInfoSP) {
		errno = EBADF; return -1;
	}
	return desInfoSP->reading(des, buf, n, min, time, timeout, vectlk);
	// the object pointed-to by desInfoSP might be
	//   automatically deleted now if des was closed by another thread during
	//	the call to reading() -- it depends on oustanding calls to myTcdrain().
}

ssize_t myWrite(int des, const void* buf, size_t nbyte) {
	unique_lock<spinlock_mutex> vectlk(vectMutex);
	//Check if the descriptor given has been closed yet
	socketDrainClassSP desInfoSP = desInfoVect.at(des);
	if(desInfoSP)
		if (desInfoSP->pair != -1)
			// locking vectMutex above makes sure that desinfoSP->pair is not closed at this instant.
			return desInfoVect[desInfoSP->pair]->writing(des, buf, nbyte);
		else {
			errno = EPIPE; return -1;
		}
	else {
		errno = EBADF; return -1;
	}
}

/*
 * Function:  make the calling thread wait for a reading thread to drain the data
 */
int myTcdrain(int des) {
	unique_lock<spinlock_mutex> vectlk(vectMutex);
	socketDrainClassSP desInfoSP = desInfoVect.at(des); // chose not to write desInfoVect[des]
	if(desInfoSP)
		if (desInfoSP->pair != -1)
			// locking vectMutex above makes sure that desinfoSP->pair is not closed here
			return desInfoVect[desInfoSP->pair]->waitForDraining(vectlk);
			// the object formerly pointed-to by desInfoVect[desInfoSP->pair] was just
			//	automatically deleted if desInfoSP->pair has now been closed by another thread
			//  and there are no oustanding calls to myReadcond specifying desInfoSP->pair for des
		else {
			errno = EPIPE; return -1;
		}
	else {
		errno = EBADF; return -1;
	}
}

int myClose(int des) {
	// lock vectMutex because we don't want to process myClose()
	//		at the wrong moment in another operation
	lock_guard<spinlock_mutex> vectlk(vectMutex);
	if(!desInfoVect.at(des)) {
		errno = EBADF;
		return -1;
	}
	return desInfoVect[des]->closing(des);
	// the object formerly pointed-to by desInfoVec[des] was just automatically deleted if
	//	no other thread is using it.
}

static int daSktPr[2];	  // Descriptor Array for Socket Pair

// experiment with your own threads and your own calls to the functions above.
// the below code will be changed for the final exam.

void threadT32Func(void)
{
	int RetVal;
    char	Ba[200];
	CircBuf<char> buffer;
	buffer.reserve(300); // note constant of 300
	buffer.write("123456789", 10);
	RetVal = buffer.read(Ba, 200);
	cout << "RetVal in primary: " << RetVal << " Ba: " << Ba << endl;

	mySocketpair(AF_LOCAL, SOCK_STREAM, 0, daSktPr);
	myWrite(daSktPr[0], "123456789", 10); // don't forget nul termination character
    RetVal = myReadcond(daSktPr[1], Ba, 200, 8, 0, 0);
	cout << "RetVal in primary: " << RetVal << " Ba: " << Ba << endl;
	myTcdrain(daSktPr[0]);
	myClose(daSktPr[0]);

	std::this_thread::sleep_for (std::chrono::milliseconds(100));
}

int main() {
	thread threadT32(threadT32Func);
	threadT32.join();
	return 0;
}
