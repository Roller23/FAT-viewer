#include "FAT.h"

static struct global_data_t global_data;

int loadDiskImage(const char *name) {
  FILE *diskFile = fopen(name, "rb");
  if (diskFile == NULL) {
    printf("Couldn't open %s\n", name);
    return 1;
  }

  global_data.BS = calloc(1, sizeof(BootSector_t));

  if (global_data.BS == NULL) {
    printf("Couldn't allocate memory\n");
    fclose(diskFile);
    return 1;
  }

  fread(global_data.BS, sizeof(BootSector_t), 1, diskFile);

  BootSector_t *BS = global_data.BS;

  uint32_t number_of_sectors = MAX(BS->number_of_sectors_2b, BS->number_of_sectors_4b);

  uint32_t FAT_in_bytes = BS->size_of_FAT * BS->bytes_per_sector;
  uint32_t cluster_in_bytes = BS->sectors_per_cluster * BS->bytes_per_sector;
  uint8_t **FATs = calloc(BS->FATs, sizeof(uint8_t *));

  if (FATs == NULL) {
    printf("Couldn't allocate memory\n");
    free(BS);
    return 1;
  }
  
  for (int i = 0; i < BS->FATs; i++) {
    FATs[i] = calloc(FAT_in_bytes, sizeof(uint8_t));
    if (FATs[i] == NULL) {
      for (int j = 0; j < i; j++) {
        free(FATs[j]);
      }
      free(FATs);
      free(BS);
      fclose(diskFile);
      printf("Couldn't allocate memory\n");
      return 1;
    }
    fread(FATs[i], FAT_in_bytes, 1, diskFile);
  }

  uint8_t *FAT = FATs[0];

  for (int i = 1; i < BS->FATs; i++) {
    free(FATs[i]);
  }
  free(FATs);

  uint32_t root_in_bytes = BS->max_files_in_root * sizeof(FileEntry_t);
  uint32_t root_in_sectors = root_in_bytes / BS->bytes_per_sector;
  FileEntry_t *rootEntries = calloc(BS->max_files_in_root, sizeof(FileEntry_t));

  if (rootEntries == NULL) {
    free(FAT);
    free(BS);
    fclose(diskFile);
    printf("Couldn't allocate memory\n");
    return 1;
  }

  fread(rootEntries, root_in_bytes, 1, diskFile);

  uint32_t loaded_sectors = 1 + (BS->FATs * BS->size_of_FAT) + root_in_sectors;
  uint32_t remaining_sectors = number_of_sectors - loaded_sectors;
  uint32_t remaining_entries = (remaining_sectors * BS->bytes_per_sector) / sizeof(FileEntry_t);
  FileEntry_t *dataSection = calloc(remaining_entries, sizeof(FileEntry_t));

  if (dataSection == NULL) {
    free(FAT);
    free(BS);
    free(rootEntries);
    fclose(diskFile);
    printf("Couldn't allocate memory\n");
    return 1;
  }

  fread(dataSection, BS->bytes_per_sector, remaining_sectors, diskFile);

  global_data.FAT = FAT;
  global_data.dataSection = dataSection;
  global_data.rootEntries = rootEntries;

  fclose(diskFile);
  return 0;
}

void initGUI(void) {
  char buffer[BUFFER_SIZE];
  while (true) {
    memset(buffer, 0, BUFFER_SIZE);
    printf("> ");
    fgets(buffer, BUFFER_SIZE, stdin);
    uint32_t length = strlen(buffer);
    if (buffer[length - 1] == '\n') {
      buffer[length - 1] = 0;
    } else {
      while (getchar() != '\n');
    }
    if (strcmp("exit", buffer) == 0) {
      break;
    }
    handleCommand(buffer);
  }
}

void freeResources(void) {
  free(global_data.FAT);
  free(global_data.dataSection);
  free(global_data.rootEntries);
  free(global_data.BS);
}

static void makeHistoryBackup(void) {
  global_data.historyIndexBackup = global_data.historyIndex;
  memcpy(global_data.historyBackup, global_data.directoryHistory, MAX_DEPTH);
}

static void restoreHistory(void) {
  global_data.historyIndex = global_data.historyIndexBackup;
  memcpy(global_data.directoryHistory, global_data.historyBackup, MAX_DEPTH);
}

static File_t *goAndFetch(char *path, bool restore_history) {
  // goes to the specified path and fetches the file/folder if possible
  // this function changes path history, so it's possibe to restore it to
  // the previous state if necessary
  if (path == NULL) {
    return NULL;
  }
  makeHistoryBackup();
  if (*path == '/') {
    global_data.historyIndex = 0;
  }
  char *dup = strdup(path);
  if (dup == NULL) {
    restoreHistory();
    return NULL;
  }
  char *chunk = strtok(dup, "/");
  char *final_chunk = NULL;
  bool last_file = false;
  while (chunk != NULL) {
    if (strcmp(".", chunk) == 0) {
      chunk = strtok(NULL, "/");
      final_chunk = chunk;
      continue;
    }
    if (strcmp("..", chunk) == 0) {
      chunk = strtok(NULL, "/");
      final_chunk = chunk;
      if (global_data.historyIndex > 0) {
        global_data.historyIndex--;
      }
      continue;
    }
    FileEntry_t *dir = findEntry(chunk);
    if (dir == NULL) {
      restoreHistory();
      free(dup);
      return NULL;
    }
    if (!is_directory(dir)) {
      if (strtok(NULL, "/") != NULL) {
        restoreHistory();
        free(dup);
        return NULL;
      }
      final_chunk = chunk;
      last_file = true;
      break;
    }
    global_data.directoryHistory[++global_data.historyIndex] = dir;
    final_chunk = chunk;
    chunk = strtok(NULL, "/");
  }
  if (!last_file && global_data.historyIndex > 0 && final_chunk != NULL) {
    global_data.historyIndex--;
  }
  if (final_chunk == NULL && global_data.historyIndex == 0) {
    // arrived at root
    free(dup);
    if (restore_history) {
      restoreHistory();
    }
    return ROOT;
  }
  FileEntry_t *entry = findEntry(final_chunk);
  free(dup);
  if (entry == NULL) {
    restoreHistory();
    return NULL;
  }
  File_t *handle = calloc(1, sizeof(File_t));
  if (handle == NULL) {
    restoreHistory();
    return NULL;
  }
  handle->_entry = entry;
  handle->_position = 0;
  handle->_type = is_directory(entry) ? directory : file;
  handle->_size = entry->file_size;
  handle->_opened = true;
  if (restore_history) {
    restoreHistory();
  }
  return handle;
}

File_t *directoryOpen(char *directoryname) {
  // can handle root
  File_t *handle = goAndFetch(directoryname, true);
  if (handle == NULL) {
    restoreHistory();
    return NULL;
  }
  if (handle == ROOT) {
    File_t *root = calloc(1, sizeof(File_t));
    root->_type = directory;
    root->_entry = NULL;
    root->_opened = true;
    return root;
  }
  return handle;
}

File_t *fileOpen(char *filename) {
  // can open both files and directories, not recommended for directories
  // see directoryOpen()
  File_t *handle = goAndFetch(filename, true);
  if (handle == NULL || handle == ROOT) {
    restoreHistory();
    return NULL;
  }
  return handle;
}

int32_t fileRead(char *buffer, size_t size, size_t items, File_t *handle) {
  // returns bytes read on success, FILE_ERROR on error, or FILE_END if reached the EOF
  if (!handle || !handle->_opened || !buffer || handle->_type == directory) {
    return FILE_ERROR;
  }
  size_t to_read = size * items;
  if (to_read < size) {
    // overflow
    return FILE_ERROR;
  }
  size_t remaining_bytes = handle->_size - handle->_position;
  if (remaining_bytes == 0) {
    return FILE_END;
  }
  if (to_read > remaining_bytes) {
    to_read = remaining_bytes;
  }
  uint8_t *contents = getContents(handle->_entry);
  if (contents == NULL) {
    return FILE_ERROR;
  }
  memcpy(buffer, contents + handle->_position, to_read);
  handle->_position += to_read;
  free(contents);
  return to_read;
}

int32_t fileReadChar(File_t *handle) {
  char c;
  int32_t res = fileRead(&c, 1, 1, handle);
  if (res < 0) {
    return res;
  }
  return c;
}

int32_t fileReadDirectory(char *buffer, File_t *handle) {
  static int lastIndex = 0;
  if (!handle || !handle->_opened || handle->_type != directory) {
    return FILE_ERROR;
  }
  FileEntry_t *entry = handle->_entry;
  FileEntry_t *entries = (FileEntry_t *)getContents(entry);
  if (entries == NULL) {
    return FILE_ERROR;
  }
  for (size_t i = 0, entryIndex = 0; true; i++) {
    FileEntry_t entry = entries[i];
    if (lastEntry(&entry)) {
      break;
    }
    if (skippable(&entry)) {
      continue;
    }
    if (entryIndex <= lastIndex) {
      entryIndex++;
      continue;
    }
    char *filename = getFilename(&entry);
    if (filename == NULL) {
      free(entries);
      return FILE_ERROR;
    }
    strcpy(buffer, filename);
    free(filename);
    lastIndex++;
    return 0;
  }
  free(entries);
  lastIndex = 0;
  return FILE_END;
}

void fileSeek(File_t *handle, size_t position) {
  if (!handle || !handle->_opened) {
    return;
  }
  if (position > handle->_size) {
    position = handle->_size;
  }
  handle->_position = position;
}

void fileSeekCurrent(File_t *handle, int32_t offset) {
  if (!handle || !handle->_opened) {
    return;
  }
  int32_t new_position = handle->_position + offset;
  if (new_position < 0) {
    handle->_position = 0;
    return;
  }
  if (new_position > handle->_size) {
    handle->_position = handle->_size;
    return;
  }
  handle->_position = new_position;
}

void fileSeekBeginning(File_t *handle) {
  fileSeek(handle, 0);
}

void fileSeekEnd(File_t *handle) {
  fileSeek(handle, handle->_size);
}

void fileClose(File_t *handle) {
  if (!handle->_opened) {
    return;
  }
  handle->_opened = false;
  free(handle);
}

static char *getFilename(FileEntry_t *entry) {
  char *buffer = calloc(13, sizeof(uint8_t));
  if (!buffer) return NULL;
  uint32_t index = 0;
  for (int i = 0; i < sizeof(entry->filename); i++) {
    if (entry->filename[i] == ' ') break;
    buffer[index++] = tolower(entry->filename[i]);
  }
  if (entry->extension[0] == ' ') {
    // there's no extension
    return buffer;
  }
  buffer[index++] = '.';
  for (int i = 0; i < sizeof(entry->extension); i++) {
    if (entry->extension[i] == ' ') break;
    buffer[index++] = tolower(entry->extension[i]);
  }
  return buffer;
}

static void printFilename(FileEntry_t *entry) {
  char *name = getFilename(entry);
  if (name == NULL) {
    printf("Couldn't read the filename!\n");
    return;
  }
  printf("%s", name);
  free(name);
}

static FileEntry_t *getDirectory(uint32_t index) {
  return global_data.directoryHistory[index];
}

static FileEntry_t *getCurrentDir(void) {
  return getDirectory(global_data.historyIndex);
}

static void printTime(uint16_t time) {
  uint16_t hour = get_hours(time);
  uint16_t minutes = get_minutes(time);
  uint16_t seconds = get_seconds(time);
  printf("%hu:%.2hu:%.2hu", hour, minutes, seconds);
}

static void printDate(uint16_t date) {
  char *months[] = {0, "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  uint16_t year = get_year(date);
  uint16_t month = get_month(date);
  uint16_t day = get_day(date);
  if (month > 12 || day > 31) {
    printf("Invalid date!\n");
    return;
  }
  printf("%s %hu %hu", months[month], day, year);
}

static void printFullDate(uint16_t time, uint16_t date) {
  printTime(time);
  printf(", ");
  printDate(date);
}

static uint16_t get_fat_entry(uint8_t *FAT, uint16_t index) {
  uint32_t fat_offset = index + (index / 2);
  uint16_t entry_value = *(uint16_t *)&FAT[fat_offset];
  if (index & 0x0001) {
    entry_value >>= 4;
    return entry_value;
  } else {
    return (entry_value & 0x0fff);
  }
}

static void dump(void *data, uint32_t size) {
  uint8_t *p = (uint8_t *)data;
  int index = 0;
  while (1) {
    for (int i = 0; i < 12; i++) {
      printf("%.2x ", p[index]);
      index++;
    }
    printf("\n");
    if (index >= size) {
      break;
    }
  }
}

static uint32_t countFATentries(FileEntry_t *entry) {
  uint8_t *FAT = global_data.FAT;
  uint16_t FAT_entry = entry->first_cluster_address_low;
  uint32_t counter = 0;
  while (!(last_entry(FAT_entry) || bad_entry(FAT_entry))) {
    counter++;
    FAT_entry = get_fat_entry(FAT, FAT_entry);
  }
  return counter;
}

static uint8_t *getContents(FileEntry_t *entry) {
  // fetches whatever contents the entry is pointing to
  if (entry == NULL) {
    // it's the root directory
    return (uint8_t *)global_data.rootEntries;
  }
  bool isDirectory = is_directory(entry);
  uint8_t *FAT = global_data.FAT;
  BootSector_t *BS = global_data.BS;
  uint32_t cluster_size = BS->bytes_per_sector * BS->sectors_per_cluster;
  uint16_t FAT_index = entry->first_cluster_address_low;
  uint32_t remaining_data = entry->file_size;
  uint8_t *contents;
  uint32_t temp;
  if (isDirectory) {
    uint32_t clusters = countFATentries(entry);
    contents = calloc(clusters * cluster_size, sizeof(uint8_t));
  } else {
    contents = calloc(entry->file_size + 1, sizeof(uint8_t));
  }
  if (!contents) {
    return NULL;
  }
  uint8_t *start = (uint8_t *)global_data.dataSection;
  uint32_t data_read = 0;
  uint16_t FAT_entry_value = FAT_index;
  while (true) {
    if (bad_entry(FAT_entry_value)) {
      free(contents);
      return NULL;
    }
    if (isDirectory && last_entry(FAT_entry_value)) {
      break;
    }
    uint32_t offset = (FAT_entry_value - 2) * cluster_size;
    uint8_t *content_ptr = start + offset;
    uint32_t to_read = remaining_data > cluster_size ? cluster_size : remaining_data;
    if (isDirectory) {
      to_read = cluster_size;
    }
    memcpy(contents + data_read, content_ptr, to_read);
    data_read += to_read;
    remaining_data -= to_read;
    if (last_entry(FAT_entry_value)) {
      break;
    }
    FAT_entry_value = get_fat_entry(FAT, FAT_entry_value);
  }
  return contents;
}

static void dumpBSInfo(BootSector_t *BS) {
  uint32_t number_of_sectors = MAX(BS->number_of_sectors_2b, BS->number_of_sectors_4b);
  printf("OEM %s\n", BS->OEM);
  printf("Bytes per sector %hu\n", BS->bytes_per_sector);
  printf("Reserved area in sectors %hu\n", BS->reserved_area);
  printf("Number of sectors %u\n", number_of_sectors);
  printf("Number of heads %hu\n", BS->number_of_heads);
  printf("Sectors per cluster %hhu\n", BS->sectors_per_cluster);
  printf("Max files in root directory %hu\n", BS->max_files_in_root);
  printf("Number of FATs %hhu\n", BS->FATs);
  printf("Size of FAT in sectors %hu\n", BS->size_of_FAT);
  printf("Volume label %.11s\n", BS->volume_label);
  printf("File system type %.8s\n\n", BS->system_type_level);
}

static uint32_t countRootEntries(void) {
  FileEntry_t *rootEntries = global_data.rootEntries;
  BootSector_t *BS = global_data.BS;
  uint32_t counter = 0;
  for (int i = 0; i < BS->max_files_in_root; i++) {
    FileEntry_t entry = rootEntries[i];
    if (lastEntry(&entry)) {
      break;
    }
    if (skippable(&entry)) {
      continue;
    }
    counter++;
  }
  return counter;
}

static FileEntry_t *findEntry(const char *name) {
  if (name == NULL || *name == '.') {
    return NULL;
  }
  FileEntry_t *directory = getCurrentDir();
  FileEntry_t *dirCluster = (FileEntry_t *)getContents(directory);
  if (dirCluster == NULL) {
    printf("Couldn't read the cluster!\n");
    return NULL;
  }
  for (size_t i = 0; true; i++) {
    FileEntry_t entry = dirCluster[i];
    if (lastEntry(&entry)) {
      break;
    }
    if (skippable(&entry)) {
      continue;
    }
    char *filename = getFilename(&entry);
    if (!filename) {
      printf("Couldn't read the filename!\n");
      continue;
    }
    if (strcmp(name, filename) == 0) {
      free(filename);
      return dirCluster + i;
    }
    free(filename);
  }
  return NULL;
}

static void printCurrentDirectory(void) {
  for (size_t i = 0; i <= global_data.historyIndex; i++) {
    FileEntry_t *directoryEntry = getDirectory(i);
    if (directoryEntry == NULL) {
      // root entry
      printf("/");
      continue;
    }
    printFilename(directoryEntry);
    printf("/");
  }
}

static void printIndentation(size_t times) {
  for (int i = 0; i < times; i++) {
    printf("   ");
  }
}

static void showDirectoryContents(FileEntry_t *directory, size_t indent, bool recursive) {
  FileEntry_t *entries = (FileEntry_t *)getContents(directory);
  if (entries == NULL) {
    printf("  Couldn't read entries cluster!\n");
    return;
  }
  for (size_t i = 0; true; i++) {
    FileEntry_t entry = entries[i];
    if (lastEntry(&entry)) {
      break;
    }
    if (skippable(&entry)) {
      continue;
    }
    if (*entry.filename == '.') {
      // don't show . and ..
      continue;
    }
    printIndentation(indent);
    printFullDate(entry.creation_time, entry.creation_date);
    printf("  ");
    if (is_directory(&entry)) {
      printf("<DIRECTORY>");
    } else {
      printf("%u bytes", entry.file_size);
    }
    printf("  ");
    printFilename(&entry);
    printf("\n");
    if (recursive && is_directory(&entry)) {
      FileEntry_t *subEntries = (FileEntry_t *)getContents(&entry);
      if (subEntries != NULL) {
        showDirectoryContents(subEntries, indent + 1, true);
      }
    }
  }
}

static bool skippable(FileEntry_t *entry) {
  if (entry->allocation_status == DELETED) {
    return true;
  }
  if (entry->file_attributes & HIDDEN_FILE) {
    return true;
  }
  if (entry->file_attributes & LONG_FILENAME) {
    return true;
  }
  return false;
}

static bool lastEntry(FileEntry_t *entry) {
  return entry->allocation_status == UNALLOCATED;
}

static void handleCommand(char *command) {
  const char *first = strtok(command, " ");
  char *second = strtok(NULL, " ");
  if (strcmp("rootinfo", first) == 0) {
    BootSector_t *BS = global_data.BS;
    uint32_t entries = countRootEntries();
    double percentage = ((double)entries / BS->max_files_in_root) * 100.00;
    printf("  Max entries in root directory %hu\n", BS->max_files_in_root);
    printf("  Entries in root directory %u\n", entries);
    printf("  Root directory is %.2lf%% full\n", percentage);
    return;
  }
  if (strcmp("spaceinfo", first) == 0) {
    uint8_t *FAT = global_data.FAT;
    BootSector_t *BS = global_data.BS;
    uint32_t bytes = BS->size_of_FAT * BS->bytes_per_sector;
    uint32_t cluster_size = BS->bytes_per_sector * BS->sectors_per_cluster;
    uint32_t FAT_size = BS->size_of_FAT * BS->bytes_per_sector;
    uint32_t FAT_entries = (FAT_size / 3) * 2;
    uint32_t bad_entries = 0;
    uint32_t free_entries = 0;
    uint32_t used_entries = 0;
    uint32_t ending_entries = 0;
    for (int i = 0; i < FAT_entries; i++) {
      uint16_t entry = get_fat_entry(FAT, i);
      bad_entries += bad_entry(entry);
      free_entries += free_entry(entry);
      ending_entries += last_entry(entry);
      used_entries += used_entry(entry);
    }
    printf("  Currently there are\n");
    printf("    %u used entries\n", used_entries);
    printf("    %u free entries\n", free_entries);
    printf("    %u bad entries\n", bad_entries);
    printf("    %u entries ending a cluster chain\n", ending_entries);
    printf("  Each cluster is %hhu sectors (%u bytes) long\n", BS->sectors_per_cluster, cluster_size);
    return;
  }
  if (strcmp("pwd", first) == 0) {
    printf("  Current directory: ");
    printCurrentDirectory();
    printf("\n");
    return;
  }
  if (strcmp("cd", first) == 0) {
    if (second == NULL) {
      printf("  No argument supplied!\n");
      return;
    }
    File_t *handle = goAndFetch(second, false);
    if (handle == ROOT) {
      return;
    }
    if (handle == NULL) {
      printf("  %s doesn't exist.\n", second);
      return;
    }
    FileEntry_t *directory = handle->_entry;
    fileClose(handle);
    if (!is_directory(directory)) {
      printf("  %s is not a directory.\n", second);
      return;
    }
    if (global_data.historyIndex + 1 == MAX_DEPTH) {
      printf("  Max depth reached!\n");
      return;
    }
    global_data.historyIndex++;
    global_data.directoryHistory[global_data.historyIndex] = directory;
    return;
  }
  if (strcmp("dir", first) == 0) {
    showDirectoryContents(getCurrentDir(), 1, false);
    return;
  }
  if (strcmp("cat", first) == 0) {
    if (second == NULL) {
      printf("  No argument supplied!\n");
      return;
    }
    File_t *handle = fileOpen(second);
    if (handle == NULL) {
      printf("  %s not found.\n", second);
      return;
    }
    FileEntry_t *entry = handle->_entry;
    fileClose(handle);
    if (is_directory(entry)) {
      printf("  Cannot read %s because it's a directory.\n", second);
      return;
    }
    uint8_t *contents = getContents(entry);
    printf("%s\n", contents);
    free(contents);
    return;
  }
  if (strcmp("get", first) == 0) {
    if (second == NULL) {
      printf("  No argument supplied!\n");
      return;
    }
    File_t *handle = fileOpen(second);
    if (handle == NULL) {
      printf("  %s not found.\n", second);
      return;
    }
    FileEntry_t *entry = handle->_entry;
    fileClose(handle);
    if (is_directory(entry)) {
      printf("  Cannot read %s because it's a directory.\n", second);
      return;
    }
    uint8_t *contents = getContents(entry);
    char *filename = getFilename(entry);
    FILE *output = fopen(filename, "w");
    if (output == NULL) {
      free(contents);
      free(filename);
      printf("  Couldn't create %s.\n", filename);
      return;
    }
    fwrite(contents, strlen((const char *)contents), 1, output);
    fclose(output);
    printf("  %s successfully copied to disk.\n", filename);
    free(contents);
    free(filename);
    return;
  }
  if (strcmp("fileinfo", first) == 0) {
    if (second == NULL) {
      printf("  No argument supplied!\n");
      return;
    }
    makeHistoryBackup();
    File_t *handle = goAndFetch(second, false);
    if (handle == NULL || handle == ROOT) {
      restoreHistory();
      printf("  %s not found.\n", second);
      return;
    }
    FileEntry_t *entry = handle->_entry;
    fileClose(handle);
    char *name = getFilename(entry);
    printf("  Full name: ");
    printCurrentDirectory();
    printf("%s\n", name);
    free(name);
    restoreHistory();
    printf("  Attributes: ");
    if (entry->file_attributes & FILE_READ_ONLY) {
      printf("READ ONLY");
    }
    if (entry->file_attributes & HIDDEN_FILE) {
      printf(" HIDDEN");
    }
    if (entry->file_attributes & ARCHIVE) {
      printf(" ARCHIVE");
    }
    if (entry->file_attributes & DIRECTORY) {
      printf(" DIRECTORY");
    }
    if (entry->file_attributes & SYSTEM_FILE) {
      printf(" SYSTEM FILE");
    }
    printf("\n");
    if (!is_directory(entry)) {
      printf("  Size: %u\n", entry->file_size);
    }
    printf("  Created: ");
    printFullDate(entry->creation_time, entry->creation_date);
    printf("\n");
    printf("  Last modified: ");
    printFullDate(entry->modified_time, entry->modified_date);
    printf("\n");
    printf("  Last accessed: ");
    printDate(entry->access_date);
    printf("\n");
    printf("  Cluster chain: ");
    uint16_t FAT_entry = entry->first_cluster_address_low;
    uint8_t *FAT = global_data.FAT;
    while (true) {
      printf("%hu", FAT_entry);
      FAT_entry = get_fat_entry(FAT, FAT_entry);
      if (last_entry(FAT_entry) || bad_entry(FAT_entry)) {
        break;
      }
      printf(", ");
    }
    printf("\n");
    uint32_t clusters = countFATentries(entry);
    printf("  Clusters: %u\n", clusters);
    return;
  }
  if (strcmp("zip", first) == 0) {
    char *third = strtok(NULL, " ");
    char *fourth = strtok(NULL, " ");
    if (!second || !third || !fourth) {
      printf("  Not enough arguments supplied!\n");
      return;
    }
    File_t *handle1 = fileOpen(second);
    File_t *handle2 = fileOpen(third);
    if (!handle1 || !handle2) {
      printf("  Couldn't find files\n");
      return;
    }
    FileEntry_t *entry1 = handle1->_entry;
    FileEntry_t *entry2 = handle2->_entry;
    fileClose(handle1);
    fileClose(handle2);
    if (is_directory(entry1) || is_directory(entry2)) {
      printf("  Cannot zip a directory.\n");
      return;
    }
    FILE *output = fopen(fourth, "w");
    if (output == NULL) {
      printf("  Couldn't open %s", fourth);
      return;
    }
    char *content1 = (char *)getContents(entry1);
    if (content1 == NULL) {
      printf("  Couldn't read file contents!\n");
      return;
    }
    char *content2 = (char *)getContents(entry2);
    if (content2 == NULL) {
      free(content1);
      printf("  Couldn't read file contents!\n");
      return;
    }
    char *token1 = strsep(&content1, "\n");
    char *token2 = strsep(&content2, "\n");
    while (token1 || token2) {
      if (token1) {
        fwrite(token1, strlen(token1), 1, output);
        fwrite("\n", 1, 1, output);
      }
      if (token2) {
        fwrite(token2, strlen(token2), 1, output);
        fwrite("\n", 1, 1, output);
      }
      token1 = strsep(&content1, "\n");
      token2 = strsep(&content2, "\n");
    }
    fclose(output);
    free(content1);
    free(content2);
    printf("  Successfully zipped files.\n");
    return;
  }
  if (strcmp(first, "tree") == 0) {
    showDirectoryContents(NULL, 1, true);
    return;
  }
  if (strcmp(first, "help") == 0) {
    printf("  Available commands:\n");
    printf("    exit - terminates the program\n");
    printf("    dir - list current directory's files and folders\n");
    printf("    cd <directory> - enter directory\n");
    printf("    pwd - print working directory\n");
    printf("    cat <filename> - print file's contents\n");
    printf("    get <filename> - copy file's contents to local folder\n");
    printf("    zip <filename1> <filename2> <output_name> save files' contents to output\n");
    printf("    rootinfo - print information about the root directory\n");
    printf("    spaceinfo - print information about the disk image\n");
    printf("    fileinfo <filename> - print information about the file\n");
    printf("    tree - show contents of the whole image\n");
    return;
  }
  printf("  Unknown command '%s', type help for a list of available commands\n", first);
}