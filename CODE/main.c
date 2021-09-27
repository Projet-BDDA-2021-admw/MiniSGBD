

#include <stdio.h>

#include "DiskManager/DBParams.h"
#include "DiskManager/DiskManager.h"

extern DBParams params;

int main(int argc, char **argv) {
    if (argc != 1) {
        fprintf(stderr, "Utilisation: %s CHEMIN_VERS_BD (il manque un argument)\n", argv[0]);
        return -1;
    }
    params.DBPath = argv[1]; //todo : vérifier que c'est un chemin valide
    params.pageSize = 4096;
    params.maxPagesPerFile = 4;
    initDiskManager();

    printf("Bonjour :)\n");
    return 0;
}
