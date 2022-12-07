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
// APPEND

//TODO:
// append --- accessrights, abs/rel path
// cd --- accessrights, abs/rel path, make sure accessrights is changed when we cd
// chmod --- abs/rel path
// mv --- accessrights, abs/rel path
// rm --- accessrights, abs/rel path


std::string currDirStr = "/";
DirBlock currentDir;

//when we change directory we will add to this otherwise we can save a name in the dirblock

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
    file.dirEntry.access_rights = 7;

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
        return -1;
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

    // check if it exists
    if(destDir.first.blockNo == -1)
    {
        return -1;
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
    //check if sourepath exists
    int fileIndex = FindFile(sourcepath, currentDir);

    if(fileIndex == -1)
    {
        std::cout << "couldn't find source file" << std::endl;
        return -1;
    }

    if(CheckFileCreation(destpath, currentDir))
    {
        strcpy(currentDir.entries[fileIndex].file_name, destpath.c_str());
        disk.write(0, (uint8_t*)currentDir.entries);
    }
    else
        return -1;

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(const std::string& filepath)
{
    //we will first test if a file exists with this filename
    std::pair<int,int> rmPair = FindRm(filepath);

    //check if it was found
    if(rmPair.first == -1)
        return -2;

    //check if it was a directory and if that is true check if that directory is empty
    if(rmPair.second == TYPE_DIR)
    {
        DirBlock temp;

        disk.read(currentDir.entries[rmPair.first].first_blk, (uint8_t*)temp.entries);

        //this should never happen that we need to check for this because it should always contain this in the first spot
        //if(strcmp("..", temp.entries[0].file_name) != 0)
        //{
        //    return -1;
        //}

        for(int i = 1; i < 64; ++i)
        {
            if(temp.entries[i].access_rights != 0)
                return -1;
        }
    }

    //delete from direntries
    currentDir.entries[rmPair.first].access_rights = 0;

    int fIndex = currentDir.entries[rmPair.first].first_blk;

    while(fIndex != -1)
    {
        int nextIndex = FAT[fIndex];
        FAT[fIndex] = FAT_EOF;
        fIndex = nextIndex;
    }

    disk.write(currentDir.blockNo, (uint8_t*)currentDir.entries);

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(const std::string& filepath1, const std::string& filepath2)
{
    int copyFrom = FindFile(filepath1, currentDir);
    int writeTo = FindFile(filepath2, currentDir);

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

    //first thing we want to do is check if the size is divsible by 1024. If it is that means that the buffer is full
    //if it is true we will see if we can create a new fat block and find the latest entry in

    // if it is true we will find the latest fat entry







    //first thing we want to do is find latest fat entry for write to file.

    //we want to check if the latest

    //First thing we want to do is check if the writeTo file latest buffer is full. If it is we dont have to do much just copy form the other and write

    //check if the size is 0 in other words that we have filled the latest buffer slot
    if(currentDir.entries[writeTo].size % 1024)
    {
        int blk;
        int blkValue = currentDir.entries[writeTo].first_blk;
        while(blkValue != -1)
        {
            blk = blkValue;
            blkValue = FAT[blk];
            //continue to traverse fat until we reach end of file i.e., -1
        }
    }

    //if we get zero after modulo 1024 we can just change the fat entry at the end and copy the buffer from the other
    if(currentDir.entries[writeTo].size % 1024)
    {


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

    if(!CheckFileCreation(dirpath, dirPair.first))
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
    nEntry.first_blk = 0;
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
    // first we will look for dirpath
    int dirIndex = FindDirectory(dirpath);

    //if we cant find it return -1;
    if(dirIndex == -1)
    {
        std::cout << "couldn't find this directory" << std::endl;
        return -1;
    }

    //otherwise load
    currentDir.access_right = currentDir.entries[dirIndex].access_rights;
    disk.read(currentDir.entries[dirIndex].first_blk, (uint8_t*)currentDir.entries);
    currentDir.blockNo = dirIndex;


    //add to our current string
    if(dirpath == "..")
    {
        currDirStr = currDirStr.substr(0, currDirStr.find_last_of('/'));

        //fix when we cd from one directory to root
        if(currDirStr.empty())
            currDirStr += '/';

        //find latest / and remove everything to the right
    }
    else
    {
        if(currDirStr.back() != '/')
            currDirStr += '/';

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
FS::chmod(const std::string& accessrights, const std::string& filepath)
{
    std::pair<int, int> filePair = FindRm(filepath);

    if(filePair.first == -1)
        return -1;

    currentDir.entries[filePair.first].access_rights = stoi(accessrights);

    disk.write(currentDir.blockNo, (uint8_t*)currentDir.entries);

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

int FS::FindDirectory(const std::string& dir)
{
    for(int i = 0; i < 64; ++i)
    {
        if(currentDir.entries[i].access_rights != 0 && currentDir.entries[i].type == TYPE_DIR)
        {
            if(strcmp(dir.c_str(), currentDir.entries[i].file_name) == 0)
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

std::pair<int,int> FS::FindRm(const std::string& filepath)
{
    //for each entry in directory
    for(int i = 0; i < 64; ++i)
    {
        if(currentDir.entries[i].access_rights != 0)
        {
            if(strcmp(filepath.c_str(), currentDir.entries[i].file_name) == 0)
            {
                return std::make_pair(i, currentDir.entries[i].type);
            }
        }
    }

    return std::make_pair(-1, -1);
}

std::pair<DirBlock, std::string> FS::GetDir(const std::string& path)
{
    DirBlock dir = currentDir;

    if(path[0] == '/')
    {
        disk.read(0, (uint8_t*)dir.entries);
        dir.blockNo = 0;
        dir.access_right = 6;
    }

    int start = 0;
    int end = path.find('/');

    while(end != std::string::npos)
    {
        //we will see if we can find the directory
        int index = FindDirectory(path.substr(start, end - start));

        if(index == -1)
        {
            dir.blockNo = -1;
            std::cout << "Couldn't find the directory" << std::endl;
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
;;