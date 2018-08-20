// ShmWriter.cpp : Sample implementation of a connection to the IG's control
// SHM for triggering individual render frames.
//
// (c) 2015 by VIRES Simulationstechnologie GmbH
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
* method for writing the commands to the SHM
*/
void openShm();
int  initShm();
int  writeTriggerToShm();

/**
* some global variables, considered "members" of this example
*/
unsigned int mShmKey       = RDB_SHM_ID_CONTROL_GENERATOR_IN;   // key of the SHM segment
unsigned int mFrameNo      = 0;
double       mFrameTime    = 0.030;                             // frame time is 30ms
void*        mShmPtr       = 0;                                 // pointer to the SHM segment
size_t       mShmTotalSize = 64 * 1024;                         // 64kB total size of SHM segment
bool         mVerbose      = false;                             // run in verbose mode?
Framework::RDBHandler mRdbHandler;                              // use the RDBHandler helper routines to handle 
                                                                // the memory and message management

/**
* information about usage of the software
* this method will exit the program
*/
void usage()
{
    printf("usage: shmWriter [-k:key]\n\n");
    printf("       -k:key        SHM key that is to be addressed\n");
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
                    
                case 'v':       // verbose mode
                    mVerbose = true;
                    break;
                    
                default:
                    usage();
                    break;
            }
        }
    }
    
    fprintf( stderr, "ValidateArgs: key = 0x%x\n", mShmKey );
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
        usleep( 1000 );     // do not overload the CPU
    }
    
    fprintf( stderr, "...attached! Initializing now...\n" );
    
    int retVal = initShm();
    
    fprintf( stderr, "...initialized (result = %d)! Triggering now...\n", retVal );
    // now write the trigger to the SHM for the time being
    while ( 1 )
    {
        writeTriggerToShm();
        
        usleep( ( unsigned int ) ( 1.e6 * mFrameTime ) );    // wait for frame time (not very precise!)
    }
}

/**
* open the shared memory segment; create if necessary
*/
void openShm()
{
    // do not open twice!
    if ( mShmPtr )
        return;
        
    int shmid = 0; 
    int flag  = IPC_CREAT | 0777;
    
    // does the memory already exist?
    if ( ( shmid = shmget( mShmKey, 0, 0 ) ) < 0 )
    {
        // not yet there, so let's create the segment
        if ( ( shmid = shmget( mShmKey, mShmTotalSize, flag ) ) < 0 )
        {
            perror("openShm: shmget()");
            mShmPtr = 0;
            return;
        }
    }
    
    // now attach to the segment
    if ( ( mShmPtr = (char *)shmat( shmid, (char *)0, 0 ) ) == (char *) -1 )
    {
        perror("openShm: shmat()");
        mShmPtr = 0;
    }
    
    if ( !mShmPtr )
        return;

    struct shmid_ds sInfo;

    if ( ( shmid = shmctl( shmid, IPC_STAT, &sInfo ) ) < 0 )
        perror( "openShm: shmctl()" );
    else
        mShmTotalSize = sInfo.shm_segsz;
        
    // allocate a single buffer within the shared memory segment
    mRdbHandler.shmConfigure( mShmPtr, 1, mShmTotalSize );
}

int initShm()
{
    if ( !mShmPtr )
        return 0;
    
    // get access to the administration information of the first RDB buffer in SHM
    RDB_SHM_BUFFER_INFO_t* info = mRdbHandler.shmBufferGetInfo( 0 );    
    
    if ( !info )
        return 0;
        
    // force all flags to be zero
    info->flags = 0;
        
    // clear the buffer before writing to it (otherwise messages will accumulate)
    if ( !mRdbHandler.shmBufferClear( 0, true ) )   // true = clearing will be forced; not recommended!
        return 0;

    fprintf( stderr, "initShm: cleared shm\n" );
    
    return 1;
}    

int writeTriggerToShm()
{
    if ( !mShmPtr )
        return 0;
    
    // get access to the administration information of the first RDB buffer in SHM
    RDB_SHM_BUFFER_INFO_t* info = mRdbHandler.shmBufferGetInfo( 0 );    
    
    if ( !info )
        return 0;
        
    // is the buffer ready for write?
    if ( info->flags )      // is the buffer accessible (flags == 0)?
        return 0;
        
    // clear the buffer before writing to it (otherwise messages will accumulate)
    if ( !mRdbHandler.shmBufferClear( 0, true ) )   // true = clearing will be forced; not recommended!
        return 0;

    fprintf( stderr, "writeTriggerToShm: sending single trigger\n" );

    // increase the frame number
    mFrameNo++;

    // create a message containing the sync information
    RDB_SYNC_t* syncData = ( RDB_SYNC_t* ) mRdbHandler.addPackage( mFrameNo * mFrameTime, mFrameNo, RDB_PKG_ID_SYNC );

    if ( !syncData )
        return 0;

    syncData->mask    = 0x0;
    syncData->cmdMask = RDB_SYNC_CMD_RENDER_SINGLE_FRAME;

    // set some information concerning the RDB buffer itself
    info->id    = 1;
    info->flags = RDB_SHM_BUFFER_FLAG_IG;

    // now copy the symc message to the first RDB buffer in SHM
    mRdbHandler.mapMsgToShm( 0 );
    
    return 1;
}    
