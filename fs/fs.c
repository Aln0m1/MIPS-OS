#include "serv.h"
#include <mmu.h>

struct Super *super;

uint32_t *bitmap;

// Task 4: Dynamic cache address allocation
#define CACHE_START 0x10000000
#define CACHE_END 0x50000000
#define CACHE_SIZE (CACHE_END - CACHE_START)
#define PAGES_IN_CACHE (CACHE_SIZE / PAGE_SIZE)

// Bitmap for free cache pages (1 = free, 0 = used)
static uint8_t cache_page_bitmap[PAGES_IN_CACHE / 8 + 1];

// Mapping from disk block number to cache virtual address
#define MAX_DISK_BLOCKS 1024
static void *block_to_cache[MAX_DISK_BLOCKS];

// Inverse mapping from cache virtual address to disk block number
static uint32_t cache_to_block[PAGES_IN_CACHE];

// Helper function: Allocate a cache page, returns virtual address or NULL
static void *alloc_cache_page(void) {
	// Find first free page (lowest address first)
	for (int i = 0; i < PAGES_IN_CACHE; i++) {
		int byte_idx = i / 8;
		int bit_idx = i % 8;
		if (cache_page_bitmap[byte_idx] & (1 << bit_idx)) {
			// Mark as used
			cache_page_bitmap[byte_idx] &= ~(1 << bit_idx);
			void *va = (void *)(CACHE_START + i * PAGE_SIZE);
			return va;
		}
	}
	return NULL; // No free pages
}

// Helper function: Free a cache page
static void free_cache_page(void *va) {
	if (va < (void *)CACHE_START || va >= (void *)CACHE_END) {
		return;
	}
	uint32_t idx = ((uintptr_t)va - CACHE_START) / PAGE_SIZE;
	int byte_idx = idx / 8;
	int bit_idx = idx % 8;
	// Mark as free
	cache_page_bitmap[byte_idx] |= (1 << bit_idx);
}

// Helper function: Initialize cache bitmap
static void init_cache_bitmap(void) {
	// Mark all pages as free initially
	for (int i = 0; i < sizeof(cache_page_bitmap); i++) {
		cache_page_bitmap[i] = 0xFF;
	}
	// Initialize mappings to NULL
	for (int i = 0; i < MAX_DISK_BLOCKS; i++) {
		block_to_cache[i] = NULL;
	}
	for (int i = 0; i < PAGES_IN_CACHE; i++) {
		cache_to_block[i] = -1; // Invalid block number
	}
}

// Helper function: Get directory FCB pointer from block and offset
struct File *get_dir_fcb(uint32_t block, uint32_t offset) {
    if (block == 0) {
        return NULL;
    }
    void *va = disk_addr(block);
    if (!va_is_mapped(va)) {
        if (read_block(block, NULL, NULL) < 0) {
            return NULL;
        }
        va = disk_addr(block);
    }
    return (struct File *)((char *)va + offset);
}

// Helper function: Set dir block and offset for a file FCB
void set_dir_info(struct File *f, struct File *dir) {
    if (dir == &super->s_root) {
        f->f_dir_block = 1; // super block contains root dir
        f->f_dir_offset = offsetof(struct Super, s_root);
        return;
    }
    // Find dir's position in its parent directory
    if (dir->f_dir_block == 0) {
        f->f_dir_block = 0;
        f->f_dir_offset = 0;
        return;
    }
    uint32_t nblock = dir->f_size / BLOCK_SIZE;
    for (int i = 0; i < nblock; i++) {
        uint32_t diskbno;
        if (file_map_block(dir, i, &diskbno, 0) < 0) {
            continue;
        }
        void *va = disk_addr(diskbno);
        struct File *files = (struct File *)va;
        for (int j = 0; j < FILE2BLK; j++) {
            if (&files[j] == dir) {
                f->f_dir_block = diskbno;
                f->f_dir_offset = j * sizeof(struct File);
                return;
            }
        }
    }
    f->f_dir_block = 0;
    f->f_dir_offset = 0;
}

// Helper function: Find a file's position in a directory block, return its block and offset
void find_file_pos(struct File *dir, struct File *f, uint32_t *diskbno_out, uint32_t *offset_out) {
    if (dir == &super->s_root) {
        *diskbno_out = 1;
        *offset_out = offsetof(struct Super, s_root);
        return;
    }
    uint32_t nblock = dir->f_size / BLOCK_SIZE;
    for (int i = 0; i < nblock; i++) {
        uint32_t diskbno;
        if (file_map_block(dir, i, &diskbno, 0) < 0) {
            continue;
        }
        void *va = disk_addr(diskbno);
        struct File *files = (struct File *)va;
        for (int j = 0; j < FILE2BLK; j++) {
            if (&files[j] == f) {
                *diskbno_out = diskbno;
                *offset_out = j * sizeof(struct File);
                return;
            }
        }
    }
    *diskbno_out = 0;
    *offset_out = 0;
}

void file_flush(struct File *);
int block_is_free(u_int);
int file_map_block(struct File *f, u_int filebno, u_int *diskbno, u_int alloc);

// Overview:
// Return the virtual address of this disk block in cache.
void *disk_addr(u_int blockno) {
	if (blockno >= MAX_DISK_BLOCKS) {
		return NULL;
	}
	return block_to_cache[blockno];
}

// Overview:
//  Check if this virtual address is mapped to a block. (check PTE_V bit)
int va_is_mapped(void *va) {
	return (vpd[PDX(va)] & PTE_V) && (vpt[VPN(va)] & PTE_V);
}

// Overview:
//  Check if this disk block is mapped in cache.
//  Returns the virtual address of the cache page if mapped, 0 otherwise.
void *block_is_mapped(u_int blockno) {
	if (blockno >= MAX_DISK_BLOCKS) {
		return NULL;
	}
	return block_to_cache[blockno]; // NULL means not mapped
}

// Overview:
//  Check if this virtual address is dirty. (check PTE_DIRTY bit)
int va_is_dirty(void *va) {
	return va_is_mapped(va) && (vpt[VPN(va)] & PTE_DIRTY);
}

// Overview:
//  Check if this block is dirty. (check corresponding `va`)
int block_is_dirty(u_int blockno) {
	void *va = disk_addr(blockno);
	return va_is_dirty(va);
}

// Overview:
//  Mark this block as dirty (cache page has changed and needs to be written back to disk).
int dirty_block(u_int blockno) {
	void *va = disk_addr(blockno);

	if (!va_is_mapped(va)) {
		return -E_NOT_FOUND;
	}

	if (va_is_dirty(va)) {
		return 0;
	}

	return syscall_mem_map(0, va, 0, va, PTE_D | PTE_DIRTY);
}

/* Lab 5 Key Code "write-block" */
// Overview:
//  Write the current contents of the block out to disk.
void write_block(u_int blockno) {
	// Step 1: detect is this block is mapped, if not, can't write it's data to disk.
	if (!block_is_mapped(blockno)) {
		user_panic("write unmapped block %08x", blockno);
	}

	// Step2: write data to IDE disk. (using ide_write, and the diskno is 0)
	void *va = disk_addr(blockno);
	ide_write(0, blockno * SECT2BLK, va, SECT2BLK);
	syscall_mem_map(0, va, 0, va, PTE_D);
}
/* End of Key Code "write-block" */

/* Lab 5 Key Code "read-block" */
// Overview:
//  Make sure a particular disk block is loaded into memory.
//
// Post-Condition:
//  Return 0 on success, or a negative error code on error.
//
//  If blk!=0, set *blk to the address of the block in memory.
//
//  If isnew!=0, set *isnew to 0 if the block was already in memory, or
//  to 1 if the block was loaded off disk to satisfy this request. (Isnew
//  lets callers like file_get_block clear any memory-only fields
//  from the disk blocks when they come in off disk.)
int read_block(u_int blockno, void **blk, u_int *isnew) {
	// Step 1: validate blockno. Make file the block to read is within the disk.
	if (super && blockno >= super->s_nblocks) {
		user_panic("reading non-existent block %08x\n", blockno);
	}
	if (blockno >= MAX_DISK_BLOCKS) {
		user_panic("blockno %08x exceeds MAX_DISK_BLOCKS\n", blockno);
	}

	// Step 2: validate this block is used, not free.
	if (bitmap && block_is_free(blockno)) {
		user_panic("reading free block %08x\n", blockno);
	}

	// Step 3: check if already mapped
	if (block_is_mapped(blockno)) { // the block is in memory
		if (isnew) {
			*isnew = 0;
		}
		if (blk) {
			*blk = block_to_cache[blockno];
		}
		return 0;
	}

	// Step 4: allocate a new cache page
	void *va = alloc_cache_page();
	if (va == NULL) {
		user_panic("out of cache pages\n");
		return -E_NO_MEM;
	}

	// Step 5: map the page and read from disk
	if (isnew) {
		*isnew = 1;
	}
	try(syscall_mem_alloc(0, va, PTE_D));
	ide_read(0, blockno * SECT2BLK, va, SECT2BLK);

	// Step 6: update mappings
	block_to_cache[blockno] = va;
	uint32_t cache_idx = ((uintptr_t)va - CACHE_START) / PAGE_SIZE;
	cache_to_block[cache_idx] = blockno;

	// Step 7: if blk != NULL, assign 'va' to '*blk'.
	if (blk) {
		*blk = va;
	}
	return 0;
}
/* End of Key Code "read-block" */

// Overview:
//  Allocate a page to cache the disk block.
int map_block(u_int blockno) {
	// Step 1: If the block is already mapped in cache, return 0.
	if (block_is_mapped(blockno)) {
		return 0;
	}
	if (blockno >= MAX_DISK_BLOCKS) {
		return -E_INVAL;
	}

	// Step 2: Allocate a cache page
	void *va = alloc_cache_page();
	if (va == NULL) {
		return -E_NO_MEM;
	}

	// Step 3: Map the page
	int r = syscall_mem_alloc(0, va, PTE_D);
	if (r < 0) {
		free_cache_page(va);
		return r;
	}

	// Step 4: Update mappings
	block_to_cache[blockno] = va;
	uint32_t cache_idx = ((uintptr_t)va - CACHE_START) / PAGE_SIZE;
	cache_to_block[cache_idx] = blockno;

	return 0;
}

// Overview:
//  Unmap a disk block in cache.
void unmap_block(u_int blockno) {
	// Step 1: Get the mapped address of the cache page of this block
	void *va = block_is_mapped(blockno);
	if (va == NULL) {
		return; // Already not mapped
	}
	if (blockno >= MAX_DISK_BLOCKS) {
		return;
	}

	// Step 2: If this block is used (not free) and dirty in cache, write it back first
	if (!block_is_free(blockno) && block_is_dirty(blockno)) {
		write_block(blockno);
	}

	// Step 3: Update mappings
	uint32_t cache_idx = ((uintptr_t)va - CACHE_START) / PAGE_SIZE;
	block_to_cache[blockno] = NULL;
	cache_to_block[cache_idx] = -1;

	// Step 4: Unmap the virtual address and free cache page
	panic_on(syscall_mem_unmap(0, va));
	free_cache_page(va);

	user_assert(!block_is_mapped(blockno));
}

// Helper function: Get pointer to bitmap entry, ensuring the bitmap block is cached
static uint32_t *get_bitmap_entry(u_int blockno) {
	u_int bitmap_block = (blockno / 32) / (BLOCK_SIZE / 4) + 2;
	// Make sure the bitmap block is in cache
	if (!block_is_mapped(bitmap_block)) {
		void *blk;
		read_block(bitmap_block, &blk, 0);
	}
	// Calculate offset within the bitmap block
	u_int entry_within_block = (blockno / 32) % (BLOCK_SIZE / 4);
	return (uint32_t *)((char *)disk_addr(bitmap_block) + entry_within_block * 4);
}

// Overview:
//  Check if the block 'blockno' is free via bitmap.
//
// Post-Condition:
//  Return 1 if the block is free, else 0.
int block_is_free(u_int blockno) {
	if (super == 0 || blockno >= super->s_nblocks) {
		return 0;
	}

	uint32_t *entry = get_bitmap_entry(blockno);
	if (*entry & (1 << (blockno % 32))) {
		return 1;
	}

	return 0;
}

// Overview:
//  Mark a block as free in the bitmap.
void free_block(u_int blockno) {
	if (blockno == 0 || blockno >= super->s_nblocks) {
		return;
	}

	uint32_t *entry = get_bitmap_entry(blockno);
	*entry |= 1 << (blockno & 0x1f);

	u_int bitmap_block = (blockno / 32) / (BLOCK_SIZE / 4) + 2;
	write_block(bitmap_block);

	if (block_is_mapped(blockno)) {
		unmap_block(blockno);
	}
}

// Overview:
//  Search in the bitmap for a free block and allocate it.
//
// Post-Condition:
//  Return block number allocated on success,
//  Return -E_NO_DISK if we are out of blocks.
int alloc_block_num(void) {
	int blockno;
	u_int nbitmap = (super->s_nblocks + BLOCK_SIZE_BIT - 1) / BLOCK_SIZE_BIT;
	// walk through this bitmap, find a free one and mark it as used, then sync
	// this block to IDE disk (using `write_block`) from memory.
	for (blockno = nbitmap + 2; blockno < super->s_nblocks; blockno++) {
		uint32_t *entry = get_bitmap_entry(blockno);
		if (*entry & (1 << (blockno % 32))) { // the block is free
			*entry &= ~(1 << (blockno % 32));
			u_int bitmap_block = (blockno / 32) / (BLOCK_SIZE / 4) + 2;
			write_block(bitmap_block); // write to disk.
			return blockno;
		}
	}
	// no free blocks.
	return -E_NO_DISK;
}

// Overview:
//  Allocate a block -- first find a free block in the bitmap, then map it into memory.
int alloc_block(void) {
	int r, bno;
	// Step 1: find a free block.
	if ((r = alloc_block_num()) < 0) { // failed.
		return r;
	}
	bno = r;

	// Step 2: map this block into memory.
	if ((r = map_block(bno)) < 0) {
		free_block(bno);
		return r;
	}

	// Step 3: return block number.
	return bno;
}

// Overview:
//  Read and validate the file system super-block.
//
// Post-condition:
//  If error occurred during read super block or validate failed, panic.
void read_super(void) {
	int r;
	void *blk;

	// Step 1: read super block.
	if ((r = read_block(1, &blk, 0)) < 0) {
		user_panic("cannot read superblock: %d", r);
	}

	super = blk;

	// Step 2: Check fs magic nunber.
	if (super->s_magic != FS_MAGIC) {
		user_panic("bad file system magic number %x %x", super->s_magic, FS_MAGIC);
	}

	// Step 3: validate disk size.
	if (super->s_nblocks > DISKMAX / BLOCK_SIZE) {
		user_panic("file system is too large");
	}

	debugf("superblock is good\n");
}

// Overview:
//  Read and validate the file system bitmap.
//
// Hint:
//  Read all the bitmap blocks into memory.
//  Set the 'bitmap' to point to the first bitmap block.
//  For each block i, user_assert(!block_is_free(i))) to check that they're all marked as in use.
void read_bitmap(void) {
	u_int i;
	void *blk;

	// Step 1: Calculate the number of the bitmap blocks, and read them into memory.
	u_int nbitmap = (super->s_nblocks + BLOCK_SIZE_BIT - 1) / BLOCK_SIZE_BIT;
	for (i = 0; i < nbitmap; i++) {
		read_block(i + 2, &blk, 0);
	}

	// Set bitmap to the first bitmap block's cache address
	bitmap = (uint32_t *)disk_addr(2);

	// Step 2: Make sure the reserved and root blocks are marked in-use.
	user_assert(!block_is_free(0));
	user_assert(!block_is_free(1));

	// Step 3: Make sure all bitmap blocks are marked in-use.
	for (i = 0; i < nbitmap; i++) {
		user_assert(!block_is_free(i + 2));
	}

	debugf("read_bitmap is good\n");
}

// Overview:
//  Test that write_block works, by smashing the superblock and reading it back.
void check_write_block(void) {
	super = 0;
	void *blk0, *blk1;

	// backup the super block.
	panic_on(read_block(0, &blk0, 0));
	panic_on(read_block(1, &blk1, 0));
	memcpy((char *)blk0, (char *)blk1, BLOCK_SIZE);

	// smash it
	strcpy((char *)blk1, "OOPS!\n");
	write_block(1);
	user_assert(block_is_mapped(1));

	// clear it out - use unmap_block instead of direct syscall
	unmap_block(1);
	user_assert(!block_is_mapped(1));

	// validate the data read from the disk.
	panic_on(read_block(1, &blk1, 0));
	user_assert(strcmp((char *)blk1, "OOPS!\n") == 0);

	// restore the super block.
	memcpy((char *)blk1, (char *)blk0, BLOCK_SIZE);
	write_block(1);
	super = (struct Super *)blk1;
}

// Overview:
//  Initialize the file system.
// Hint:
//  1. read super block.
//  2. check if the disk can work.
//  3. read bitmap blocks from disk to memory.
void fs_init(void) {
	init_cache_bitmap();
	read_super();
	check_write_block();
	read_bitmap();
}

// Overview:
//  Find the disk block where 'f' is in. Then mark this block as dirty.
void dirty_fcb(struct File *f) {
	if (f->f_dir_block != 0) {
		// Get dir FCB first
		struct File *dir = get_dir_fcb(f->f_dir_block, f->f_dir_offset);
		if (dir) {
			u_int nblock = dir->f_size / BLOCK_SIZE;
			for (int i = 0; i < nblock; i++) {
				u_int diskbno;
				struct File *files;
				if (file_map_block(dir, i, &diskbno, 0) < 0) {
					debugf("dirty_fcb: file_map_block failed\n");
					break;
				}
				files = disk_addr(diskbno);
				if (files <= f && f < files + FILE2BLK) {
					dirty_block(diskbno);
					break;
				}
			}
		}
	} else if (f == &super->s_root) {
		// Root dir is in super block (block 1)
		dirty_block(1);
	}
}

// Overview:
//  Like pgdir_walk but for files.
//  Find the disk block number slot for the 'filebno'th block in file 'f'. Then, set
//  '*ppdiskbno' to point to that slot. The slot will be one of the f->f_direct[] entries,
//  or an entry in the indirect block.
//  When 'alloc' is set, this function will allocate an indirect block if necessary.
//
// Post-Condition:
//  Return 0 on success, and set *ppdiskbno to the pointer to the target block.
//  Return -E_NOT_FOUND if the function needed to allocate an indirect block, but alloc was 0.
//  Return -E_NO_DISK if there's no space on the disk for an indirect block.
//  Return -E_NO_MEM if there's not enough memory for an indirect block.
//  Return -E_INVAL if filebno is out of range (>= NINDIRECT).
int file_block_walk(struct File *f, u_int filebno, uint32_t **ppdiskbno, u_int alloc) {
	int r;
	uint32_t *ptr;
	uint32_t *blk;

	if (filebno < NDIRECT) {
		// Step 1: if the target block is corresponded to a direct pointer, just return the
		// disk block number.
		ptr = &f->f_direct[filebno];
	} else if (filebno < NINDIRECT) {
		// Step 2: if the target block is corresponded to the indirect block, but there's no
		//  indirect block and `alloc` is set, create the indirect block.
		if (f->f_indirect == 0) {
			if (alloc == 0) {
				return -E_NOT_FOUND;
			}

			if ((r = alloc_block()) < 0) {
				return r;
			}
			f->f_indirect = r;
			dirty_fcb(f);
		}

		// Step 3: read the new indirect block to memory.
		if ((r = read_block(f->f_indirect, (void **)&blk, 0)) < 0) {
			return r;
		}
		ptr = blk + filebno;
	} else {
		return -E_INVAL;
	}

	// Step 4: store the result into *ppdiskbno, and return 0.
	*ppdiskbno = ptr;
	return 0;
}

// OVerview:
//  Set *diskbno to the disk block number for the filebno'th block in file f.
//  If alloc is set and the block does not exist, allocate it.
//
// Post-Condition:
//  Returns 0: success, < 0 on error.
//  Errors are:
//   -E_NOT_FOUND: alloc was 0 but the block did not exist.
//   -E_NO_DISK: if a block needed to be allocated but the disk is full.
//   -E_NO_MEM: if we're out of memory.
//   -E_INVAL: if filebno is out of range.
int file_map_block(struct File *f, u_int filebno, u_int *diskbno, u_int alloc) {
	int r;
	uint32_t *ptr;

	// Step 1: find the pointer for the target block.
	if ((r = file_block_walk(f, filebno, &ptr, alloc)) < 0) {
		return r;
	}

	// Step 2: if the block not exists, and create is set, alloc one.
	if (*ptr == 0) {
		if (alloc == 0) {
			return -E_NOT_FOUND;
		}

		if ((r = alloc_block()) < 0) {
			return r;
		}
		*ptr = r;
	}

	// Step 3: set the pointer to the block in *diskbno and return 0.
	*diskbno = *ptr;
	return 0;
}

// Overview:
//  Remove a block from file f. If it's not there, just silently succeed.
int file_clear_block(struct File *f, u_int filebno) {
	int r;
	uint32_t *ptr;

	if ((r = file_block_walk(f, filebno, &ptr, 0)) < 0) {
		return r;
	}

	if (*ptr) {
		free_block(*ptr);
		*ptr = 0;
	}

	return 0;
}

/* Lab 5 Key Code "file-get-block" */
// Overview:
//  Set *blk to point at the filebno'th block in file f.
//
// Hint: use file_map_block and read_block.
//
// Post-Condition:
//  return 0 on success, and read the data to `blk`, return <0 on error.
int file_get_block(struct File *f, u_int filebno, void **blk) {
	int r;
	u_int diskbno;
	u_int isnew;

	// Step 1: find the disk block number is `f` using `file_map_block`.
	if ((r = file_map_block(f, filebno, &diskbno, 1)) < 0) {
		return r;
	}

	// Step 2: read the data in this disk to blk.
	if ((r = read_block(diskbno, blk, &isnew)) < 0) {
		return r;
	}
	return 0;
}
/* End of Key Code "file-get-block" */

// Overview:
//  Mark the offset/BLOCK_SIZE'th block dirty in file f.
int file_dirty(struct File *f, u_int offset) {
	int r;
	u_int diskbno;

	if ((r = file_map_block(f, offset / BLOCK_SIZE, &diskbno, 0)) < 0) {
		return r;
	}

	return dirty_block(diskbno);
}

// Overview:
//  Find a file named 'name' in the directory 'dir'. If found, set *file to it.
//
// Post-Condition:
//  Return 0 on success, and set the pointer to the target file in `*file`.
//  Return the underlying error if an error occurs.
int dir_lookup(struct File *dir, char *name, struct File **file) {
	// Step 1: Calculate the number of blocks in 'dir' via its size.
	u_int nblock;

	nblock = dir->f_size / BLOCK_SIZE;

	// Step 2: Iterate through all blocks in the directory.
	for (int i = 0; i < nblock; i++) {
		// Read the i'th block of 'dir' and get its address in 'blk' using 'file_get_block'.
		void *blk;

		try(file_get_block(dir, i, &blk));
		struct File *files = (struct File *)blk;

		// Find the target among all 'File's in this block.
		for (struct File *f = files; f < files + FILE2BLK; ++f) {
			// Compare the file name against 'name' using 'strcmp'.
			if (strcmp(name, f->f_name) == 0) {
				*file = f;
				// Don't modify file's dir info here - it should already be set during creation
				return 0;
			}
		}
	}

	return -E_NOT_FOUND;
}

// Overview:
//  Alloc a new File structure under specified directory. Set *file
//  to point at a free File structure in dir.
int dir_alloc_file(struct File *dir, struct File **file) {
	int r;
	u_int nblock, i, j;
	void *blk;
	struct File *f;

	nblock = dir->f_size / BLOCK_SIZE;

	for (i = 0; i < nblock; i++) {
		// read the block.
		if ((r = file_get_block(dir, i, &blk)) < 0) {
			return r;
		}

		f = blk;

		for (j = 0; j < FILE2BLK; j++) {
			if (f[j].f_name[0] == '\0') { // found free File structure.
				*file = &f[j];
				return 0;
			}
		}
	}

	// no free File structure in exists data block.
	// new data block need to be created.
	dir->f_size += BLOCK_SIZE;
	dirty_fcb(dir);
	if ((r = file_get_block(dir, i, &blk)) < 0) {
		return r;
	}
	f = blk;
	*file = &f[0];

	return 0;
}

// Overview:
//  Skip over slashes.
char *skip_slash(char *p) {
	while (*p == '/') {
		p++;
	}
	return p;
}

// Overview:
//  Evaluate a path name, starting at the root.
//
// Post-Condition:
//  On success, set *pfile to the file we found and set *pdir to the directory
//  the file is in.
//  If we cannot find the file but find the directory it should be in, set
//  *pdir and copy the final path element into lastelem.
int walk_path(char *path, struct File **pdir, struct File **pfile, char *lastelem) {
	char *p;
	char name[MAXNAMELEN];
	struct File *dir, *file;
	int r;

	// start at the root.
	path = skip_slash(path);
	file = &super->s_root;
	dir = 0;
	name[0] = 0;

	if (pdir) {
		*pdir = 0;
	}

	*pfile = 0;

	// find the target file by name recursively.
	while (*path != '\0') {
		dir = file;
		p = path;

		while (*path != '/' && *path != '\0') {
			path++;
		}

		if (path - p >= MAXNAMELEN) {
			return -E_BAD_PATH;
		}

		memcpy(name, p, path - p);
		name[path - p] = '\0';
		path = skip_slash(path);
		if (dir->f_type != FTYPE_DIR) {
			return -E_NOT_FOUND;
		}

		if ((r = dir_lookup(dir, name, &file)) < 0) {
			if (r == -E_NOT_FOUND && *path == '\0') {
				if (pdir) {
					*pdir = dir;
				}

				if (lastelem) {
					strcpy(lastelem, name);
				}

				*pfile = 0;
			}

			return r;
		}
	}

	if (pdir) {
		*pdir = dir;
	}

	*pfile = file;
	return 0;
}

// Overview:
//  Open "path".
//
// Post-Condition:
//  On success set *pfile to point at the file and return 0.
//  On error return < 0.
int file_open(char *path, struct File **file) {
	return walk_path(path, 0, file, 0);
}

// Overview:
//  Create "path".
//
// Post-Condition:
//  On success set *file to point at the file and return 0.
//  On error return < 0.
int file_create(char *path, struct File **file) {
	char name[MAXNAMELEN];
	int r;
	struct File *dir, *f;

	if ((r = walk_path(path, &dir, &f, name)) == 0) {
		return -E_FILE_EXISTS;
	}

	if (r != -E_NOT_FOUND || dir == 0) {
		return r;
	}

	if (dir_alloc_file(dir, &f) < 0) {
		return r;
	}

	strcpy(f->f_name, name);
	f->f_size = 0;
	f->f_type = FTYPE_REG;
	for (int i = 0; i < NDIRECT; i++) {
		f->f_direct[i] = 0;
	}
	f->f_indirect = 0;
	// Set dir info
	uint32_t dir_block, dir_offset;
	find_file_pos(dir, dir, &dir_block, &dir_offset);
	f->f_dir_block = dir_block;
	f->f_dir_offset = dir_offset;

	dirty_fcb(f);
	struct File *dir_fcb = get_dir_fcb(f->f_dir_block, f->f_dir_offset);
	if (dir_fcb) {
		file_flush(dir_fcb);
	}
	*file = f;
	return 0;
}

// Overview:
//  Truncate file down to newsize bytes.
//
//  Since the file is shorter, we can free the blocks that were used by the old
//  bigger version but not by our new smaller self. For both the old and new sizes,
//  figure out the number of blocks required, and then clear the blocks from
//  new_nblocks to old_nblocks.
//
//  If the new_nblocks is no more than NDIRECT, free the indirect block too.
//  (Remember to clear the f->f_indirect pointer so you'll know whether it's valid!)
//
// Hint: use file_clear_block.
void file_truncate(struct File *f, u_int newsize) {
	u_int bno, old_nblocks, new_nblocks;

	old_nblocks = ROUND(f->f_size, BLOCK_SIZE) / BLOCK_SIZE;
	new_nblocks = ROUND(newsize, BLOCK_SIZE) / BLOCK_SIZE;

	if (newsize == 0) {
		new_nblocks = 0;
	}

	if (new_nblocks <= NDIRECT) {
		for (bno = new_nblocks; bno < old_nblocks; bno++) {
			panic_on(file_clear_block(f, bno));
		}
		if (f->f_indirect) {
			free_block(f->f_indirect);
			f->f_indirect = 0;
		}
	} else {
		for (bno = new_nblocks; bno < old_nblocks; bno++) {
			panic_on(file_clear_block(f, bno));
		}
	}
	f->f_size = newsize;
	dirty_fcb(f);
}

// Overview:
//  Set file size to newsize.
int file_set_size(struct File *f, u_int newsize) {
	if (f->f_size > newsize) {
		file_truncate(f, newsize);
	} else {
		f->f_size = newsize;
		dirty_fcb(f);
	}

	return 0;
}

// Overview:
//  Flush the contents of file f out to disk.
//  Loop over all the blocks in file.
//  Translate the file block number into a disk block number and then
//  check whether that disk block is dirty. If so, write it out.
//
// Hint: use file_map_block, block_is_dirty, and write_block.
void file_flush(struct File *f) {
	u_int nblocks;
	u_int bno;
	u_int diskbno;
	int r;

	nblocks = ROUND(f->f_size, BLOCK_SIZE) / BLOCK_SIZE;

	for (bno = 0; bno < nblocks; bno++) {
		if ((r = file_map_block(f, bno, &diskbno, 0)) < 0) {
			continue;
		}
		if (block_is_dirty(diskbno)) {
			write_block(diskbno);
		}
	}
}

// Overview:
//  Sync the entire file system.  A big hammer.
void fs_sync(void) {
	int i;
	for (i = 0; i < super->s_nblocks; i++) {
		if (block_is_dirty(i)) {
			write_block(i);
		}
	}
}

// Overview:
//  Close a file.
void file_close(struct File *f) {
	// Flush the file itself. Then unmap all blocks of the file.
	file_flush(f);
	// Remove the check for FTYPE_REG - unmap blocks for both regular files and directories
	u_int nblock = f->f_size / BLOCK_SIZE;
	for (int i = 0; i < nblock; i++) {
		u_int diskbno;
		if (file_map_block(f, i, &diskbno, 0) < 0) {
			debugf("file_close: file_map_block failed\n");
			break;
		}
		unmap_block(diskbno);
	}
}

// Overview:
//  Remove a file by truncating it and then zeroing the name.
int file_remove(char *path) {
	int r;
	struct File *f;

	// Step 1: find the file on the disk.
	if ((r = walk_path(path, 0, &f, 0)) < 0) {
		return r;
	}

	// Step 2: truncate it's size to zero.
	file_truncate(f, 0);

	// Step 3: clear it's name.
	f->f_name[0] = '\0';

	// Step 4: flush f's dir.
	dirty_fcb(f);
	struct File *dir_fcb = get_dir_fcb(f->f_dir_block, f->f_dir_offset);
	if (dir_fcb) {
		file_flush(dir_fcb);
	}

	return 0;
}
