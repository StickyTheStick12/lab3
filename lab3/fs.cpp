#include <iostream>
#include <algorithm>
#include "fs.h"

//TODO:
// Fix numbers
// format:
// Check for correct values
// --
// cat
// --
// create:
// incorporate file creation check
// --


FatEntry FAT[1024];
Block buffer;
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
    //Remove all data from virtdisk
    std::fill(std::begin(buffer.data), std::end(buffer.data), 0);

    for(int i = 0; i < 1024; ++i)
    {
        disk.write(i, (uint8_t*)&buffer);
    }

    //create a new rootDir
    Block rootDir;

    //fill rootDir with zero
    std::fill(std::begin(rootDir.data), std::end(rootDir.data), 0);
    rootDir.dir.isdir = true;
    rootDir.dir.nextEntry = 0;

    //write rootDir
    buffer = rootDir;
    disk.write(0, (uint8_t*)&buffer);

    //set all fat entries to free
    std::fill(std::begin(FAT), std::end(FAT), FAT_FREE);

    FAT[0] = FAT_EOF;
    FAT[1] = FAT_EOF;

    //save fat
    SaveFat();

    std::cout << "FS::format()\n";
    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int
FS::create(std::string filepath)
{
    //allocate a file
    File file;
    file.pos = 0;
    std::fill(std::begin(file.buffer->data), std::end(file.buffer->data), 0);

    int index = GetUnusedBlock();

    dir_entry newEntry;
    newEntry.size = 0;
    newEntry.type = 0;
    newEntry.first_blk = index;
    std::copy(std::begin(filepath), std::end(filepath), newEntry.file_name);

    //save to directory
    FAT[index] = FAT_EOF;

    SaveFat();

    //read chars and put them into the buffer
    std::string str;

    while(getline(std::cin, str))
    {
        if(str.empty())
            break;
        for(int i = 0; i < str.size(); ++i)
        {
            WriteChar(file, str[i]);
        }
    }

    std::cout << "FS::create(" << filepath << ")\n";
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
    for(int i = 0; i < 128; ++i) //max files in a directory
    {
        if(currentDir.entries[i].isUsed)
        {
            std::cout << currentDir.entries[i].file_name << "       " << currentDir.entries[i].size << std::endl;
        }
    }

    std::cout << "FS::ls()\n";
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
