#!/usr/bin/env python

from __future__ import print_function
import csv
import sys
import os
from os import listdir
from os.path import isfile, join
from shutil import copyfile

class MyDialect(csv.Dialect):
    strict = True
    skipinitialspace = True
    quoting = csv.QUOTE_ALL
    delimiter = ';'
    quotechar = '"'
    lineterminator = '\n'

def print_err(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

def process_xml_file(csvFile, annotationsDirPath, subDir, imageSetsFile, folderName):

    try:
        csvData = csv.reader(open(csvFile))
    except:
        XML_INPUT_FILE_MSG = "Directory " + subDir + "doesn't contain the file: " + csvFile
        print_err(XML_INPUT_FILE_MSG)

    header = csvData.next()

    for row in csvData:

        rowReader = csv.reader(row, MyDialect())
        F_NAME, WIDTH, HEIGHT, ROI_X1_MIN, ROI_Y1_MIN, ROI_X2_MAX, ROI_Y2_MAX, CLASS_ID = [i for i in range(8)]

        for rowVals in rowReader:

            newImageName = subDir + "_" + rowVals[F_NAME]

            copyfile(folderName + "/" + subDir + "/" + rowVals[F_NAME], imagesDirPath + "/" + newImageName)

            imageSetsFile.write(newImageName.replace(".ppm", "") + "\n")

            xmlData = open(annotationsDirPath + "/" + newImageName.replace(".ppm", ".xml"), 'w')

            UNDEF_STR = "UNDEFINED\n"

            xmlData.write("<annotation>\n")
            xmlData.write("<folderName>\n")
            xmlData.write("<fileName>\n")
            xmlData.write(subDir + "/" + rowVals[F_NAME] + "\n") # original image name
            xmlData.write("</fileName>\n")
            xmlData.write("<source>\n")
            xmlData.write("<database>\n")
            xmlData.write("GTRSB\n")
            xmlData.write("</database>\n")
            xmlData.write("<annotation>\n")
            xmlData.write(UNDEF_STR)
            xmlData.write("</annotation>\n")
            xmlData.write("<image>\n")
            xmlData.write(newImageName + "\n") # new unique image file name
            xmlData.write("</image>\n")
            xmlData.write("<flickrid>\n")
            xmlData.write(UNDEF_STR)
            xmlData.write("</flickrid>\n")
            xmlData.write("</source>\n")
            xmlData.write("<owner>\n")
            xmlData.write("<flickrid>\n")
            xmlData.write(UNDEF_STR)
            xmlData.write("</flickrid>\n")
            xmlData.write("<name\n>")
            xmlData.write(subDir)
            xmlData.write("</name>\n")
            xmlData.write("</owner>\n")
            xmlData.write("<size>\n")
            xmlData.write("<width>\n")
            xmlData.write(rowVals[WIDTH]+"\n")
            xmlData.write("</width>\n")
            xmlData.write("<height>\n")
            xmlData.write(rowVals[HEIGHT]+"\n")
            xmlData.write("</height>\n")
            xmlData.write("</size>\n")
            xmlData.write("<depth>\n")
            xmlData.write("3\n")
            xmlData.write("</depth>\n")
            xmlData.write("<segmented>\n")
            xmlData.write(UNDEF_STR)
            xmlData.write("</segmented>\n")
            xmlData.write("<object>\n")
            xmlData.write("<name>\n")
            xmlData.write(subDir+"\n")
            xmlData.write("</name>\n")
            xmlData.write("<pose>\n")
            xmlData.write(UNDEF_STR)
            xmlData.write("</pose>\n")
            xmlData.write("<truncated>\n")
            xmlData.write(UNDEF_STR)
            xmlData.write("</truncated>\n")
            xmlData.write("<difficult>\n")
            xmlData.write(UNDEF_STR)
            xmlData.write("</difficult>\n")
            xmlData.write("<bndbox>\n")
            xmlData.write("<xmin>\n")
            xmlData.write(rowVals[ROI_X1_MIN]+"\n")
            xmlData.write("</xmin>\n")
            xmlData.write("<ymin>\n")
            xmlData.write(rowVals[ROI_Y1_MIN]+"\n")
            xmlData.write("</ymin>\n")
            xmlData.write("<xmax>\n")
            xmlData.write(rowVals[ROI_X2_MAX]+"\n")
            xmlData.write("</xmax>\n")
            xmlData.write("<ymax>\n")
            xmlData.write(rowVals[ROI_Y2_MAX]+"\n")
            xmlData.write("</ymax>\n")
            xmlData.write("</bndbox>\n")
            xmlData.write("</object>\n")
            xmlData.write("</folderName>\n")
            xmlData.write("</annotation>\n")

            xmlData.close()

#################################################

try:
    folderName = sys.argv[1]
except:
    USAGE_MSG = "USAGE: python csvToXml [path to directory \"images\"]"
    print_err(USAGE_MSG)

annotationsDirPath = "Annotations"  # subDir + "_xml_files"
if not os.path.exists(annotationsDirPath):
    try:
        os.makedirs(annotationsDirPath)
    except:
        OPEN_DIR_MSG = "Couldn't open directory: " + annotationsDirPath

imagesDirPath = "Images"
if not os.path.exists(imagesDirPath):
    try:
        os.makedirs(imagesDirPath)
    except:
        OPEN_DIR_MSG = "Couldn't open directory: " + imagesDirPath

imageSetDirPath = "ImageSets"
if not os.path.exists(imageSetDirPath):
    try:
        os.makedirs(imageSetDirPath)
    except:
        OPEN_DIR_MSG = "Couldn't open directory: " + imageSetDirPath

imageSetsFile = open(imageSetDirPath + "/train.txt", "w")

foldersLst = [f for f in listdir(folderName) if not isfile(join(folderName, f))]

for subDir in foldersLst:
    csvFile = folderName+"/"+ subDir +"/GT-"+subDir+".csv"
    process_xml_file(csvFile, annotationsDirPath, subDir, imageSetsFile, folderName)

imageSetsFile.close()




