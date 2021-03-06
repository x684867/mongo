// mmap_win.cpp

/*    Copyright 2009 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "mongo/pch.h"

#include "mongo/db/d_concurrency.h"
#include "mongo/db/storage/durable_mapped_file.h"
#include "mongo/util/file_allocator.h"
#include "mongo/util/mmap.h"
#include "mongo/util/text.h"
#include "mongo/util/timer.h"

namespace mongo {

    static size_t fetchMinOSPageSizeBytes() {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        size_t minOSPageSizeBytes = si.dwPageSize;
        minOSPageSizeBytesTest(minOSPageSizeBytes);
        return minOSPageSizeBytes;
    }
    const size_t g_minOSPageSizeBytes = fetchMinOSPageSizeBytes();


    mutex mapViewMutex("mapView");
    ourbitset writable;

    MAdvise::MAdvise(void *,unsigned, Advice) { }
    MAdvise::~MAdvise() { }

    const unsigned long long memoryMappedFileLocationFloor = 256LL * 1024LL * 1024LL * 1024LL;
    static unsigned long long _nextMemoryMappedFileLocation = memoryMappedFileLocationFloor;

    static SimpleMutex _nextMemoryMappedFileLocationMutex("nextMemoryMappedFileLocationMutex");

    unsigned long long AlignNumber(unsigned long long number, unsigned long long granularity)
    {
        return (number + granularity - 1) & ~(granularity - 1);
    }

    static void* getNextMemoryMappedFileLocation(unsigned long long mmfSize) {
        if (4 == sizeof(void*)) {
            return 0;
        }
        SimpleMutex::scoped_lock lk(_nextMemoryMappedFileLocationMutex);

        static unsigned long long granularity = 0;

        if (0 == granularity) {
            SYSTEM_INFO systemInfo;
            GetSystemInfo(&systemInfo);
            granularity = static_cast<unsigned long long>(systemInfo.dwAllocationGranularity);
        }

        unsigned long long thisMemoryMappedFileLocation = _nextMemoryMappedFileLocation;

        int current_retry = 1;

        while (true) {
            MEMORY_BASIC_INFORMATION memInfo;

            if (VirtualQuery(reinterpret_cast<LPCVOID>(thisMemoryMappedFileLocation),
                &memInfo, sizeof(memInfo)) == 0) {
                DWORD gle = GetLastError();

                // If we exceed the limits of Virtual Memory 
                // - 8TB before Windows 8.1/2012 R2, 128 TB after
                // restart scanning from our memory mapped floor once more
                // This is a linear scan of regions, not of every VM page
                if (gle == ERROR_INVALID_PARAMETER && current_retry == 1) {
                    thisMemoryMappedFileLocation = memoryMappedFileLocationFloor;
                    ++current_retry;
                    continue;
                }

                log() << "VirtualQuery of " << thisMemoryMappedFileLocation
                    << " failed with error " << errnoWithDescription(gle);
                fassertFailed(17484);
            }

            // Free memory regions that we can use for memory map files
            // 1. Marked MEM_FREE, not MEM_RESERVE
            // 2. Marked as PAGE_NOACCESS, not anything else
            if (memInfo.Protect == PAGE_NOACCESS &&
                memInfo.State == MEM_FREE &&
                memInfo.RegionSize > mmfSize)
                break;

            thisMemoryMappedFileLocation = reinterpret_cast<unsigned long long>(memInfo.BaseAddress)
                + memInfo.RegionSize;
        }

        _nextMemoryMappedFileLocation = thisMemoryMappedFileLocation
            + AlignNumber(mmfSize, granularity);

        return reinterpret_cast<void*>(static_cast<uintptr_t>(thisMemoryMappedFileLocation));
    }

    /** notification on unmapping so we can clear writable bits */
    void MemoryMappedFile::clearWritableBits(void *p) {
        for( unsigned i = ((size_t)p)/ChunkSize; i <= (((size_t)p)+len)/ChunkSize; i++ ) {
            writable.clear(i);
            verify( !writable.get(i) );
        }
    }

    MemoryMappedFile::MemoryMappedFile()
        : _flushMutex(new mutex("flushMutex")), _uniqueId(0) {
        fd = 0;
        maphandle = 0;
        len = 0;
        created();
    }

    void MemoryMappedFile::close() {
        LockMongoFilesShared::assertExclusivelyLocked();
        for( vector<void*>::iterator i = views.begin(); i != views.end(); i++ ) {
            clearWritableBits(*i);
            UnmapViewOfFile(*i);
        }
        views.clear();
        if ( maphandle )
            CloseHandle(maphandle);
        maphandle = 0;
        if ( fd )
            CloseHandle(fd);
        fd = 0;
        destroyed(); // cleans up from the master list of mmaps
    }

    unsigned long long mapped = 0;

    void* MemoryMappedFile::createReadOnlyMap() {
        verify( maphandle );

        scoped_lock lk(mapViewMutex);

        void* readOnlyMapAddress = NULL;
        int current_retry = 0;

        while (true) {

            LPVOID thisAddress = getNextMemoryMappedFileLocation(len);

            readOnlyMapAddress = MapViewOfFileEx(
                maphandle,          // file mapping handle
                FILE_MAP_READ,      // access
                0, 0,               // file offset, high and low
                0,                  // bytes to map, 0 == all
                thisAddress);       // address to place file

            if (0 == readOnlyMapAddress) {
                DWORD dosError = GetLastError();

                ++current_retry;

                // If we failed to allocate a memory mapped file, try again in case we picked
                // an address that Windows is also trying to use for some other VM allocations
                if (dosError == ERROR_INVALID_ADDRESS && current_retry < 5) {
                    continue;
                }

                log() << "MapViewOfFileEx for " << filename()
                    << " at address " << thisAddress
                    << " failed with error " << errnoWithDescription(dosError)
                    << " (file size is " << len << ")"
                    << " in MemoryMappedFile::createReadOnlyMap"
                    << endl;

                fassertFailed(16165);
            }

            break;
        }

        views.push_back( readOnlyMapAddress );
        return readOnlyMapAddress;
    }

    void* MemoryMappedFile::map(const char *filenameIn, unsigned long long &length, int options) {
        verify( fd == 0 && len == 0 ); // can't open more than once
        setFilename(filenameIn);
        FileAllocator::get()->allocateAsap( filenameIn, length );
        /* big hack here: Babble uses db names with colons.  doesn't seem to work on windows.  temporary perhaps. */
        char filename[256];
        strncpy(filename, filenameIn, 255);
        filename[255] = 0;
        {
            size_t len = strlen( filename );
            for ( size_t i=len-1; i>=0; i-- ) {
                if ( filename[i] == '/' ||
                        filename[i] == '\\' )
                    break;

                if ( filename[i] == ':' )
                    filename[i] = '_';
            }
        }

        updateLength( filename, length );

        {
            DWORD createOptions = FILE_ATTRIBUTE_NORMAL;
            if ( options & SEQUENTIAL )
                createOptions |= FILE_FLAG_SEQUENTIAL_SCAN;
            DWORD rw = GENERIC_READ | GENERIC_WRITE;
            fd = CreateFileW(
                     toWideString(filename).c_str(),
                     rw, // desired access
                     FILE_SHARE_WRITE | FILE_SHARE_READ, // share mode
                     NULL, // security
                     OPEN_ALWAYS, // create disposition
                     createOptions , // flags
                     NULL); // hTempl
            if ( fd == INVALID_HANDLE_VALUE ) {
                DWORD dosError = GetLastError();
                log() << "CreateFileW for " << filename
                        << " failed with " << errnoWithDescription( dosError )
                        << " (file size is " << length << ")"
                        << " in MemoryMappedFile::map"
                        << endl;
                return 0;
            }
        }

        mapped += length;

        {
            DWORD flProtect = PAGE_READWRITE; //(options & READONLY)?PAGE_READONLY:PAGE_READWRITE;
            maphandle = CreateFileMappingW(fd, NULL, flProtect,
                                          length >> 32 /*maxsizehigh*/,
                                          (unsigned) length /*maxsizelow*/,
                                          NULL/*lpName*/);
            if ( maphandle == NULL ) {
                DWORD dosError = GetLastError();
                log() << "CreateFileMappingW for " << filename
                        << " failed with " << errnoWithDescription( dosError )
                        << " (file size is " << length << ")"
                        << " in MemoryMappedFile::map"
                        << endl;
                close();
                fassertFailed( 16225 );
            }
        }

        void *view = 0;
        {
            scoped_lock lk(mapViewMutex);
            DWORD access = ( options & READONLY ) ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS;

            int current_retry = 0;
            while (true) {

                LPVOID thisAddress = getNextMemoryMappedFileLocation(length);

                view = MapViewOfFileEx(
                    maphandle,      // file mapping handle
                    access,         // access
                    0, 0,           // file offset, high and low
                    0,              // bytes to map, 0 == all
                    thisAddress);  // address to place file

                if (view == 0) {
                    DWORD dosError = GetLastError();

                    ++current_retry;

                    // If we failed to allocate a memory mapped file, try again in case we picked
                    // an address that Windows is also trying to use for some other VM allocations
                    if (dosError == ERROR_INVALID_ADDRESS && current_retry < 5) {
                        continue;
                    }

                    log() << "MapViewOfFileEx for " << filename
                        << " at address " << thisAddress
                        << " failed with " << errnoWithDescription(dosError)
                        << " (file size is " << length << ")"
                        << " in MemoryMappedFile::map"
                        << endl;

                    close();
                    fassertFailed(16166);
                }

                break;
            }
        }

        views.push_back(view);
        len = length;
        return view;
    }

    extern mutex mapViewMutex;

    __declspec(noinline) void makeChunkWritable(size_t chunkno) { 
        scoped_lock lk(mapViewMutex);

        if( writable.get(chunkno) ) // double check lock
            return;

        // remap all maps in this chunk.  common case is a single map, but could have more than one with smallfiles or .ns files
        size_t chunkStart = chunkno * MemoryMappedFile::ChunkSize;
        size_t chunkNext = chunkStart + MemoryMappedFile::ChunkSize;

        scoped_lock lk2(privateViews._mutex());
        map<void*,DurableMappedFile*>::iterator i = privateViews.finditer_inlock((void*) (chunkNext-1));
        while( 1 ) {
            const pair<void*,DurableMappedFile*> x = *(--i);
            DurableMappedFile *mmf = x.second;
            if( mmf == 0 )
                break;

            size_t viewStart = (size_t) x.first;
            size_t viewEnd = (size_t) (viewStart + mmf->length());
            if( viewEnd <= chunkStart )
                break;

            size_t protectStart = max(viewStart, chunkStart);
            dassert(protectStart<chunkNext);

            size_t protectEnd = min(viewEnd, chunkNext);
            size_t protectSize = protectEnd - protectStart;
            dassert(protectSize>0&&protectSize<=MemoryMappedFile::ChunkSize);

            DWORD oldProtection;
            bool ok = VirtualProtect( reinterpret_cast<void*>( protectStart ),
                                      protectSize,
                                      PAGE_WRITECOPY,
                                      &oldProtection );
            if ( !ok ) {
                DWORD dosError = GetLastError();
                log() << "VirtualProtect for " << mmf->filename()
                        << " chunk " << chunkno
                        << " failed with " << errnoWithDescription( dosError )
                        << " (chunk size is " << protectSize
                        << ", address is " << hex << protectStart << dec << ")"
                        << " in mongo::makeChunkWritable, terminating"
                        << endl;
                fassertFailed( 16362 );
            }
        }

        writable.set(chunkno);
    }

    void* MemoryMappedFile::createPrivateMap() {
        verify( maphandle );

        scoped_lock lk(mapViewMutex);

        LPVOID thisAddress = getNextMemoryMappedFileLocation( len );

        void* privateMapAddress = NULL;
        int current_retry = 0;

        while (true) {

            privateMapAddress = MapViewOfFileEx(
                maphandle,          // file mapping handle
                FILE_MAP_READ,      // access
                0, 0,               // file offset, high and low
                0,                  // bytes to map, 0 == all
                thisAddress);      // address to place file

            if (privateMapAddress == 0) {
                DWORD dosError = GetLastError();

                ++current_retry;

                // If we failed to allocate a memory mapped file, try again in case we picked
                // an address that Windows is also trying to use for some other VM allocations
                if (dosError == ERROR_INVALID_ADDRESS && current_retry < 5) {
                    continue;
                }

                log() << "MapViewOfFileEx for " << filename()
                    << " failed with error " << errnoWithDescription(dosError)
                    << " (file size is " << len << ")"
                    << " in MemoryMappedFile::createPrivateMap"
                    << endl;

                fassertFailed(16167);
            }

            break;
        }

        clearWritableBits( privateMapAddress );
        views.push_back( privateMapAddress );
        return privateMapAddress;
    }

    void* MemoryMappedFile::remapPrivateView(void *oldPrivateAddr) {
        verify( Lock::isW() );

        LockMongoFilesExclusive lockMongoFiles;

        clearWritableBits(oldPrivateAddr);
        if( !UnmapViewOfFile(oldPrivateAddr) ) {
            DWORD dosError = GetLastError();
            log() << "UnMapViewOfFile for " << filename()
                    << " failed with error " << errnoWithDescription( dosError )
                    << " in MemoryMappedFile::remapPrivateView"
                    << endl;
            fassertFailed( 16168 );
        }

        void* newPrivateView = MapViewOfFileEx(
                maphandle,          // file mapping handle
                FILE_MAP_READ,      // access
                0, 0,               // file offset, high and low
                0,                  // bytes to map, 0 == all
                oldPrivateAddr );   // we want the same address we had before
        if ( 0 == newPrivateView ) {
            DWORD dosError = GetLastError();
            log() << "MapViewOfFileEx for " << filename()
                    << " failed with error " << errnoWithDescription( dosError )
                    << " (file size is " << len << ")"
                    << " in MemoryMappedFile::remapPrivateView"
                    << endl;
        }
        fassert( 16148, newPrivateView == oldPrivateAddr );
        return newPrivateView;
    }

    // prevent WRITETODATAFILES() from running at the same time as FlushViewOfFile()
    SimpleMutex globalFlushMutex("globalFlushMutex");

    class WindowsFlushable : public MemoryMappedFile::Flushable {
    public:
        WindowsFlushable( MemoryMappedFile* theFile,
                          void * view,
                          HANDLE fd,
                          const std::string& filename,
                          boost::shared_ptr<mutex> flushMutex )
            : _theFile(theFile), _view(view), _fd(fd), _filename(filename), _flushMutex(flushMutex)
        {}

        void flush() {
            if (!_view || !_fd)
                return;

            LockMongoFilesShared mmfilesLock;
            if ( MongoFile::getAllFiles().count( _theFile ) == 0 ) {
                // this was deleted while we were unlocked
                return;
            }

            SimpleMutex::scoped_lock _globalFlushMutex(globalFlushMutex);
            scoped_lock lk(*_flushMutex);

            int loopCount = 0;
            bool success = false;
            bool timeout = false;
            int dosError = ERROR_SUCCESS;
            const int maximumTimeInSeconds = 60 * 15;
            Timer t;
            while ( !success && !timeout ) {
                ++loopCount;
                success = FALSE != FlushViewOfFile( _view, 0 );
                if ( !success ) {
                    dosError = GetLastError();
                    if ( dosError != ERROR_LOCK_VIOLATION ) {
                        break;
                    }
                    timeout = t.seconds() > maximumTimeInSeconds;
                }
            }
            if ( success && loopCount > 1 ) {
                log() << "FlushViewOfFile for " << _filename
                        << " succeeded after " << loopCount
                        << " attempts taking " << t.millis()
                        << "ms" << endl;
            }
            else if ( !success ) {
                log() << "FlushViewOfFile for " << _filename
                        << " failed with error " << dosError
                        << " after " << loopCount
                        << " attempts taking " << t.millis()
                        << "ms" << endl;
                // Abort here to avoid data corruption
                fassert(16387, false);
            }

            success = FALSE != FlushFileBuffers(_fd);
            if (!success) {
                int err = GetLastError();
                out() << "FlushFileBuffers failed: " << errnoWithDescription( err )
                      << " file: " << _filename << endl;
                dataSyncFailedHandler();
            }
        }

        MemoryMappedFile* _theFile; // this may be deleted while we are running
        void * _view;
        HANDLE _fd;
        string _filename;
        boost::shared_ptr<mutex> _flushMutex;
    };

    void MemoryMappedFile::flush(bool sync) {
        uassert(13056, "Async flushing not supported on windows", sync);
        if( !views.empty() ) {
            WindowsFlushable f( this, viewForFlushing() , fd , filename() , _flushMutex);
            f.flush();
        }
    }

    MemoryMappedFile::Flushable * MemoryMappedFile::prepareFlush() {
        return new WindowsFlushable( this, viewForFlushing() , fd , filename() , _flushMutex );
    }

}
