#ifndef __FAT_
#define __FAT_

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

// file attributes
#define FILE_READ_ONLY 0x01
#define HIDDEN_FILE 0x02
#define SYSTEM_FILE 0x04
#define VOLUME_LABEL 0x08
#define LONG_FILENAME 0x0f
#define DIRECTORY 0x10
#define ARCHIVE 0x20

// allocation statuses
#define UNALLOCATED 0x00
#define DELETED 0xe5

// time and date bitmasks
#define TIME_HOURS 0xf800
#define TIME_MINUTES 0x07e0
#define TIME_SECONDS 0x001f
#define DATE_YEAR 0xfe00
#define DATE_MONTH 0x01e0
#define DATE_DAY 0x001f
#define get_hours(time) (((time) & TIME_HOURS) >> 11)
#define get_minutes(time) (((time) & TIME_MINUTES) >> 5)
#define get_seconds(time) (((time) & TIME_SECONDS) * 2)
#define get_year(date) (1980 + (((date) & DATE_YEAR) >> 9))
#define get_month(date) (((date) & DATE_MONTH) >> 5)
#define get_day(date) ((date) & DATE_DAY)

#define last_entry(entry) ((entry) >= 0xff8)
#define bad_entry(entry) ((entry) == 0xff7)
#define free_entry(entry) ((entry) == 0x000)
#define used_entry(entry) ((entry) >= 0x002 && (entry) <= 0xfef)
#define reserved_entry(entry) ((entry) >= 0xff0 && (entry) <= 0xff6)

#define is_directory(fileEntry) (!!((fileEntry)->file_attributes & DIRECTORY))

#define BUFFER_SIZE 1024
#define MAX_DEPTH 100

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define FILE_ERROR (-2)
#define FILE_END (-3)
#define ROOT ((void *)-1)

enum file_type {directory, file};

struct __attribute__((packed)) _BootSector {
  uint8_t intructions[3];
  uint8_t OEM[8];
  uint16_t bytes_per_sector;
  uint8_t sectors_per_cluster;
  uint16_t reserved_area; // in sectors
  uint8_t FATs;
  uint16_t max_files_in_root;
  uint16_t number_of_sectors_2b; // if 2 bytes are not enough, set to 0 and use 4b
  uint8_t media_type;
  uint16_t size_of_FAT; // in sectors
  uint16_t sectors_per_track;
  uint16_t number_of_heads;
  uint32_t number_of_sectors_before_start_pos; // before the start position
  uint32_t number_of_sectors_4b; // used if 2b is set to 0
  uint8_t drive_number;
  uint8_t __reserved[1];
  uint8_t ex_boot_signature; // used to validate next three fields
  uint32_t serial_number;
  uint8_t volume_label[11];
  uint8_t system_type_level[8];
  uint8_t pad2[448];
  uint16_t signature_value;
};

struct __attribute__((packed)) _FileEntry {
  union {
    uint8_t allocation_status;
    struct {
      uint8_t filename[8];
      uint8_t extension[3];
    };
  };
  uint8_t file_attributes;
  uint8_t reserved;
  uint8_t creation_time_ms; // in tenths of seconds
  uint16_t creation_time; // h(15-11), m(10-5), s(4-0)
  uint16_t creation_date; // y(15-9), m(8-5), d(4-0)
  uint16_t access_date;
  uint16_t first_cluster_address_high; // invalid in FAT12, valid in FAT16
  uint16_t modified_time;
  uint16_t modified_date;
  uint16_t first_cluster_address_low; // valid in FAT12
  uint32_t file_size; // 0 if directory
};

struct _File_t {
  struct _FileEntry *_entry;
  int _position;
  enum file_type _type;
  size_t _size;
  bool _opened;
};

struct global_data_t {
  struct _BootSector *BS;
  uint8_t *FAT; // main FAT
  struct _FileEntry *dataSection;
  struct _FileEntry *rootEntries;
  struct _FileEntry *directoryHistory[MAX_DEPTH];
  struct _FileEntry *historyBackup[MAX_DEPTH];
  uint32_t historyIndex;
  uint32_t historyIndexBackup;
};

typedef struct _FileEntry FileEntry_t;
typedef struct _BootSector BootSector_t;
typedef struct _File_t File_t;

// internal functions

static FileEntry_t *findEntry(const char *name);
static uint8_t *getContents(FileEntry_t *entry);
static void printFilename(FileEntry_t *entry);
static char *getFilename(FileEntry_t *entry);
static void printDate(uint16_t date);
static void printTime(uint16_t time);
static void printFullDate(uint16_t time, uint16_t date);
static uint16_t get_fat_entry(uint8_t *FAT, uint16_t index);
static void dump(void *data, uint32_t size);
static void dumpBSInfo(BootSector_t *BS);
static void handleCommand(char *command);
static uint32_t countRootEntries(void);
static uint32_t countFATentries(FileEntry_t *entry);
static FileEntry_t *getCurrentDir(void);
static FileEntry_t *getDirectory(uint32_t index);
static void printCurrentDirectory(void);
static void showDirectoryContents(FileEntry_t *directory, size_t indent, bool recursive);
static void printIndentation(size_t times);
static File_t *goAndFetch(char *filename, bool reset_depth);
static void makeHistoryBackup(void);
static void restoreHistory(void);
static bool lastEntry(FileEntry_t *entry);
static bool skippable(FileEntry_t *entry);

// API

int loadDiskImage(const char *name);
void initGUI(void);
void freeResources(void);
File_t *fileOpen(char *filename);
File_t *directoryOpen(char *directoryname);
void fileClose(File_t *handle);
int32_t fileRead(char *buffer, size_t size, size_t items, File_t *handle);
int32_t fileReadDirectory(char *buffer, File_t *handle);
int32_t fileReadChar(File_t *handle);
void fileSeek(File_t *handle, size_t position);
void fileSeekCurrent(File_t *handle, int32_t offset);
void fileSeekBeginning(File_t *handle);
void fileSeekEnd(File_t *handle);
bool skippable(FileEntry_t *entry);
bool lastEntry(FileEntry_t *entry);

#endif // __FAT_