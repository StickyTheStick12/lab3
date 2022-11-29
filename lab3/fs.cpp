#include <iostream>
#include <algorithm>
#include <iomanip>
#include "fs.h"

//TODO:
// WRITECHAR
// CAT
//


//TODO:
// Fix numbers
// --


FatEntry FAT[1024];
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
    Datablock buffer;
    //create a datablock and fill with zero
    std::fill(std::begin(buffer), std::end(buffer), 0);

    //We will overwrite the whole disk with this block to reset anything that is left
    for(int i = 0; i < 1024; ++i) //Change this to how many block we want
        disk.write(i, (uint8_t*)&buffer);


    //create a new rootDir
    DirBlock rootDir;

    //fill rootDir with zero
    rootDir.isdir = true;

    //write rootDir
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
FS::create(std::string filepath)
{
    // check for file errors
    if(!CheckFileCreation(filepath))
    {
        std::cout << "Couldn't create this file. An error with the filename occured";
        return -1;
    }

    //allocate a file
    File file;
    file.pos = 0;
    std::fill(std::begin(file.buffer->data), std::end(file.buffer->data), 0);

    int index = GetUnusedBlock();

    dir_entry newEntry;
    newEntry.size = 0;
    newEntry.type = TYPE_FILE;
    newEntry.first_blk = index;
    std::copy(std::begin(filepath), std::end(filepath), newEntry.file_name);

    //save to directory
    FAT[index] = FAT_EOF;

    //save the fat
    SaveFat();

    std::string str;

    //Continue to read forever
    while(getline(std::cin, str))
    {
        //if string is empty we will save the file
        if(str.empty())
            break;
        //otherwise send one char to writechar
        for(int i = 0; i < str.size(); ++i)
        {
            WriteChar(file, str[i]);
        }
    }

    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filepath)
{
    //loop through fat table and find file
    // we will then read the file and follow

    File file;

    DirBlock directory = FindDirectory();

    return;

    std::cout << "FS::cat(" << filepath << ")\n";
    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{
    std::cout.fill(' ');
    std::cout << std::setw(30) << std::left << "Filename:";
    std::cout << "Size:" << std::endl;

    for(int i = 0; i < 1024; ++i) //max files in a directory
    {
        //check if entry in entris is used i.e. that something exists in that position
        if(currentDir.entries[i].isUsed)
        {
            std::cout << std::setw(30) << std::left << currentDir.entries[i].file_name;
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
    std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";
    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    std::cout << "FS::pwd()\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
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
    std::copy(std::begin(FAT), std::end(FAT), buffer.fat);
    disk.write(1, (uint8_t*)&buffer);
}

void FS::LoadFat()
{
    disk.read(1, (uint8_t*)&FAT);
}

void FS::WriteChar(File& file, char a)
{
    file.buffer->data[file.pos] = a;
    file.pos++;
    file.dirEntry.size += sizeof(char);

    //if memory becomes full
    if(file.pos == 1024)
    {
        disk.write(file.fatBlock, (uint8_t*)&file->buffer->data);
        //write to disk

        int indexNextBlock = GetUnusedBlock();
        FAT[file.fatBlock] = indexNextBlock;
        FAT[indexNextBlock] = FAT_EOF;
        SaveFat();
        file.fatBlock = indexNextBlock;
        file.pos = 0;
    }
}

DirBlock* FS::FindDirectory(const std::string path, std::string filename)
{
    std::string blockPath;

    DirBlock parentDir;

    int isAbsolute = blockPath[0] == '/';

    if(blockPath[0] == '.' && strlen(blockPath) == 1) {
        return &virtualDisk[currentDir->firstblock].dir;//currentDir->firstblock
    }
    if(blockPath[0] == '.' && blockPath[1] == '.' && strlen(blockPath) == 2) {
        if(currentDir == NULL) {
            printf("Couldn't find directory\n");
            return NULL;
        } else {
            dirblock_t *parentBlock = findParentBlock(rootDirectoryBlock, &virtualDisk[currentDir->firstblock].dir);
            if(parentBlock == rootDirectoryBlock) {
                printf("You are in the root directory");
                return NULL;
            }
            return parentBlock;
        }
    }

    if(isAbsolute || currentDir == NULL) parentDirectoryBlock = rootDirectoryBlock;
    else parentDirectoryBlock = &(virtualDisk[currentDir->firstblock].dir);

    char *head, *tail = blockPath;
    while ((head = strtok_r(tail, "/", &tail))) {
        // If current token is file set filename
        // and return its parent directory
        if(strchr(head, '.') != NULL) {
            *filename = malloc(MAXNAME);
            strcpy(*filename, head);
            return parentDirectoryBlock;
        }

        int found = 0;
        for(int i = 0; i < parentDirectoryBlock->nextEntry; i++) {
            if(strcmp(parentDirectoryBlock->entrylist[i].name, head) == 0 &&
               parentDirectoryBlock->entrylist[i].isdir) {
                parentDirectoryBlock = &(virtualDisk[parentDirectoryBlock->entrylist[i].firstblock].dir);
                found = 1;
            }
        }

        // If directory not found and modify is set to true, create new directory
        if(found == 0 && modify == 0) return NULL;
        else if(found == 0 && modify) parentDirectoryBlock = createDirectoryBlock(parentDirectoryBlock, head);

    }
    return parentDirectoryBlock;
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
                return true;
        }
    }

    return false;
};;
