#include <stdio.h>
#include "FAT.h"

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("usage: %s <file input>\n", argv[0]);
    return 1;
  }
  loadDiskImage(argv[1]);
  initGUI();
  freeResources();
  return 0;
}