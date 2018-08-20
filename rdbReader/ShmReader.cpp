// ShmReader.cpp : Sample implementation of a process reading
// from a shared memory segment (double buffered) with RDB layout
// (c) 2016 by VIRES Simulationstechnologie GmbH
// Provided AS IS without any warranty!
//

#include <stdlib.h>
#include <stdio.h>
#include <sys/shm.h>
#include <string.h>
#include <unistd.h>
#include "RDBHandler.hh"

// forward declarations of methods

/**
* method for checking the contents of the SHM
*/
int  checkShm();
void openShm();

/**
* routine for handling an RDB message; to be provided by user;
* here, only a printing of the message is performed
* @param msg    pointer to the message that is to be handled
*/
void handleMessage( RDB_MSG_t* msg );


/**
* some global variables, considered "members" of this example
*/
unsigned int mShmKey       = RDB_SHM_ID_IMG_GENERATOR_OUT;      // key of the SHM segment
unsigned int mCheckMask    = RDB_SHM_BUFFER_FLAG_TC;
void*        mShmPtr       = 0;                                 // pointer to the SHM segment
size_t       mShmTotalSize = 0;                                 // remember the total size of the SHM segment
bool         mVerbose      = false;                             // run in verbose mode?
int          mForceBuffer  = -1;                                // force reading one of the SHM buffers (0=A, 1=B)

/**
* information about usage of the software
* this method will exit the program
*/
void usage()
{
    printf("usage: shmReader [-k:key] [-c:checkMask] [-v] [-f:bufferId]\n\n");
    printf("       -k:key        SHM key that is to be addressed\n");
    printf("       -c:checkMask  mask against which to check before reading an SHM buffer\n");
    printf("       -f:bufferId   force reading of a given buffer (0 or 1) instead of checking for a valid checkMask\n");
    printf("       -v            run in verbose mode\n");
    exit(1);
}

/**
* validate the arguments given in the command line
*/
void ValidateArgs(int argc, char **argv)
{
    for( int i = 1; i < argc; i++)
    {
        if ((argv[i][0] == '-') || (argv[i][0] == '/'))
        {
            switch (tolower(argv[i][1]))
            {
                case 'k':        // shared memory key
                    if ( strlen( argv[i] ) > 3 )
                        mShmKey = atoi( &argv[i][3] );
                    break;
                    
                case 'c':       // check mask
                    if ( strlen( argv[i] ) > 3 )
                        mCheckMask = atoi( &argv[i][3] );
                    break;
                    
                case 'f':       // force reading a given buffer
                    if ( strlen( argv[i] ) > 3 )
                        mForceBuffer = atoi( &argv[i][3] );
                    break;
                    
                case 'v':       // verbose mode
                    mVerbose = true;
                    break;
                    
                default:
                    usage();
                    break;
            }
        }
    }
    
    fprintf( stderr, "ValidateArgs: key = 0x%x, checkMask = 0x%x, mForceBuffer = %d\n", 
                     mShmKey, mCheckMask, mForceBuffer );
}

/**
* main program with high frequency loop for checking the shared memory;
* does nothing else
*/

int main(int argc, char* argv[])
{
    // Parse the command line
    //
    ValidateArgs(argc, argv);
    
    // first: open the shared memory (try to attach without creating a new segment)
    
    fprintf( stderr, "attaching to shared memory....\n" );
    
    while ( !mShmPtr )
    {
        openShm();
        usleep( 100000 );     // do not overload the CPU
    }
    
    fprintf( stderr, "...attached! Reading now...\n" );
    
    // now check the SHM for the time being
    while ( 1 )
    {
        checkShm();
        
        usleep( 1000 );
    }
}

/**
* open the shared memory segment
*/
void openShm()
{
    // do not open twice!
    if ( mShmPtr )
        return;
        
    int shmid = 0; 

    if ( ( shmid = shmget( mShmKey, 0, 0 ) ) < 0 )
        return;

    if ( ( mShmPtr = (char *)shmat( shmid, (char *)0, 0 ) ) == (char *) -1 )
    {
        perror("openShm: shmat()");
        mShmPtr = 0;
    }

    if ( mShmPtr )
    {
        struct shmid_ds sInfo;

        if ( ( shmid = shmctl( shmid, IPC_STAT, &sInfo ) ) < 0 )
            perror( "openShm: shmctl()" );
        else
            mShmTotalSize = sInfo.shm_segsz;
    }
}

int checkShm()
{
    if ( !mShmPtr )
        return 0;

    // get a pointer to the shm info block
    RDB_SHM_HDR_t* shmHdr = ( RDB_SHM_HDR_t* ) ( mShmPtr );

    if ( !shmHdr )
        return 0;

    if ( ( shmHdr->noBuffers != 2 ) )
    {
        fprintf( stderr, "checkShm: no or wrong number of buffers in shared memory. I need two buffers!" );
        return 0;
    }

    // allocate space for the buffer infos
    RDB_SHM_BUFFER_INFO_t** pBufferInfo = ( RDB_SHM_BUFFER_INFO_t** ) ( new char [ shmHdr->noBuffers * sizeof( RDB_SHM_BUFFER_INFO_t* ) ] );
    RDB_SHM_BUFFER_INFO_t*  pCurrentBufferInfo = 0;

    char* dataPtr = ( char* ) shmHdr;
    dataPtr += shmHdr->headerSize;

    for ( int i = 0; i < shmHdr->noBuffers; i++ )
    {
        pBufferInfo[ i ] = ( RDB_SHM_BUFFER_INFO_t* ) dataPtr;
        dataPtr += pBufferInfo[ i ]->thisSize;
    }

    // get the pointers to message section in each buffer
    RDB_MSG_t* pRdbMsgA = ( RDB_MSG_t* ) ( ( ( char* ) mShmPtr ) + pBufferInfo[0]->offset );
    RDB_MSG_t* pRdbMsgB = ( RDB_MSG_t* ) ( ( ( char* ) mShmPtr ) + pBufferInfo[1]->offset );
    
    // pointer to the message that will actually be read
    RDB_MSG_t* pRdbMsg  = 0;

    // remember the flags that are set for each buffer
    unsigned int flagsA = pBufferInfo[ 0 ]->flags;
    unsigned int flagsB = pBufferInfo[ 1 ]->flags;

    // check whether any buffer is ready for reading (checkMask is set (or 0) and buffer is NOT locked)
    bool readyForReadA = ( ( flagsA & mCheckMask ) || !mCheckMask ) && !( flagsA & RDB_SHM_BUFFER_FLAG_LOCK );
    bool readyForReadB = ( ( flagsB & mCheckMask ) || !mCheckMask ) && !( flagsB & RDB_SHM_BUFFER_FLAG_LOCK );

    if ( mVerbose )
    {
        fprintf( stderr, "ImageReader::checkShm: before processing SHM\n" );
        fprintf( stderr, "ImageReader::checkShm: Buffer A: frameNo = %06d, flags = 0x%x, locked = <%s>, lock mask set = <%s>, readyForRead = <%s>\n", 
                         pRdbMsgA->hdr.frameNo, 
                         flagsA,
                         ( flagsA & RDB_SHM_BUFFER_FLAG_LOCK ) ? "true" : "false",
                         ( flagsA & mCheckMask ) ? "true" : "false",
                         readyForReadA ?  "true" : "false" );

        fprintf( stderr, "                       Buffer B: frameNo = %06d, flags = 0x%x, locked = <%s>, lock mask set = <%s>, readyForRead = <%s>\n", 
                         pRdbMsgB->hdr.frameNo, 
                         flagsB,
                         ( flagsB & RDB_SHM_BUFFER_FLAG_LOCK ) ? "true" : "false",
                         ( flagsB & mCheckMask ) ? "true" : "false",
                         readyForReadB ?  "true" : "false" );
    }

    if ( mForceBuffer < 0 )  // auto-select the buffer if none is forced to be read
    {
        // check which buffer to read
        if ( ( readyForReadA ) && ( readyForReadB ) )
        {
            if ( pRdbMsgA->hdr.frameNo > pRdbMsgB->hdr.frameNo )        // force using the latest image!!
            {
                pRdbMsg            = pRdbMsgA; 
                pCurrentBufferInfo = pBufferInfo[ 0 ];
            }
            else
            {
                pRdbMsg            = pRdbMsgB; 
                pCurrentBufferInfo = pBufferInfo[ 1 ];
            }
        }
        else if ( readyForReadA )
        {
            pRdbMsg            = pRdbMsgA; 
            pCurrentBufferInfo = pBufferInfo[ 0 ];
        }
        else if ( readyForReadB )
        {
            pRdbMsg            = pRdbMsgB;
            pCurrentBufferInfo = pBufferInfo[ 1 ];
        }
    }
    else if ( ( mForceBuffer == 0 ) && readyForReadA )   // force reading buffer A
    {
        pRdbMsg            = pRdbMsgA; 
        pCurrentBufferInfo = pBufferInfo[ 0 ];
    }
    else if ( ( mForceBuffer == 1 ) && readyForReadB ) // force reading buffer B
    {
        pRdbMsg            = pRdbMsgB;
        pCurrentBufferInfo = pBufferInfo[ 1 ];
    }
    
    // lock the buffer that will be processed now (by this, no other process will alter the contents)
    if ( pCurrentBufferInfo )
        pCurrentBufferInfo->flags |= RDB_SHM_BUFFER_FLAG_LOCK;

    // no data available?
    if ( !pRdbMsg || !pCurrentBufferInfo )
    {
        delete pBufferInfo;
        pBufferInfo = 0;

        // return with valid result if simulation is not yet running
        if ( ( pRdbMsgA->hdr.frameNo == 0 ) && ( pRdbMsgB->hdr.frameNo == 0 ) )
            return 1;

        // otherwise return a failure
        return 0;
    }

    // handle all messages in the buffer
    if ( !pRdbMsg->hdr.dataSize )
    {
        fprintf( stderr, "checkShm: zero message data size, error.\n" );
        return 0;
    }
    
    unsigned int maxReadSize = pCurrentBufferInfo->bufferSize;
    
    while ( 1 )
    {
        // handle the message that is contained in the buffer; this method should be provided by the user (i.e. YOU!)
        handleMessage( pRdbMsg );
        
        // do not read more bytes than there are in the buffer (avoid reading a following buffer accidentally)
        maxReadSize -= pRdbMsg->hdr.dataSize + pRdbMsg->hdr.headerSize;

        if ( maxReadSize < ( pRdbMsg->hdr.headerSize + pRdbMsg->entryHdr.headerSize ) )
            break;
            
        // go to the next message (if available); there may be more than one message in an SHM buffer!
        pRdbMsg = ( RDB_MSG_t* ) ( ( ( char* ) pRdbMsg ) + pRdbMsg->hdr.dataSize + pRdbMsg->hdr.headerSize );
        
        if ( !pRdbMsg )
            break;
            
        if ( pRdbMsg->hdr.magicNo != RDB_MAGIC_NO )
            break;
    }
    
    // release after reading
    pCurrentBufferInfo->flags &= ~mCheckMask;                   // remove the check mask
    pCurrentBufferInfo->flags &= ~RDB_SHM_BUFFER_FLAG_LOCK;     // remove the lock mask

    if ( mVerbose )
    {
        unsigned int flagsA = pBufferInfo[ 0 ]->flags;
        unsigned int flagsB = pBufferInfo[ 1 ]->flags;

        fprintf( stderr, "ImageReader::checkShm: after processing SHM\n" );
        fprintf( stderr, "ImageReader::checkShm: Buffer A: frameNo = %06d, flags = 0x%x, locked = <%s>, lock mask set = <%s>\n", 
                         pRdbMsgA->hdr.frameNo, 
                         flagsA,
                         ( flagsA & RDB_SHM_BUFFER_FLAG_LOCK ) ? "true" : "false",
                         ( flagsA & mCheckMask ) ? "true" : "false" );
        fprintf( stderr, "                       Buffer B: frameNo = %06d, flags = 0x%x, locked = <%s>, lock mask set = <%s>.\n", 
                         pRdbMsgB->hdr.frameNo, 
                         flagsB,
                         ( flagsB & RDB_SHM_BUFFER_FLAG_LOCK ) ? "true" : "false",
                         ( flagsB & mCheckMask ) ? "true" : "false" );
    }

    return 1;
}    

void handleMessage( RDB_MSG_t* msg )
{
    // just print the message
    Framework::RDBHandler::printMessage( msg );
}

