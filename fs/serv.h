#include <fs.h>
#include <lib.h>
#include <mmu.h>

#define PTE_DIRTY 0x0004 // file system block cache is dirty

#define SECT_SIZE 512			  /* Bytes per disk sector */
#define SECT2BLK (BLOCK_SIZE / SECT_SIZE) /* sectors to a block */

/* Disk block n, when in memory, is mapped into dynamically allocated pages
 * in [CACHE_BASE, CACHE_LIMIT). */
#define CACHE_BASE 0x10000000
#define CACHE_LIMIT 0x50000000

/* Maximum disk size we can handle (1GB) */
#define DISKMAX 0x40000000

/* ide.c */
void ide_read(u_int diskno, u_int secno, void *dst, u_int nsecs);
void ide_write(u_int diskno, u_int secno, void *src, u_int nsecs);

/* fs.c */
int file_open(char *path, struct File **pfile);
int file_create(char *path, struct File **file);
int file_get_block(struct File *f, u_int blockno, void **pblk);
int file_set_size(struct File *f, u_int newsize);
void file_close(struct File *f);
int file_remove(char *path);
int file_dirty(struct File *f, u_int offset);
void file_flush(struct File *);
int file_fcb_location(struct File *f, u_int *block, u_int *offset);
int file_get_by_loc(u_int block, u_int offset, struct File **pf);
int file_has_parent(struct File *f);

void fs_init(void);
void fs_sync(void);
extern uint32_t *bitmap;
int map_block(u_int);
int alloc_block(void);
