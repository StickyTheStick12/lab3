#include <iostream>
#include <cstdint>
#include "disk.h"

#ifndef __FS_H__
#define __FS_H__

#define ROOT_BLOCK 0
#define FAT_BLOCK 1
#define FAT_FREE 0
#define FAT_EOF -1

#define TYPE_FILE 0
#define TYPE_DIR 1
#define READ 0x04
#define WRITE 0x02
#define EXECUTE 0x01

typedef char Datablock[4096];

//TODO: OBS!!!!!!!! we can change uint8t to a signed integer and thus store -1 as the access right if we want to remove  this because ze
// zero can be allowed if we doesnt have access at all.

struct dir_entry{
    char file_name[56]; // name of the file / sub-directory
    uint32_t size; // size of the file in bytes
    uint16_t first_blk; // index in the FAT for the first block of the file
    uint8_t type; // directory (1) or file (0)
    uint8_t access_rights; // read (0x04), write (0x02), execute (0x01)
};

struct DirBlock
{
    dir_entry entries[64]; // change this
    int16_t blockNo;
    uint8_t access_right;
};


struct File
{
    int pos;
    char mode[3];
    Datablock buffer;
    dir_entry dirEntry;
};

class FS {
private:
    Disk disk;
    // size of a FAT entry is 2 bytes
    int16_t FAT[BLOCK_SIZE/2];

public:
    FS();
    ~FS();
    // formats the disk, i.e., creates an empty file system
    int format();
    // create <filepath> creates a new file on the disk, the data content is
    // written on the following rows (ended with an empty row)
    int create(const std::string& filepath);
    // cat <filepath> reads the content of a file and prints it on the screen
    int cat(const std::string& filepath);
    // ls lists the content in the current directory (files and sub-directories)
    int ls();

    // cp <sourcepath> <destpath> makes an exact copy of the file
    // <sourcepath> to a new file <destpath>
    int cp(const std::string& sourcepath, const std::string& destpath);
    // mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
    // or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
    int mv(const std::string& sourcepath, const std::string& destpath);
    // rm <filepath> removes / deletes the file <filepath>
    int rm(const std::string& filepath);
    // append <filepath1> <filepath2> appends the contents of file <filepath1> to
    // the end of file <filepath2>. The file <filepath1> is unchanged.
    int append(const std::string& filepath1, const std::string& filepath2);

    // mkdir <dirpath> creates a new sub-directory with the name <dirpath>
    // in the current directory
    int mkdir(const std::string& dirpath);
    // cd <dirpath> changes the current (working) directory to the directory named <dirpath>
    int cd(const std::string& dirpath);
    // pwd prints the full path, i.e., from the root directory, to the current
    // directory, including the current directory name
    int pwd();

    // chmod <accessrights> <filepath> changes the access rights for the
    // file <filepath> to <accessrights>.
    int chmod(const std::string& accessrights, const std::string& filepath);

private:
    //#-----FAT FUNCTIONS-----#
    void SaveFat();
    int GetUnusedBlock();

    //#-----FILE FUNCTIONS-----#
    bool CheckFileCreation(const std::string& filename, const DirBlock& dir);
    int FindFile(const std::string& filename, const DirBlock& dirBlock);

    //#-----DIRECTORY FUNCTIONS-----#
    int FindFreeDirPlace(DirBlock& dir);
    int FindDirectory(const std::string& dir, const DirBlock& dirBlock);
    void InitDir(DirBlock& dir);
    std::pair<DirBlock, std::string> GetDir(const std::string& path);

    std::pair<int, DirBlock> FindRm(const std::string& filepath);
    std::pair<DirBlock, std::string> CdHelper(const std::string& path);
};

#endif // __FS_H__