#!/bin/tcsh

# compile the RDB shm reader and writer examples

echo "compiling shmReader..."
g++ -o shmReader RDBHandler.cc ShmReader.cpp
echo "...done"

echo "compiling shmWriter..."
g++ -o shmWriter RDBHandler.cc ShmWriter.cpp
echo "...done"

echo "compiling shmWriterExt..."
g++ -o shmWriterExt RDBHandler.cc ShmWriterExt.cpp
echo "...done"
