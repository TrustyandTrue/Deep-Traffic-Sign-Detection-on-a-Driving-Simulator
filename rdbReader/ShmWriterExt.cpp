// ShmWriterExt.cpp : Sample implementation of a process sending
// triggers to IG every nth TC frame
// (c) 2016 by VIRES Simulationstechnologie GmbH
// Provided AS IS without any warranty!
//

#include <stdlib.h>
#include <stdio.h>
#include <sys/shm.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include "RDBHandler.hh"

#define DEFAULT_PORT        48190   /* for image port it should be 48192 */
#define DEFAULT_BUFFER      204800


// forward declarations of methods

/**
* open the network interface for sending trigger data
*/
void openNetwork();

/**
* make sure network data is being read
*/
void readNetwork();

/**
* open the shared memory segment for receiving IG images
*/
void openIgOutShm();

/**
* check and parse the contents of the IG images SHM
*/
int checkIgOutShm();

/**
* open the shared memory segment for sending IG trigger
*/
void openIgCtrlShm();

/**
* check and parse the contents of the shared memory
*/
int checkIgCtrlShm();

/**
* initialize the contents of the shared memory
*/
int initIgCtrlShm();

/**
* trigger the IG
*/
int writeTriggerToIgCtrlShm();

/**
* check for data that has to be read (otherwise socket will be clogged)
* @param descriptor socket descriptor
*/
int getNoReadyRead( int descriptor );

/**
* parse an RDB message (received via SHM or network)
* @param msg    pointer to message that shall be parsed
*/
void parseRDBMessage( RDB_MSG_t* msg );

/**
* parse an RDB message entry
* @param simTime    simulation time of the message
* @param simFrame   simulation frame of the message
* @param entryHdr   pointer to the entry header itself
*/
void parseRDBMessageEntry( const double & simTime, const unsigned int & simFrame, RDB_MSG_ENTRY_HDR_t* entryHdr );

/**
* routine for handling an RDB message; to be provided by user;
* here, only a printing of the message is performed
* @param msg    pointer to the message that is to be handled
*/
void handleMessage( RDB_MSG_t* msg );

/**
* send a trigger to the taskControl via network socket
* @param sendSocket socket descriptor
* @param simTime    internal simulation time
* @param simFrame   internal simulation frame
*/
void sendRDBTrigger( int & sendSocket, const double & simTime, const unsigned int & simFrame );

/**
* open all communication means
*/
void openCommunication();

/**
* get the current time
*/
double getTime();

/**
* calculate some statistics
*/
void calcStatistics();


/**
* some global variables, considered "members" of this example
*/
unsigned int mCheckMask        = RDB_SHM_BUFFER_FLAG_TC;
bool         mVerbose          = false;                             // run in verbose mode?
char         szServer[128];                                         // Server to connect to for RDB data
int          iPort             = DEFAULT_PORT;                      // Port on server to connect to
int          mClient           = -1;                                // client socket
unsigned int mSimFrame         = 0;                                 // simulation frame counter
double       mSimTime          = 0.0;                               // simulation time
double       mDeltaTime        = 0.01;                              // simulation step width
int          mLastShmFrame     = -1;
int          mLastNetworkFrame = -1;
int          mLastIGTriggerFrame = -1;
int          mLastImageId      = 0;
int          mTotalNoImages    = 0;

// stuff for triggering IG
unsigned int mIgCtrlShmKey       = RDB_SHM_ID_CONTROL_GENERATOR_IN;   // key of the SHM segment
void*        mIgCtrlShmPtr       = 0;                                 // pointer to the SHM segment
size_t       mIgCtrlShmTotalSize = 64 * 1024;                         // 64kB total size of SHM segment
Framework::RDBHandler mIgCtrlRdbHandler;                              // use the RDBHandler helper routines to handle 
                                                                      // the memory and message management
// stuff for reading images
unsigned int mIgOutShmKey       = RDB_SHM_ID_IMG_GENERATOR_OUT;      // key of the SHM segment
unsigned int mIgOutCheckMask    = RDB_SHM_BUFFER_FLAG_TC;
void*        mIgOutShmPtr       = 0;                                 // pointer to the SHM segment
int          mIgOutForceBuffer  = -1;
size_t       mIgOutShmTotalSize = 0;                                 // remember the total size of the SHM segment
                                                                     // the memory and message management
                                                                     
unsigned int mFrameNo        = 0;
unsigned int mFrameTime      = mDeltaTime; 
bool         mHaveImage      = false;                                                                     
bool         mHaveFirstImage = false;
bool         mHaveFirstFrame = false;
bool         mCheckForImage  = false;

// some stuff for performance measurement
double       mStartTime = -1.0;

/**
* information about usage of the software
* this method will exit the program
*/
void usage()
{
    printf("usage: videoTest [-k:key] [-c:checkMask] [-v] [-f:bufferId] [-p:x] [-s:IP] [-h]\n\n");
    printf("       -k:key        SHM key that is to be addressed\n");
    printf("       -c:checkMask  mask against which to check before reading an SHM buffer\n");
    printf("       -p:x          Remote port to send to\n");
    printf("       -s:IP         Server's IP address or hostname\n");
    printf("       -v            run in verbose mode\n");
    exit(1);
}

/**
* validate the arguments given in the command line
*/
void ValidateArgs(int argc, char **argv)
{
    // initalize the server variable
    strcpy( szServer, "127.0.0.1" );

    for( int i = 1; i < argc; i++)
    {
        if ((argv[i][0] == '-') || (argv[i][0] == '/'))
        {
            switch (tolower(argv[i][1]))
            {
                case 'k':        // shared memory key
                    if ( strlen( argv[i] ) > 3 )
                        sscanf( &argv[i][3], "0x%x", &mIgCtrlShmKey );
                    break;
                    
                case 'c':       // check mask
                    if ( strlen( argv[i] ) > 3 )
                        mCheckMask = atoi( &argv[i][3] );
                    break;
                    
                case 'v':       // verbose mode
                    mVerbose = true;
                    break;
                    
                case 'p':        // Remote port
                    if (strlen(argv[i]) > 3)
                        iPort = atoi(&argv[i][3]);
                    break;
                case 's':       // Server
                    if (strlen(argv[i]) > 3)
                        strcpy(szServer, &argv[i][3]);
                    break;

                case 'h':
                default:
                    usage();
                    break;
            }
        }
    }
    
    fprintf( stderr, "ValidateArgs: key = 0x%x, checkMask = 0x%x\n", 
                     mIgCtrlShmKey, mCheckMask );
}

/**
* main program with high frequency loop for checking the shared memory;
* does nothing else
*/

int main(int argc, char* argv[])
{
    // Parse the command line
    ValidateArgs(argc, argv);
    
    // open the communication ports
    openCommunication();
    
    // now check the SHM for the time being
    while ( 1 )
    {
        int lastSimFrame = mLastNetworkFrame;
        
        // first read network messages
        readNetwork();
        
        bool haveNewFrame = ( lastSimFrame != mLastNetworkFrame );
        
        // now read IG output
        if ( mCheckForImage )
        {
            checkIgOutShm();
            
            mCheckForImage = !mHaveImage;
        }
        
        if ( haveNewFrame )
        {
            fprintf( stderr, "main: new simulation frame (%d) available, mLastIGTriggerFrame = %d\n", 
                             mLastNetworkFrame, mLastIGTriggerFrame );
                             
            mHaveFirstFrame = true;
        }
            
        if ( mLastNetworkFrame >= ( mLastIGTriggerFrame + 3 ) ) // create an image only every 3rd network frame
        {
            usleep( 5000 );
            
            // request image and wait for it
            while ( !writeTriggerToIgCtrlShm() )
            {
                usleep( 100 );
            }
            mLastIGTriggerFrame = mLastNetworkFrame;
            
            mHaveImage     = false;
            mCheckForImage = true;
            
            // wait for the image before proceeding
            haveNewFrame = false;
        }
        
        // has an image arrived or do the first frames need to be triggered 
        //(first image will arrive with a certain frame delay only)
        if ( !mHaveFirstImage || mHaveImage || haveNewFrame || !mHaveFirstFrame )
        {
            // do not initialize too fast
            if ( !mHaveFirstImage || !mHaveFirstFrame )
                usleep( 100000 );   // 10H

            sendRDBTrigger( mClient, mSimTime, mSimFrame );

            // increase internal counters
            mSimTime += mDeltaTime;
            mSimFrame++;
            
            mHaveImage = false;
            
            // calculate the timing statistics
            calcStatistics();
        }
        
        usleep( 10 );       // do not overload the CPU
    }
}

void openCommunication()
{
    // open the network connection to the taskControl (so triggers may be sent)
    fprintf( stderr, "openCommunication: creating network connection....\n" );
    openNetwork();  // this is blocking until the network has been opened
    
    // now: open the shared memory for IG control (try to attach without creating a new segment)
    fprintf( stderr, "openCommunication: attaching to shared memory (IG control) 0x%x....\n", mIgCtrlShmKey );

    while ( !mIgCtrlShmPtr )
    {
        openIgCtrlShm();
        usleep( 1000 );     // do not overload the CPU
    }
    
    fprintf( stderr, "...attached to IG control SHM! Initializing now...\n" );
    
    int retVal = initIgCtrlShm();
    
    fprintf( stderr, "...initialized (result = %d)!\n", retVal );

    // open the shared memory for IG image output (try to attach without creating a new segment)
    fprintf( stderr, "openCommunication: attaching to shared memory (IG image output) 0x%x....\n", mIgOutShmKey );

    while ( !mIgOutShmPtr )
    {
        openIgOutShm();
        usleep( 1000 );     // do not overload the CPU
    }
    
    fprintf( stderr, "...attached to IG image output SHM! Triggering now...\n" );
}

void openNetwork()
{
    struct sockaddr_in server;
    struct hostent    *host = NULL;

    // Create the socket, and attempt to connect to the server
    mClient = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    
    if ( mClient == -1 )
    {
        fprintf( stderr, "socket() failed: %s\n", strerror( errno ) );
        return;
    }
    
    int opt = 1;
    setsockopt ( mClient, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof( opt ) );

    server.sin_family      = AF_INET;
    server.sin_port        = htons(iPort);
    server.sin_addr.s_addr = inet_addr(szServer);
    
    // If the supplied server address wasn't in the form
    // "aaa.bbb.ccc.ddd" it's a hostname, so try to resolve it
    if ( server.sin_addr.s_addr == INADDR_NONE )
    {
        host = gethostbyname(szServer);
        if ( host == NULL )
        {
            fprintf( stderr, "Unable to resolve server: %s\n", szServer );
            return;
        }
        memcpy( &server.sin_addr, host->h_addr_list[0], host->h_length );
    }
    
	// wait for connection
	bool bConnected = false;

    while ( !bConnected )
    {
        if ( connect( mClient, (struct sockaddr *)&server, sizeof( server ) ) == -1 )
        {
            fprintf( stderr, "connect() failed: %s\n", strerror( errno ) );
            sleep( 1 );
        }
        else
            bConnected = true;
    }

    fprintf( stderr, "connected!\n" );
}

void readNetwork()
{
    static char*         szBuffer       = 0;
    int                  ret            = 0;
    static unsigned int  sBytesInBuffer = 0;
    static size_t        sBufferSize    = sizeof( RDB_MSG_HDR_t );
    static unsigned char *spData        = ( unsigned char* ) calloc( 1, sBufferSize );
    static int           sVerbose       = 0;
    
    if ( !szBuffer )
        szBuffer = new char[DEFAULT_BUFFER];  // allocate on heap
        
    // make sure this is non-bloekcing and read everything that's available!
    do
    {
        ret = 0;        // nothing read yet
        
        int noReady = getNoReadyRead( mClient );
    
        if ( noReady < 0 )
        {
            fprintf( stderr, "recv() failed: %s\n", strerror( errno ) );
            break;
        }

        if ( noReady > 0 )
        {
            // read data
            if ( ( ret = recv( mClient, szBuffer, DEFAULT_BUFFER, 0 ) ) != 0 )
            {
                // do we have to grow the buffer??
                if ( ( sBytesInBuffer + ret ) > sBufferSize )
                {
                    spData      = ( unsigned char* ) realloc( spData, sBytesInBuffer + ret );
                    sBufferSize = sBytesInBuffer + ret;
                }

                memcpy( spData + sBytesInBuffer, szBuffer, ret );
                sBytesInBuffer += ret;

                // already complete messagae?
                if ( sBytesInBuffer >= sizeof( RDB_MSG_HDR_t ) )
                {
                    RDB_MSG_HDR_t* hdr = ( RDB_MSG_HDR_t* ) spData;

                    // is this message containing the valid magic number?
                    if ( hdr->magicNo != RDB_MAGIC_NO )
                    {
                        fprintf( stderr, "message receiving is out of sync; discarding data" );
                        sBytesInBuffer = 0;
                    }
                    else
                    {
                        // handle all messages in the buffer before proceeding
                        while ( sBytesInBuffer >= ( hdr->headerSize + hdr->dataSize ) )
                        {
                            unsigned int msgSize = hdr->headerSize + hdr->dataSize;
 
                            // now parse the message
                            parseRDBMessage( ( RDB_MSG_t* ) spData );

                            // remove message from queue
                            memmove( spData, spData + msgSize, sBytesInBuffer - msgSize );
                            sBytesInBuffer -= msgSize;
                        }
                    }
                }
            }
        }
    } 
    while ( ret > 0 );
}

void handleMessage( RDB_MSG_t* msg )
{
    // just print the message
    parseRDBMessage( msg );
}

void parseRDBMessage( RDB_MSG_t* msg )
{
    if ( !msg )
      return;

    if ( !msg->hdr.dataSize )
        return;
    
    RDB_MSG_ENTRY_HDR_t* entry = ( RDB_MSG_ENTRY_HDR_t* ) ( ( ( char* ) msg ) + msg->hdr.headerSize );
    uint32_t remainingBytes    = msg->hdr.dataSize;
        
    while ( remainingBytes )
    {
        parseRDBMessageEntry( msg->hdr.simTime, msg->hdr.frameNo, entry );

        remainingBytes -= ( entry->headerSize + entry->dataSize );
        
        if ( remainingBytes )
          entry = ( RDB_MSG_ENTRY_HDR_t* ) ( ( ( ( char* ) entry ) + entry->headerSize + entry->dataSize ) );
    }
}

void parseRDBMessageEntry( const double & simTime, const unsigned int & simFrame, RDB_MSG_ENTRY_HDR_t* entryHdr )
{
    if ( !entryHdr )
        return;
    
    if ( entryHdr->pkgId == RDB_PKG_ID_END_OF_FRAME )   // check for end-of-frame only
    {
        mLastNetworkFrame = simFrame;
        return;
    }
    
    if ( entryHdr->pkgId == RDB_PKG_ID_IMAGE )   // check for end-of-frame only
    {
        RDB_IMAGE_t *myImg = ( RDB_IMAGE_t* )( ( ( char* ) entryHdr ) + entryHdr->headerSize );
        
        if ( myImg->id > 0 )
        {
            fprintf( stderr, "parseRDBMessageEntry: simframe = %d: have image no. %d\n", simFrame, myImg->id );
            
            if ( ( myImg->id > 3 ) && ( ( myImg->id - mLastImageId ) != 3 ) )
            {
                fprintf( stderr, "WARNING: parseRDBMessageEntry: delta of image ID out of bounds: delta = %d\n", myImg->id - mLastImageId );
            }

            mLastImageId    = myImg->id;
            mHaveImage      = true;
            mHaveFirstImage = true;
            mTotalNoImages++;
        }
    }
}

int getNoReadyRead( int descriptor )
{
    fd_set         fs;
    struct timeval time;
    
    // still no valid descriptor?
    if ( descriptor < 0 )
        return 0;

    FD_ZERO( &fs );
    FD_SET( descriptor, &fs );
    
    memset( &time, 0, sizeof( struct timeval ) );

    int retVal = 0;
    
    retVal = select( descriptor + 1, &fs, (fd_set*) 0, (fd_set*) 0, &time);
    
    if ( retVal > 0 )
        return FD_ISSET( descriptor, &fs );
    
    return retVal;
}

void sendRDBTrigger( int & sendSocket, const double & simTime, const unsigned int & simFrame )
{
    // is the socket available?
    if ( mClient < 0 )
        return;
                
    Framework::RDBHandler myHandler;

    // start a new message
    myHandler.initMsg();

    // add extended package for the object state
    RDB_TRIGGER_t *trigger = ( RDB_TRIGGER_t* ) myHandler.addPackage( simTime, simFrame, RDB_PKG_ID_TRIGGER, 1, true );

    if ( !trigger )
    {
        fprintf( stderr, "sendRDBTrigger: could not create trigger\n" );
        return;
    }
        
    trigger->frameNo = simFrame;
    trigger->deltaT  = mDeltaTime;
    
    fprintf( stderr, "sendRDBTrigger: sending trigger, deltaT = %.4lf\n", trigger->deltaT );
        
    int retVal = send( mClient, ( const char* ) ( myHandler.getMsg() ), myHandler.getMsgTotalSize(), 0 );

    if ( !retVal )
        fprintf( stderr, "sendRDBTrigger: could not send trigger\n" );
}

/**
* open the shared memory segment; create if necessary
*/
void openIgCtrlShm()
{
    // do not open twice!
    if ( mIgCtrlShmPtr )
        return;
        
    int shmid = 0; 
    int flag  = IPC_CREAT | 0777;
    
    // does the memory already exist?
    if ( ( shmid = shmget( mIgCtrlShmKey, 0, 0 ) ) < 0 )
    {
        // not yet there, so let's create the segment
        if ( ( shmid = shmget( mIgCtrlShmKey, mIgCtrlShmTotalSize, flag ) ) < 0 )
        {
            perror("openIgCtrlShm: shmget()");
            mIgCtrlShmPtr = 0;
            return;
        }
    }
    
    // now attach to the segment
    if ( ( mIgCtrlShmPtr = (char *)shmat( shmid, (char *)0, 0 ) ) == (char *) -1 )
    {
        perror("openIgCtrlShm: shmat()");
        mIgCtrlShmPtr = 0;
    }
    
    if ( !mIgCtrlShmPtr )
        return;

    struct shmid_ds sInfo;

    if ( ( shmid = shmctl( shmid, IPC_STAT, &sInfo ) ) < 0 )
        perror( "openIgCtrlShm: shmctl()" );
    else
        mIgCtrlShmTotalSize = sInfo.shm_segsz;
        
    // allocate a single buffer within the shared memory segment
    mIgCtrlRdbHandler.shmConfigure( mIgCtrlShmPtr, 1, mIgCtrlShmTotalSize );
}

int initIgCtrlShm()
{
    if ( !mIgCtrlShmPtr )
        return 0;
    
    // get access to the administration information of the first RDB buffer in SHM
    RDB_SHM_BUFFER_INFO_t* info = mIgCtrlRdbHandler.shmBufferGetInfo( 0 );    
    
    if ( !info )
        return 0;
        
    // force all flags to be zero
    info->flags = 0;
        
    // clear the buffer before writing to it (otherwise messages will accumulate)
    if ( !mIgCtrlRdbHandler.shmBufferClear( 0, true ) )   // true = clearing will be forced; not recommended!
        return 0;

    fprintf( stderr, "initIgCtrlShm: cleared shm\n" );
    
    return 1;
}    

int writeTriggerToIgCtrlShm()
{
    if ( !mIgCtrlShmPtr )
        return 0;
    
    // get access to the administration information of the first RDB buffer in SHM
    RDB_SHM_BUFFER_INFO_t* info = mIgCtrlRdbHandler.shmBufferGetInfo( 0 );    
    
    if ( !info )
        return 0;
        
    // is the buffer ready for write?
    if ( info->flags )      // is the buffer accessible (flags == 0)?
        return 0;
        
    // clear the buffer before writing to it (otherwise messages will accumulate)
    if ( !mIgCtrlRdbHandler.shmBufferClear( 0 ) )
        return 0;

    // lock immediately after clearing the buffer
    mIgCtrlRdbHandler.shmBufferLock( 0 );
    
    // initialize the message, so it does not continually grow
    mIgCtrlRdbHandler.initMsg();

    fprintf( stderr, "writeTriggerToIgCtrlShm: triggering IG.." );

    // increase the frame number
    mFrameNo++;

    // create a message containing the sync information
    RDB_SYNC_t* syncData = ( RDB_SYNC_t* ) mIgCtrlRdbHandler.addPackage( mFrameNo * mFrameTime, mFrameNo, RDB_PKG_ID_SYNC );

    if ( !syncData )
    {
        mIgCtrlRdbHandler.shmBufferRelease( 0 );
        fprintf( stderr, "..failed\n" );
        return 0;
    }

    syncData->mask    = 0x0;
    syncData->cmdMask = RDB_SYNC_CMD_RENDER_SINGLE_FRAME;

    // set some information concerning the RDB buffer itself
    info->id    = 1;
    info->flags = RDB_SHM_BUFFER_FLAG_IG;

    // now copy the symc message to the first RDB buffer in SHM
    mIgCtrlRdbHandler.mapMsgToShm( 0 );
    
    mIgCtrlRdbHandler.shmBufferRelease( 0 );

    fprintf( stderr, "..done\n" );
    
    return 1;
}    

/**
* open the shared memory segment
*/
void openIgOutShm()
{
    // do not open twice!
    if ( mIgOutShmPtr )
        return;
        
    int shmid = 0; 

    if ( ( shmid = shmget( mIgOutShmKey, 0, 0 ) ) < 0 )
        return;

    if ( ( mIgOutShmPtr = (char *)shmat( shmid, (char *)0, 0 ) ) == (char *) -1 )
    {
        perror("openIgOutShm: shmat()");
        mIgOutShmPtr = 0;
    }

    if ( mIgOutShmPtr )
    {
        struct shmid_ds sInfo;

        if ( ( shmid = shmctl( shmid, IPC_STAT, &sInfo ) ) < 0 )
            perror( "openIgOutShm: shmctl()" );
        else
            mIgOutShmTotalSize = sInfo.shm_segsz;
    }
}

int checkIgOutShm()
{
    if ( !mIgOutShmPtr )
        return 0;

    // get a pointer to the shm info block
    RDB_SHM_HDR_t* shmHdr = ( RDB_SHM_HDR_t* ) ( mIgOutShmPtr );

    if ( !shmHdr )
        return 0;

    if ( ( shmHdr->noBuffers != 2 ) )
    {
        fprintf( stderr, "checkIgOutShm: no or wrong number of buffers in shared memory. I need two buffers!" );
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
    RDB_MSG_t* pRdbMsgA = ( RDB_MSG_t* ) ( ( ( char* ) mIgOutShmPtr ) + pBufferInfo[0]->offset );
    RDB_MSG_t* pRdbMsgB = ( RDB_MSG_t* ) ( ( ( char* ) mIgOutShmPtr ) + pBufferInfo[1]->offset );
    
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
        fprintf( stderr, "ImageReader::checkIgOutShm: before processing SHM\n" );
        fprintf( stderr, "ImageReader::checkIgOutShm: Buffer A: frameNo = %06d, flags = 0x%x, locked = <%s>, lock mask set = <%s>, readyForRead = <%s>\n", 
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

    if ( mIgOutForceBuffer < 0 )  // auto-select the buffer if none is forced to be read
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
    else if ( ( mIgOutForceBuffer == 0 ) && readyForReadA )   // force reading buffer A
    {
        pRdbMsg            = pRdbMsgA; 
        pCurrentBufferInfo = pBufferInfo[ 0 ];
    }
    else if ( ( mIgOutForceBuffer == 1 ) && readyForReadB ) // force reading buffer B
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
        fprintf( stderr, "checkIgOutShm: zero message data size, error.\n" );
        return 0;
    }
    
    unsigned int maxReadSize = pCurrentBufferInfo->bufferSize;
    
    while ( 1 )
    {
        // handle the message that is contained in the buffer; this method should be provided by the user (i.e. YOU!)
        fprintf( stderr, "checkIgOutShm: processing message in buffer %s\n", ( pRdbMsg == pRdbMsgA ) ? "A" : "B" );
        
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

        fprintf( stderr, "ImageReader::checkIgOutShm: after processing SHM\n" );
        fprintf( stderr, "ImageReader::checkIgOutShm: Buffer A: frameNo = %06d, flags = 0x%x, locked = <%s>, lock mask set = <%s>\n", 
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

double getTime()
{
    struct timeval tme;
    gettimeofday(&tme, 0);
    
    double now = tme.tv_sec + 1.0e-6 * tme.tv_usec;
    
    if ( mStartTime < 0.0 )
        mStartTime = now;
    
    return now;
}

void calcStatistics()
{
    double now = getTime();
    
    double dt = now - mStartTime;
    
    if ( dt < 1.e-6 )
        return;
        
    fprintf( stderr, "calcStatistics: received %d images in %.3lf seconds (i.e. %.3lf images per second )\n", 
                     mTotalNoImages, dt, mTotalNoImages / dt );
}
