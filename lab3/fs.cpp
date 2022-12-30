#include <iostream>
#include <iomanip>
#include <cstring>
#include "fs.h"

//error list
// -1. could not find object
// -2. could not allocate space for object
// -3. missing access rights
// -4. file already exists

//TODO:

std::string currDirStr = "/";
DirBlock currentDir;

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
}

FS::~FS()
{

}

// formats the disk, i.e., creates an empty file system
int
FS::format()
{
    //create a datablock and fill with zero
    Datablock buffer;
    std::fill(std::begin(buffer), std::end(buffer), 0);

    //We will overwrite the whole disk with this block to reset anything that is left
    for(int i = 0; i < 2048; ++i)
        disk.write(i, (uint8_t*)&buffer);

    //create a new rootDir
    DirBlock rootDir;
    InitDir(rootDir);

    //write rootDir to disk
    disk.write(0, (uint8_t*)&rootDir.entries);

    //save rootDir as our current dir
    currentDir = rootDir;
    currentDir.blockNo = 0;
    currentDir.access_right = 6;

    //set all fat entries to free
    std::fill(std::begin(FAT), std::end(FAT), FAT_FREE);

    FAT[0] = ROOT_BLOCK;
    FAT[1] = FAT_BLOCK;

    //save fat
    SaveFat();

    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int
FS::create(const std::string& filepath)
{
    int16_t block = currentDir.blockNo;
    uint8_t access = currentDir.access_right;

    //std::pair<DirBlock*, std::string> dirPair = GetDir(filepath);
    std::pair<DirBlock, std::string> dirPair = GetDir(filepath);

    if(dirPair.first.blockNo == -1)
    {
        std::cout << "could not find that directory" << std::endl;
        return -1;
    }

    //check if we have write permission on the folder
    if(dirPair.first.access_right != 6 && dirPair.first.access_right != 7 && dirPair.first.access_right != 2)
    {
        std::cout << "not permission" << std::endl;
        return -3;
    }

    // check for file errors
    if(!CheckFileCreation(filepath, dirPair.first))
    {
        return -4;
    }

    //check that we can add it to our fat
    int fatIndex = GetUnusedBlock();

    if(fatIndex == -1)
    {
        std::cout << "couldn't allocate a fat entry, the FAT is probably full" << std::endl;
        return -2;
    }

    //check that we can add to the current directory
    int dirIndex = FindFreeDirPlace(dirPair.first);

    if(dirIndex == -1)
    {
        std::cout << "couldn't create a file in this directory, it is probably full" << std::endl;
        return -2;
    }

    //allocate a file
    File file;
    file.pos = 0;
    file.dirEntry.size = 0;
    file.dirEntry.type = TYPE_FILE;
    file.dirEntry.first_blk = fatIndex;
    std::copy(std::begin(dirPair.second), std::end(dirPair.second), file.dirEntry.file_name);
    if(dirPair.second.size() < 55)
        file.dirEntry.file_name[dirPair.second.size()] = '\0';
    file.dirEntry.access_rights = 6;

    std::fill(std::begin(file.buffer), std::end(file.buffer), 0);

    //save to fat
    FAT[fatIndex] = FAT_EOF;

    //save the fat
    SaveFat();

    std::string str;

    int writePos = file.dirEntry.first_blk;

    //Continue to read forever
    while(getline(std::cin, str))
    {
        //if string is empty we will save the file
        if(str.empty())
            break;

        str += '\n';

        for(int i = 0; i < str.size(); ++i)
        {
            //if memory becomes full
            if(file.pos == 1024)
            {
                //write to disk
                disk.write(writePos, (uint8_t*)&file.buffer);

                int indexNextBlock = GetUnusedBlock();
                FAT[writePos] = indexNextBlock;
                FAT[indexNextBlock] = FAT_EOF;
                SaveFat();
                file.pos = 0;
                writePos = indexNextBlock;
            }

            file.buffer[file.pos] = str[i];
            file.pos++;
            file.dirEntry.size += sizeof(char);
        }
    }

    //check if we need to null terminate
    if((file.dirEntry.size % 1024) > 0)
        file.buffer[file.pos++] = '\0';

    //write last block to disk
    disk.write(writePos, (uint8_t*)file.buffer);

    //save direntry to the directory
    dirPair.first.entries[dirIndex] = file.dirEntry;

    disk.write(dirPair.first.blockNo, (uint8_t*)dirPair.first.entries);

    disk.read(block, (uint8_t*)currentDir.entries);
    currentDir.blockNo = block;
    currentDir.access_right = access;

    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(const std::string& filepath)
{
    int16_t block = currentDir.blockNo;
    uint8_t access = currentDir.access_right;

    std::pair<DirBlock, std::string> dirPair = GetDir(filepath);

    if(dirPair.first.blockNo == -1)
    {
        std::cout << "couldn't find this directory" << std::endl;
        return -1;
    }

    //check that file actually exists
    int fileIndex = FindFile(filepath, dirPair.first);

    if(fileIndex == -1)
    {
        std::cout << "couldn't find file, it probably doesn't exists in this directory" << std::endl;
        return -1;
    }

    //check that we have permissions
    if(dirPair.first.entries[fileIndex].access_rights < 4)
    {
        std::cout << "missing access to this file" << std::endl;
        return -3;
    }

    //allocate a new FatEntry
    int16_t fatIndex = dirPair.first.entries[fileIndex].first_blk;
    Datablock buffer;

    do
    {
        //fetch file
        disk.read(fatIndex, (uint8_t*)buffer);

        //print the content
        //because we have a null terminated char arr we can just print it
        std::cout << buffer;

        //fetch next fat block
        fatIndex = FAT[fatIndex];
    }
    while(fatIndex != -1);    //check if the fat points to anymore data

    currentDir.blockNo = block;
    currentDir.access_right = access;
    disk.read(block, (uint8_t*)currentDir.entries);

    //end the line
    std::cout << std::endl;

    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{
    std::cout.fill(' ');
    std::cout << std::setw(30) << std::left << "Filename:";
    std::cout << std::setw(30) << std::left << "Type:";
    std::cout << std::setw(30) << std::left << "Accessrights:";
    std::cout << "Size:" << std::endl;

    for(int i = 0; i < 64; ++i) //max files in a directory
    {
        //check if entry in entris is used i.e. that something exists in that position
        if(currentDir.entries[i].access_rights != 0)
        {
            std::cout << std::setw(30) << std::left << currentDir.entries[i].file_name;
            int temp = currentDir.entries[i].access_rights;

            std::string str = "";

            if(temp >= 4)
            {
                str += "r";
                temp -= 4;
            }
            else
                str += "-";


            if(temp >= 2)
            {
                str += "w";
                temp -= 2;

            }
            else
                str += "-";

            if(temp == 1)
                str += "x";
            else
                str += "-";

            if(currentDir.entries[i].type == TYPE_FILE)
            {
                std::cout << std::setw(30) << std::left << "File";
                std::cout << std::setw(30) << std::left << str;
                std::cout << currentDir.entries[i].size << std::endl;
            }
            else
            {
                std::cout << std::setw(30) << std::left << "Dir";
                std::cout << std::setw(30) << std::left << str;
                std::cout << "-" << std::endl;
            }
        }
    }

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(const std::string& sourcepath, const std::string& destpath)
{
    int16_t block = currentDir.blockNo;
    uint8_t access = currentDir.access_right;

    //---------------SourcePath error check------------------//
    //first load source path directory
    std::pair<DirBlock, std::string> sourceDir = GetDir(sourcepath);

    //check if it exists
    if(sourceDir.first.blockNo == -1)
    {
        return -1;
    }

    //check if file exists in this directory
    int sourceFileIndex = FindFile(sourceDir.second, sourceDir.first);

    if(sourceFileIndex == -1)
    {
        std::cout << "couldnt find sourcefile" << std::endl;
        return -4;
    }

    //check that we have read access on the file
    if(currentDir.entries[sourceFileIndex].access_rights < 4)
    {
        std::cout << "missing permission for reading sourcefile" << std::endl;
        return -3;
    }

    //---------------destPath error check------------------//
    //load destDir
    std::pair<DirBlock, std::string> destDir = GetDir(destpath);

    // check if
    if(destDir.first.blockNo == -1)
    {
        std::cout << "couldnt find this directory" << std::endl;
        return -1;
    }

    //check if it is a directory
    int tempIndex = FindDirectory(destDir.second, destDir.first);

    if(tempIndex != -1)
    {
        //load that directory
        destDir.first.blockNo = destDir.first.entries[tempIndex].first_blk;
        destDir.first.access_right = destDir.first.entries[tempIndex].access_rights;
        disk.read(destDir.first.blockNo, (uint8_t*)destDir.first.entries);
        //and find the prior name
        destDir.second = sourceDir.second;
    }

    //check that the file doesn't exist
    if(!CheckFileCreation(destDir.second, destDir.first))
    {
        std::cout << "sourcefile already exists in this directory" << std::endl;
        return -4;
    }

    //check for write permission in that folder
    if(!(currentDir.access_right > 1 && currentDir.access_right != 5 && currentDir.access_right != 4))
    {
        std::cout << "missing permission for writing in this folder" << std::endl;
        return -3;
    }

    // next see if we can add it to the fat
    int fatIndexNewFile = GetUnusedBlock();

    if(fatIndexNewFile == -1)
    {
        std::cout << "Couldn't allocate any new space on the fat. It is most probably full" << std::endl;
        return -2;
    }

    // see if we can add it to the directory
    int dirIndex = FindFreeDirPlace(currentDir);

    if(dirIndex == -1)
    {
        std::cout << "couldn't allocate any more space in this directory" << std::endl;
        return -2;
    }

    //allocate a file
    File file;
    file.pos = 0;
    std::fill(std::begin(file.buffer), std::end(file.buffer), 0);

    //give it a name
    std::copy(std::begin(destDir.second), std::end(destDir.second), file.dirEntry.file_name);
    if(destDir.second.size() < 55)
        file.dirEntry.file_name[destDir.second.size()] = '\0';

    //give it a size
    file.dirEntry.size = sourceDir.first.entries[sourceFileIndex].size;
    file.dirEntry.type = TYPE_FILE;
    file.dirEntry.first_blk = fatIndexNewFile;
    file.dirEntry.access_rights = sourceDir.first.entries[sourceFileIndex].access_rights;

    //save dirEntry to dir
    destDir.first.entries[dirIndex] = file.dirEntry;

    //save dir to disk
    disk.write(destDir.first.blockNo, (uint8_t*)destDir.first.entries);

    //save to Fat
    FAT[fatIndexNewFile] = FAT_EOF;

    //save the fat
    SaveFat();

    //fetch the first block for the file
    int fatIndexOldFile = sourceDir.first.entries[sourceFileIndex].first_blk;

    // we will read one block and write this to the disk
    // we will then set fatIndexOfFile to FAT[fatIndexOldFile]
    // if fatIndexOfFile is not -1 we will fetch a new fatblock and change vlaue in fat then continue

    while(fatIndexOldFile != -1)
    {
        //first we will read the first block
        disk.read(fatIndexOldFile, (uint8_t*)file.buffer);
        //now we will write this to the disk in the new
        disk.write(fatIndexNewFile, (uint8_t*)file.buffer);

        fatIndexOldFile = FAT[fatIndexOldFile];

        if(fatIndexOldFile != -1)
        {
            int temp = GetUnusedBlock();
            FAT[fatIndexNewFile] = temp;
            FAT[temp] = FAT_EOF;
            fatIndexNewFile = temp;
        }
    }

    currentDir.blockNo = block;
    currentDir.access_right = access;

    disk.read(block, (uint8_t*)currentDir.entries);

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(const std::string& sourcepath, const std::string& destpath)
{
    int16_t block = currentDir.blockNo;
    uint8_t access = currentDir.access_right;

    //---------------SourcePath error check------------------//
    //first load source path directory
    std::pair<DirBlock, std::string> sourceDir = GetDir(sourcepath);

    if(sourceDir.first.blockNo == -1)
    {
        std::cout << "could not find that directory" << std::endl;
        return -1;
    }

    //check if sourepath exists
    int readPos = FindFile(sourceDir.second, sourceDir.first);

    if(readPos == -1)
    {
        std::cout << "couldn't find source file" << std::endl;
        return -1;
    }

    dir_entry fileDir = sourceDir.first.entries[readPos];
    sourceDir.first.entries[readPos].access_rights = 0;
    disk.write(sourceDir.first.blockNo, (uint8_t*)sourceDir.first.entries);

    disk.read(block, (uint8_t*)currentDir.entries);

    std::pair<DirBlock, std::string> destDir = GetDir(destpath);

    //Check if the last entry is a directory, in other words that we want to mv to a directory and not rename
    int dirIndex = FindDirectory(destDir.second, currentDir);
    int temp = destDir.first.blockNo;

    //if it is a directory load it
    if(dirIndex != -1)
    {
        destDir.first.access_right = destDir.first.entries[dirIndex].access_rights;
        temp = destDir.first.entries[dirIndex].first_blk;
        destDir.first.access_right = destDir.first.entries[dirIndex].access_rights;
        disk.read(temp, (uint8_t*)destDir.first.entries);
        destDir.second = sourceDir.second;
    }

    //check write permissions for that folder
    if(destDir.first.access_right != 6 && destDir.first.access_right != 7 && destDir.first.access_right != 2)
    {
        std::cout << "not permission" << std::endl;
        return -3;
    }

    int writePos = FindFreeDirPlace(destDir.first);

    if(writePos == -1)
    {
        std::cout << "couldn't allocate a new entry in this directory" << std::endl;
        return -2;
    }

    if(CheckFileCreation(destDir.second, destDir.first))
    {
        //now we want to save that fileentry to the write dir
        destDir.first.entries[writePos] = fileDir;
        strcpy(destDir.first.entries[writePos].file_name, destDir.second.c_str());
        //both will write to the same block number which will result that we overwrite the value
        disk.write(temp, (uint8_t*)destDir.first.entries);

        currentDir.blockNo = block;
        currentDir.access_right = access;
        disk.read(block, (uint8_t*)currentDir.entries);
    }
    else
    {
        sourceDir.first.entries[readPos] = fileDir;
        disk.write(sourceDir.first.blockNo, (uint8_t*)sourceDir.first.entries);
        currentDir.blockNo = block;
        currentDir.access_right = access;
        disk.read(block, (uint8_t*)currentDir.entries);

        return -4;
    }

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(const std::string& filepath)
{
    int block = currentDir.blockNo;
    int access = currentDir.access_right;

    //we will first test if a file exists with this filename
    std::pair<int, DirBlock> rmPair = FindRm(filepath);

    //check if it was found
    if(rmPair.first == -1)
    {
        std::cout << "couldnt find this object" << std::endl;
        return -1;
    }

    //check access
    if(rmPair.second.entries[rmPair.first].access_rights == 1 ||
       rmPair.second.entries[rmPair.first].access_rights == 4 ||
       rmPair.second.entries[rmPair.first].access_rights == 5)
    {
        std::cout << "missing access" << std::endl;
        return -3;
    }

    //check if it was a directory and if that is true check if that directory is empty
    if(rmPair.second.entries[rmPair.first].type == TYPE_DIR)
    {
        DirBlock temp;

        disk.read(rmPair.second.entries[rmPair.first].first_blk, (uint8_t*)temp.entries);

        for(int i = 1; i < 64; ++i)
        {
            if(temp.entries[i].access_rights != 0)
                return -1;
        }
    }

    rmPair.second.entries[rmPair.first].access_rights = 0;

    int fIndex = rmPair.second.entries[rmPair.first].first_blk;

    while(fIndex != -1)
    {
        int nextIndex = FAT[fIndex];
        FAT[fIndex] = FAT_EOF;
        fIndex = nextIndex;
    }

    SaveFat();

    disk.write(rmPair.second.blockNo, (uint8_t*)rmPair.second.entries);

    currentDir.access_right = access;
    currentDir.blockNo = block;

    disk.read(block, (uint8_t*)currentDir.entries);

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(const std::string& filepath1, const std::string& filepath2)
{

    std::pair<int, DirBlock> copyDir = FindRm(filepath1);
    std::pair<int, DirBlock> writeDir = FindRm(filepath2);
    int copyFrom = copyDir.first;
    int writeTo = writeDir.first;

    if(copyFrom == -1)
    {
        std::cout << "cant find file to copy from" << std::endl;
        return -1;
    }

    if(writeTo == -1)
    {
        std::cout << "Could not find the file to append to" << std::endl;
        return -1;
    }

    if(copyDir.second.entries[copyFrom].access_rights < 4)
    {
        std::cout << "insuffienct access rights for copy file" << std::endl;
        return -3;
    }

    if(writeDir.second.entries[writeTo].access_rights < 5 && writeDir.second.entries[writeTo].access_rights != 2)
    {
        std::cout << "insuffienct access rights for write file" << std::endl;
        return -3;
    }

    Datablock buffer;

    if(writeDir.second.entries[writeTo].size % 1024 == 0)
    {
        int blk;
        int blkValue = writeDir.second.entries[writeTo].first_blk;
        while(blkValue != -1)
        {
            blk = blkValue;
            blkValue = FAT[blk];
        }

        int indexCopy = copyDir.second.entries[copyFrom].first_blk;

        while(indexCopy != -1)
        {
            int frFat = GetUnusedBlock();
            disk.read(FAT[indexCopy], (uint8_t*)buffer);

            disk.write(frFat, (uint8_t*)buffer);
            FAT[blk] = frFat;
            FAT[frFat] = FAT_EOF;
            blk = frFat;

            indexCopy = FAT[indexCopy];
        }

        writeDir.second.entries[writeTo].size += copyDir.second.entries[copyFrom].size;

        disk.write(writeDir.second.blockNo, (uint8_t*)writeDir.second.entries);
    }
    else
    {
        int filePos = writeDir.second.entries[writeTo].size % 1024;
        int temp = writeDir.second.entries[writeTo].first_blk;
        writeDir.second.entries[writeTo].size += copyDir.second.entries[copyFrom].size;

        int indexCopy = copyDir.second.entries[copyFrom].first_blk;
        int size = copyDir.second.entries[copyFrom].size;

        while(temp != -1)
        {
            writeTo = temp;
            temp = FAT[writeTo];
        }

        disk.read(writeTo, (uint8_t*)buffer);

        Datablock readFrom;
        int rangeVal;

        while(indexCopy != -1)
        {
            disk.read(indexCopy, (uint8_t*)readFrom);

            if(size < 1024)
            {
                rangeVal = size;
            }
            else
            {
                rangeVal = 1024;
                size -= 1024;
            }

            for (int i = 0; i < rangeVal; ++i)
            {
                if(filePos == 1024)
                {
                    disk.write(writeTo, (uint8_t*)buffer);
                    int indexNextBlock = GetUnusedBlock();
                    FAT[writeTo] = indexNextBlock;
                    SaveFat();
                    filePos = 0;
                    writeTo = indexNextBlock;
                }

                buffer[filePos++] = readFrom[i];
            }

            indexCopy = FAT[indexCopy];
        }

        if((filePos % 1024) > 0)
            buffer[filePos++] = '\0';

        //write last block to disk
        disk.write(writeTo, (uint8_t*)buffer);

        disk.write(currentDir.blockNo, (uint8_t*)writeDir.second.entries);
    }

    disk.read(0, (uint8_t*)currentDir.entries);
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(const std::string& dirpath)
{
    //first find dir
    int16_t block = currentDir.blockNo;
    uint8_t access_rights = currentDir.access_right;

    std::pair<DirBlock, std::string> dirPair = GetDir(dirpath);
    //check permissions for that folder

    if(dirPair.first.blockNo == -1)
    {
        std::cout << "couldn't find this path" << std::endl;
        return -1;
    }

    if(dirPair.first.access_right < 6 && dirPair.first.access_right != 2)
    {
        std::cout << "missing access" << std::endl;
        return -3;
    }

    if(!CheckFileCreation(dirPair.second, dirPair.first))
    {
        return -1;
    }

    //find free fatblock
    int freeFat = GetUnusedBlock();

    if(freeFat == -1)
    {
        std::cout << "couldn't allocate anymore entries in the fat" << std::endl;
        return -2;
    }

    //find empty dirPlace
    int fDir = FindFreeDirPlace(dirPair.first);

    if(fDir == -1)
    {
        std::cout << "couldn't find a empty position in dir" << std::endl;
        return -2;
    }

    // mark Fat as end of file
    FAT[freeFat] = FAT_EOF;

    //save fat
    SaveFat();

    //create a new dir_entry
    dir_entry nEntry;

    nEntry.type = TYPE_DIR;
    std::copy(std::begin(dirPair.second), std::end(dirPair.second), nEntry.file_name);
    if(dirPair.second.size() < 55)
        nEntry.file_name[dirPair.second.size()] = '\0';
    nEntry.first_blk = freeFat;
    nEntry.access_rights = 6;

    //add to current directory
    dirPair.first.entries[fDir] = nEntry;

    //save dir
    disk.write(dirPair.first.blockNo, (uint8_t*)dirPair.first.entries);

    //allocate a new DirBlock
    DirBlock nDir;

    InitDir(nDir);

    //create a new entry to put our root dir in
    nEntry.first_blk = dirPair.first.blockNo;
    strcpy(nEntry.file_name, "..");

    nDir.entries[0] = nEntry;

    //save to disk
    disk.write(freeFat, (uint8_t*)nDir.entries);

    disk.read(block, (uint8_t*)currentDir.entries);
    currentDir.blockNo = block;
    currentDir.access_right = access_rights;

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(const std::string& dirpath)
{
    if(dirpath == "/")
    {
        disk.read(0, (uint8_t*)currentDir.entries);
        currentDir.blockNo = 0;
        currentDir.access_right = 6;

        currDirStr = "/";

        return 0;
    }

    std::pair<DirBlock, std::string> nDir = CdHelper(dirpath);

    //if we cant find it return -1
    if(nDir.first.blockNo == -1)
    {
        std::cout << "couldn't find this directory" << std::endl;
        return -1;
    }

    // we now have to find the last directory
    int index = FindDirectory(nDir.second, nDir.first);

    if(index == -1)
    {
        std::cout << "couldnt find this directory" << std::endl;
        return -1;
    }

    if(nDir.first.entries[index].access_rights < 4)
    {
        std::cout << "not suffienct access rights" << std::endl;
        return -3;
    }

    currentDir.access_right = nDir.first.entries[index].access_rights;
    currentDir.blockNo = nDir.first.entries[index].first_blk;

    if(strcmp(nDir.first.entries[index].file_name, "..") == 0)
    {
        currDirStr = currDirStr.substr(0, currDirStr.find_last_of('/'));

        //fix when we cd from one directory to root
        if (currDirStr.empty())
            currDirStr = '/';
    }
    else
    {
        if(currDirStr[currDirStr.size()-1] != '/')
            currDirStr += '/';

        currDirStr += nDir.first.entries[index].file_name;
    }

    //otherwise load
    disk.read(nDir.first.entries[index].first_blk, (uint8_t*)currentDir.entries);

    /*
    std::pair<DirBlock, std::string> nDir = GetDir(dirpath);

    //if we cant find it return -1
    if(nDir.first.blockNo == -1)
    {
        std::cout << "couldn't find this directory" << std::endl;
        return -1;
    }

    // we now have to find the last directory

    int index = FindDirectory(nDir.second, nDir.first);

    if(index == -1)
    {
        std::cout << "couldnt find this directory" << std::endl;
        return -1;
    }

    if(nDir.first.entries[index].access_rights < 4)
    {
        std::cout << "not suffienct access rights" << std::endl;
        return -3;
    }

    currentDir.access_right = nDir.first.entries[index].access_rights;
    currentDir.blockNo = nDir.first.entries[index].first_blk;

    //otherwise load
    disk.read(nDir.first.entries[index].first_blk, (uint8_t*)currentDir.entries);

    int start = 0;
    int end = dirpath.find('/');

    if(dirpath[0] == '/')
    {
        currDirStr = '/';

        start = 1;
        end = dirpath.find('/', 1);
    }

    if(end == std::string::npos)
    {
        if(dirpath == "..")
        {
            currDirStr = currDirStr.substr(0, currDirStr.find_last_of('/'));

            //fix when we cd from one directory to root
            if(currDirStr.empty())
                currDirStr += '/';

            return 0;
        }
    }
    else
    {
        while(end != std::string::npos)
        {
            if(dirpath.substr(start, end-start) == "..")
            {
                currDirStr = currDirStr.substr(0, currDirStr.find_last_of('/'));

                //fix when we cd from one directory to root
                if(currDirStr.empty())
                    currDirStr += '/';
            }
            else
                break;

            start = end + 1;
            end = dirpath.find('/', start);
        }
    }

    currDirStr += dirpath.substr(start, std::string::npos);
    */
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    std::cout << currDirStr << std::endl;

    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(const std::string& accessrights, const std::string& filepath)
{
    std::pair<int, DirBlock> filePair = FindRm(filepath);

    if(filePair.first == -1)
    {
        std::cout << "couldn't find this object in this directory" << std::endl;
        return -1;
    }

    filePair.second.entries[filePair.first].access_rights = stoi(accessrights);

    disk.write(filePair.second.blockNo, (uint8_t*)filePair.second.entries);

    disk.read(currentDir.blockNo, (uint8_t*)currentDir.entries);

    return 0;
}

int FS::GetUnusedBlock()
{
    for(int i = 2; i < BLOCK_SIZE/2; ++i)
    {
        if(FAT[i] == FAT_FREE)
            return i;
    }

    return -1;
}

void FS::SaveFat()
{
    disk.write(1, (uint8_t*)&FAT);
}

int FS::FindFreeDirPlace(DirBlock& dir)
{
    //loop through all entries
    for(int i = 0; i < 64; ++i)
    {
        //check if entry is used
        if(dir.entries[i].access_rights == 0)
            return i;
    }

    //if we reach here we return -1 because it is full
    return -1;
}

bool FS::CheckFileCreation(const std::string& filename, const DirBlock& dir)
{
    //first check size that we can fit the filename in our char array. If we cant, we can't create the file
    if(filename.size() > 55)
    {
        std::cout << "Filename was too long, max length is 55." << std::endl;
        return false;
    }

    //second check that it doesn't exist
    for(int i = 0; i < 64; ++i) //max amount of files in a directory
    {
        if(dir.entries[i].access_rights != 0)
        {
            if(strcmp(dir.entries[i].file_name, filename.c_str()) == 0)
            {
                std::cout << "A file already exists with this name in this directory" << std::endl;
                return false;
            }
        }
    }

    return true;
}

int FS::FindFile(const std::string& filename, const DirBlock& dirBlock)
{
    for(int i = 0; i < 64; ++i)
    {
        if(dirBlock.entries[i].access_rights != 0 && dirBlock.entries[i].type == TYPE_FILE)
        {
            if(strcmp(dirBlock.entries[i].file_name, filename.c_str()) == 0)
            {
                return i;
            }
        }
    }

    return -1;
}

int FS::FindDirectory(const std::string& dir, const DirBlock& dirBlock)
{
    for(int i = 0; i < 64; ++i)
    {
        if(dirBlock.entries[i].access_rights != 0 && dirBlock.entries[i].type == TYPE_DIR)
        {
            if(strcmp(dir.c_str(), dirBlock.entries[i].file_name) == 0)
                return i;
        }
    }
    return -1;
}

void FS::InitDir(DirBlock &dir)
{
    for(int i = 0; i < 64; ++i)
    {
        dir.entries[i].access_rights = 0;
    }
}

std::pair<int, DirBlock> FS::FindRm(const std::string& filepath)
{
    //begin by loading the next last directory
    std::pair<DirBlock, std::string> dir = GetDir(filepath);

    //find last directory
    if(dir.first.blockNo != -1)
    {
        for(int i = 0; i < 64; ++i)
        {
            if(dir.first.entries[i].access_rights != 0)
            {
                if(strcmp(dir.second.c_str(), dir.first.entries[i].file_name) == 0)
                {
                    return std::make_pair(i, dir.first);
                }
            }
        }
    }

    return std::make_pair(-1, dir.first);
}

std::pair<DirBlock, std::string> FS::GetDir(const std::string& path)
{
    DirBlock dir = currentDir;

    int start = 0;
    int end = path.find('/');

    if(path[0] == '/')
    {
        disk.read(0, (uint8_t*)dir.entries);
        dir.blockNo = 0;
        dir.access_right = 6;

        start = 1;
        end = path.find('/', 1);
    }

    while(end != std::string::npos)
    {
        //we will see if we can find the directory
        int index = FindDirectory(path.substr(start, end - start), dir);

        if(index == -1)
        {
            dir.blockNo = -1;
            std::cout << "Couldn't find the directory" << std::endl;
            return std::make_pair(dir, path);
        }

        if(dir.entries[index].access_rights < 4)
        {
            dir.blockNo = -1;
            std::cout << "Missing permission" << std::endl;
            return std::make_pair(dir, path);
        }

        uint16_t blck = dir.entries[index].first_blk;
        dir.blockNo = blck;
        dir.access_right = dir.entries[index].access_rights;

        disk.read(blck, (uint8_t*)dir.entries);

        start = end + 1;
        end = path.find('/', start);
    }

    return std::make_pair(dir, path.substr(start, end));
}

std::pair<DirBlock, std::string> FS::CdHelper(const std::string& path)
{
    DirBlock dir = currentDir;

    int start = 0;
    int end = path.find('/');

    std::string temp = currDirStr;

    if(path[0] == '/')
    {
        disk.read(0, (uint8_t*)dir.entries);
        dir.blockNo = 0;
        dir.access_right = 6;

        temp = "/";

        start = 1;
        end = path.find('/', 1);
    }

    while(end != std::string::npos)
    {
        //we will see if we can find the directory
        int index = FindDirectory(path.substr(start, end - start), dir);

        if(index == -1)
        {
            dir.blockNo = -1;
            std::cout << "Couldn't find the directory" << std::endl;
            return std::make_pair(dir, path);
        }

        if(dir.entries[index].access_rights < 4)
        {
            dir.blockNo = -1;
            std::cout << "Missing permission" << std::endl;
            return std::make_pair(dir, path);
        }

        uint16_t blck = dir.entries[index].first_blk;
        dir.blockNo = blck;
        dir.access_right = dir.entries[index].access_rights;

        if(strcmp(dir.entries[index].file_name, "..") == 0)
        {
            temp = temp.substr(0, temp.find_last_of('/'));

            //fix when we cd from one directory to root
            if (temp.empty())
                temp = '/';
        }
        else
        {
            if(temp[temp.size()-1] != '/')
                temp += "/";

            temp += dir.entries[index].file_name;
        }
        disk.read(blck, (uint8_t*)dir.entries);

        start = end + 1;
        end = path.find('/', start);
    }

    currDirStr = temp;
    return std::make_pair(dir, path.substr(start, end));
}
;;