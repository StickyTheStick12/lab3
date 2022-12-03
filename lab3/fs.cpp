#include <iostream>
#include <iomanip>
#include <cstring>
#include "fs.h"

//mv can rename a file to an already existing filename
//pwd doesnt remove anything so when we cd to a new directory we get //dir instead of /dir and when we go back we get //dir/..

//TODO:
// APPEND
// PWD

//TODO:
// append
// rm with directories

//TODO:
// aboslute/relative path
// create
// append
// mkdir
// cd
// pwd


//TODO:
// Fix numbers

DirBlock currentDir;

//when we change directory we will add to this otherwise we can save a name in the dirblock
std::string currDirStr = "/";
//change this to a list for easy removal?


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
    Datablock buffer;
    //create a datablock and fill with zero
    std::fill(std::begin(buffer), std::end(buffer), 0);

    //We will overwrite the whole disk with this block to reset anything that is left
    for(int i = 0; i < 1024; ++i) //Change this to how many block we want
        disk.write(i, (uint8_t*)&buffer);

    //create a new rootDir
    DirBlock rootDir;

    InitDir(rootDir);

    //write rootDir to disk
    disk.write(0, (uint8_t*)&rootDir);

    //save rootDir as our current dir
    currentDir = rootDir;

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
    // check for file errors
    if(!CheckFileCreation(filepath))
    {
        std::cout << "Couldn't create this file. An error with the filename occurred";
        return -1;
    }

    //check that we can add it to our fat
    int fatIndex = GetUnusedBlock();

    if(fatIndex == -1)
    {
        std::cout << "couldn't allocate a fat entry, the FAT is probably full" << std::endl;
        return -1;
    }

    //check that we can add to the current directory
    int dirIndex = FindFreeDirPlace(currentDir);

    if(dirIndex == -1)
    {
        std::cout << "couldn't create a file in this directory, it either dont exist or is full" << std::endl;
        return -1;
    }

    //allocate a file
    File file;
    file.pos = 0;
    file.dirEntry.size = 0;
    file.dirEntry.type = TYPE_FILE;
    file.dirEntry.first_blk = fatIndex;
    std::copy(std::begin(filepath), std::end(filepath), file.dirEntry.file_name);
    file.dirEntry.file_name[filepath.size()] = '\0';
    file.dirEntry.isUsed = true;

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
        //otherwise send one char to writechar
        for(int i = 0; i < str.size(); ++i)
        {
            file.buffer[file.pos] = str[i];
            file.pos++;
            file.dirEntry.size += sizeof(char);

            //if memory becomes full
            if(file.pos == 1023)
            {
                file.buffer[file.pos++] = '\0';
                disk.write(writePos, (uint8_t*)&file.buffer);
                //write to disk

                int indexNextBlock = GetUnusedBlock();
                FAT[writePos] = indexNextBlock;
                FAT[indexNextBlock] = FAT_EOF;
                SaveFat();
                file.pos = 0;
                writePos = indexNextBlock;
            }
        }
    }

    if((file.dirEntry.size % 1023) > 0)
    {
        //if the size is not a multiple of 1024 it have not been written to disk
        file.buffer[file.pos++] = '\0';
        disk.write(writePos, (uint8_t*)file.buffer);
    }

    //save direntry to the directory
    currentDir.entries[dirIndex] = file.dirEntry;

    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filepath)
{
    //check that file actually exists
    int fileIndex = FindFile(filepath);

    if(fileIndex == -1)
    {
        std::cout << "couldn't find file, it probably doesn't exists in this directory" << std::endl;
        return -1;
    }

    //allocate a new FatEntry
    FatEntry fatIndex = currentDir.entries[fileIndex].first_blk;
    Datablock buffer;

    //fetch the first block for the file

    do
    {
        //fetch file
        disk.read(fatIndex, (uint8_t*)buffer);

        //print the content
        for(int i = 0; i < 1024/4; ++i)
        {
            std::cout << buffer[i];
        }

        //fetch next fat block
        fatIndex = FAT[fatIndex];
    }
    while(fatIndex != -1);    //check if the fat points to anymore data

    std::cout << std::endl;

    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{
    std::cout.fill(' ');
    std::cout << std::setw(30) << std::left << "Filename:";
    std::cout << std::setw(30) << std::left << "type:";
    std::cout << "Size:" << std::endl;

    for(int i = 0; i < 1024; ++i) //max files in a directory
    {
        //check if entry in entris is used i.e. that something exists in that position
        if(currentDir.entries[i].isUsed)
        {
            std::cout << std::setw(30) << std::left << currentDir.entries[i].file_name;
            if(currentDir.entries[i].type == TYPE_DIR)
                std::cout << std::setw(30) << std::left << "Dir";
            else
                std::cout << std::setw(30) << std::left << "File";

            std::cout << currentDir.entries[i].size << std::endl;
        }
    }

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(std::string sourcepath, std::string destpath)
{
    //begin with checking if destpath exists
    if(!CheckFileCreation(destpath))
    {
        std::cout << "there was an error with the destpath when copying the file. The destpath most probably "
                     "already exists in this directory" << std::endl;
    }

    // next see if we can add it to the fat
    int fatIndexNewFile = GetUnusedBlock();

    if(fatIndexNewFile == -1)
    {
        std::cout << "Couldn't allocate any new space on the fat. It is most probably full" << std::endl;
        return -1;
    }

    // see if we can add it to the directory
    int dirIndex = FindFreeDirPlace(currentDir);

    if(dirIndex == -1)
    {
        std::cout << "couldn't allocate any more space in this directory" << std::endl;
        return -1;
    }

    // see if source exists
    int sourceFileIndex = FindFile(sourcepath);

    if(sourceFileIndex == -1)
    {
        std::cout << "there was an error with the sourcepath when copying. The file probably dont exists in this dir"
        << std::endl;
        return -1;
    }

    //allocate a file
    File file;
    file.pos = 0;
    std::fill(std::begin(file.buffer), std::end(file.buffer), 0);

    //give it a name
    std::copy(std::begin(destpath), std::end(destpath), file.dirEntry.file_name);
    file.dirEntry.file_name[destpath.size()] = '\0';

    //give it a size
    file.dirEntry.size = currentDir.entries[sourceFileIndex].size;
    file.dirEntry.type = TYPE_FILE;
    file.dirEntry.first_blk = fatIndexNewFile;
    file.dirEntry.isUsed = true;

    //save dirEntry to dir
    currentDir.entries[dirIndex] = file.dirEntry;

    //save dir to disk
    disk.write(0, (uint8_t*)currentDir.entries);

    //save to Fat
    FAT[fatIndexNewFile] = FAT_EOF;

    //save the fat
    SaveFat();

    //fetch the first block for the file
    int fatIndexOldFile = currentDir.entries[sourceFileIndex].first_blk;

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

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    //check if sourepath exists
    int fileIndex = FindFile(sourcepath);

    if(fileIndex == -1)
    {
        std::cout << "couldn't find source file" << std::endl;
        return -1;
    }

    if(CheckFileCreation(destpath))
    {
        strcpy(currentDir.entries[fileIndex].file_name, destpath.c_str());
    }

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(const std::string& filepath)
{
    //We cant remove it from disk without overwriting it which is unnecessary.

    //first check if filepath exists

    int fileIndex = FindFile(filepath);

    if(fileIndex == -1)
    {
        std::cout << "couldn't remove this file. It probably doesn't exists" << std::endl;
        return -1;
    }

    //delete from direntries
    currentDir.entries[fileIndex].isUsed = false;
    int nextIndex = fileIndex;

    do
    {
        nextIndex = FAT[currentDir.entries[fileIndex].first_blk];
        FAT[currentDir.entries[fileIndex].first_blk] = FAT_EOF;

        fileIndex = nextIndex;
    }
    while(fileIndex != -1);

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    int copyFrom = FindFile(filepath1);
    int writeTo = FindFile(filepath2);

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

    Datablock buffer;

    //if we get zero after modulo 1024 we can just change the fat entry at the end and copy the buffer from the other
    if(currentDir.entries[writeTo].size % 1024)
    {
        int index = currentDir.entries[writeTo].first_blk;
        while(index != -1)
        {
            //continue to traverse fat until we reach end of file i.e., -1
            index = FAT[index];
        }

        //now we have a position to write into

        //we will now find the first block in the other file and fetch that data
        int indexCopy = currentDir.entries[copyFrom].first_blk;

        while(indexCopy != -1)
        {
            //read block from file
            disk.read(FAT[indexCopy], (uint8_t*)buffer);

            // find free fatblock to place this
            int fFat = GetUnusedBlock();

            //write buffer to disk
            disk.write(fFat, (uint8_t*)buffer);

            FAT[index] = fFat;

            FAT[fFat] = FAT_EOF;

            index = fFat;

            indexCopy = FAT[indexCopy];
        }
    }
    else
    {
        //find the latest Fat in the writefile
        int index = currentDir.entries[writeTo].first_blk;
        while(index != -1)
        {
            //continue to traverse fat until we reach end of file i.e., -1
            index = FAT[index];
        }

        int indexCopy = currentDir.entries[copyFrom].first_blk;
        File file;
        file.pos = (currentDir.entries[copyFrom].size%1024)/4;
        file.dirEntry.first_blk = index;

        int j = 0;

        while(indexCopy != -1)
        {
            //read block from file
            disk.read(FAT[indexCopy], (uint8_t*)buffer);

            if(currentDir.entries[copyFrom].size)

            //for(int i = 0; i < currentDir.entries[copyFrom].size)

            //WriteChar(file, a)

            // find free fatblock to place this
            int fFat = GetUnusedBlock();

            //write buffer to disk
            //disk.write(fFat, (uint8_t*)buffer);

            //FAT[index] = fFat;

            //FAT[fFat] = FAT_EOF;

            //index = fFat;

            indexCopy = FAT[indexCopy];
        }


        //find the first fat in the copyfile

        // for each character in buffer call on writechar
    }

    //we have 2 cases




    // case 2
    // we have to copy char for char into the new buffer


    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    //find free fatblock
    int freeFat = GetUnusedBlock();

    if(freeFat == -1)
    {
        std::cout << "couldn't allocate anymore entries in the fat" << std::endl;
    }

    //find empty dirPlace
    int fDir = FindFreeDirPlace(currentDir);

    if(fDir == -1)
    {
        std::cout << "couldn't find a empty position in dir" << std::endl;
    }

    // mark Fat as end of file
    FAT[freeFat] = FAT_EOF;

    //save fat
    SaveFat();

    //create a new dir_entry
    dir_entry nEntry;

    nEntry.type = TYPE_DIR;
    std::copy(std::begin(dirpath), std::end(dirpath), nEntry.file_name);
    nEntry.file_name[dirpath.size()] = '\0';
    nEntry.first_blk = freeFat;
    nEntry.isUsed = true;

    //add to current directory
    currentDir.entries[fDir] = nEntry;

    //save dir
    disk.write(0, (uint8_t*)currentDir.entries);

    //allocate a new DirBlock
    DirBlock nDir;

    //create a new entry to put our root dir in
    nEntry.first_blk = 0;
    strcpy(nEntry.file_name, "..");

    nDir.entries[0] = nEntry;

    //save to disk

    disk.write(freeFat, (uint8_t*)nDir.entries);

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    // begin with one level

    // first we will look for dirpath
    int dirIndex = FindDirectory(dirpath);

    //if we cant find it return -1;
    if(dirIndex == -1)
    {
        std::cout << "couldn't find this directory" << std::endl;
        return -1;
    }

    //otherwise load
    disk.read(currentDir.entries[dirIndex].first_blk, (uint8_t*)currentDir.entries);

    //add to our current string
    if(strcmp(currentDir.entries[dirIndex].file_name, "..") == 0)
    {
        //find latest / and remove everything to the right
        currDirStr = currDirStr.substr(0, currDirStr.find_last_of('/'));
    }
    else
    {
        currDirStr += dirpath;
    }

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
FS::chmod(std::string accessrights, std::string filepath)
{
    //check if accessright is odd, if it is then we know that we can subtract

    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}

int FS::GetUnusedBlock()
{
    for(int i = 2; i < 1024; ++i)
    {
        if(FAT[i] == FAT_FREE)
            return i;
    }
}

void FS::SaveFat()
{
    disk.write(1, (uint8_t*)&FAT);
}

void FS::WriteChar(File& file, char a)
{
    file.buffer[file.pos] = a;
    file.pos++;
    file.dirEntry.size += sizeof(char);
    int writePos = file.dirEntry.first_blk;

    //if memory becomes full
    if(file.pos == 1024)
    {
        disk.write(writePos, (uint8_t*)&file.buffer);
        //write to disk

        int indexNextBlock = GetUnusedBlock();
        FAT[file.dirEntry.first_blk] = indexNextBlock;
        FAT[indexNextBlock] = FAT_EOF;
        SaveFat();
        file.pos = 0;
    }
}

int FS::FindFreeDirPlace(DirBlock& dir)
{
    //loop through all entries
    for(int i = 0; i < 1024; ++i)
    {
        //check if entry is used
        if(!dir.entries[i].isUsed)
            return i;
    }

    //if we reach here we return -1 because it is full
    return -1;
}

bool FS::CheckFileCreation(const std::string& filename)
{
    //first check size that we can fit the filename in our char array. If we cant we cant create the file
    if(filename.size() > 54)
        return false;

    //second check that it doesn't exist
    for(int i = 0; i < 1024; ++i) //max amount of files in a directory
    {
        for (int j = 0; j < 56; ++j)
        {
            if(currentDir.entries[i].file_name[j] != filename[j])
                break;
        }
    }

    return true;
}

int FS::FindFile(const std::string& filename)
{
    for(int i = 0; i < 1024; ++i)
    {
        if(currentDir.entries[i].isUsed && currentDir.entries[i].type == TYPE_FILE)
        {
            if(strcmp(currentDir.entries[i].file_name, filename.c_str()) == 0)
            {
                return i;
            }
        }
    }
}

int FS::FindDirectory(const std::string& dir)
{
    for(int i = 0; i < 1024; ++i)
    {
        if(currentDir.entries[i].isUsed && currentDir.entries[i].type != TYPE_DIR)
        {
            int j = 0;
            for(j; j < 54; ++j)
            {
                if(dir[j] != currentDir.entries[j].file_name[j])
                    break;
            }

            if(j == 54)
            {
                return i;
            }
        }
    }
}

void FS::InitDir(DirBlock &dir)
{
    for(int i = 0; i < 1024; ++i)
    {
        dir.entries[i].isUsed = false;
    }
}

;;