#include "launchpad.h"
#include <stdio.h>

int main(int argc, const char** argv) {
    if (update_from_repo("git@github.com:isaiahparton/auto-updater.git", "./app") != 0)
    {
        printf("Failed to update app, launching anyway\n");
    }
#ifdef WIN32
    system("./app/ShopkeeperClient.exe");
#else
    system("./app/ShopkeeperClient");
#endif
    return 0;
}
