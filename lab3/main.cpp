#include "shell.h"
#include "fs.h"
#include "disk.h"

int
main(int argc, char **argv)
{
    //FS filesystem;

    Shell shell;
    shell.run();
    return 0;
}
