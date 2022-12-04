#include "shell.h"
#include "fs.h"
#include "disk.h"

int
main(int argc, char **argv)
{
    //FS filesystem;

    char arr[10];

    arr[0] = 'a';
    arr[1] = 'b';
    arr[2] = 'b';
    arr[3] = 'a';

    std::cout << arr << std::endl;

    Shell shell;
    shell.run();
    return 0;
}
