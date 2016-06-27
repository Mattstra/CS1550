/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.

	gcc -Wall `pkg-config fuse --cflags --libs` cs1550.c -o cs1550
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define	MAX_FILES_IN_DIR (BLOCK_SIZE - (MAX_FILENAME + 1) - sizeof(int)) / \
	((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(unsigned long))

struct cs1550_directory_entry
{
	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_directory_entry cs1550_directory_entry;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;

struct cs1550_disk_block
{
	//The first 4 bytes will be the value 0xF113DA7A
	unsigned long magic_number;
	//And all the rest of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

//How many pointers in an inode?
#define NUM_POINTERS_IN_INODE (BLOCK_SIZE - sizeof(unsigned int) - sizeof(unsigned long)) / sizeof(unsigned long)

struct cs1550_inode
{
	//The first 4 bytes will be the value 0xFFFFFFFF
	unsigned long magic_number;
	//The number of children this node has (either other inodes or data blocks)
	unsigned int children;
	//An array of disk pointers to child nodes (either other inodes or data)
	unsigned long pointers[NUM_POINTERS_IN_INODE];
};

typedef struct cs1550_inode cs1550_inode;


/* Helper functions */


/* Gets root directory  */
static void get_root_dir(cs1550_root_directory *root_ptr)
{
	FILE* fp;
        fp = fopen(".disk", "rb");

        fread(root_ptr, sizeof(struct cs1550_root_directory), 1, fp);
	fclose(fp);
}

/* Checks to see if directory exists in root directory */
static int check_dir(char* dir)
{
	printf("directory to find is %s\n", dir);
	cs1550_root_directory root;
	get_root_dir(&root);
	int i;
	
	for(i = 0; i < root.nDirectories; i++)
	{
		if(strcmp(dir, root.directories[i].dname) == 0)
		{
			return 1;
		}
	}
	return -1;
}

/* Returns subdirectory entry */
static void get_sub_dir(char* dir, cs1550_directory_entry* dir_entry)
{
	cs1550_root_directory root;
	get_root_dir(&root);


	int block_num;
	int offset;
	int i;
	for(i = 0; i < root.nDirectories; i++)
	{
		if(strcmp(dir, root.directories[i].dname) == 0)
		{
			block_num = root.directories[i].nStartBlock;
			break;
		}
	}

	FILE* fp;
        fp = fopen(".disk", "rb");
	offset = (block_num*512);
	//seek to block number
	fseek(fp, offset, SEEK_SET);

	//read entire 512 byte block
	fread(dir_entry, sizeof(struct cs1550_directory_entry), 1, fp);
	fclose(fp);
}

/* Checks to see if a file exists */
static int check_file(char* dir, char* file, char* ext)
{
	cs1550_directory_entry sub_dir;
	get_sub_dir(dir, &sub_dir);

	int i;
	for(i = 0; i < sub_dir.nFiles; i++)
	{
		if((strcmp(file, sub_dir.files[i].fname) == 0) && (strcmp(ext, sub_dir.files[i].fext) == 0))
		{
			//found file
			return sub_dir.files[i].fsize;
		}
	}

	return -1;	

}


/* There are 10240 512 byte blocks in 5mb so
 * the starting offset for the last 3 blocks to 
 * store the bitmap is 5241344
 */


/* since it has to be bit per block, each individual bit in a char is used to
 * keep track of a block so 10240 blocks / 8 bits = 1280
 */
#define bitmap_size 1280	

//For action: 0 means allocate, and 1 means free block
static void update_bitmap(int block_number, int action)
{
	FILE* fp;
	int index;
	int bit;

	fp = fopen(".disk", "rb+");

	//seek to offset for bitmap
	fseek(fp, 5241344, SEEK_SET);

	//char array to represent bitmap
	unsigned char *bitmap = malloc(bitmap_size * sizeof(unsigned char));

	//copy bitmap on disk to buffer
	fread(bitmap, sizeof(unsigned char), 1280, fp);

	//alocate block
	if(action == 0)
	{
		//set individual bit representing a block to 1
		if((block_number % 8) == 0)
		{
			index = (block_number /8)-1;
			bit = 7;	//go to last bit in char
			//set bit to 1
			bitmap[index] = bitmap[index] | (1 << bit);
			
		}

		else
		{
			index = (block_number /8);
			bit = (block_number % 8);
			//set bit to 1
                        bitmap[index] = (bitmap[index] | (1 << bit));
		}
		
	}

	//free block
	else
	{
		//set individual bit representing a block to 0
                if((block_number % 8) == 0)
                {
                        index = (block_number /8)-1;
                        bit = 7;        //go to last bit in char
                        //set bit to 0
                        bitmap[index] = bitmap[index] & ~(1 << bit);

                }

                else
                {
                        index = (block_number /8);
                        bit = (block_number % 8)-1;
                        //set bit to 0
                        bitmap[index] = bitmap[index] & ~(1 << bit);
                }
	}

	//write to file
	fseek(fp, 5241344, SEEK_SET);
	fwrite(bitmap, 1, 1280, fp);
	fclose(fp);
}

static int get_free_block()
{
	FILE* fp;
	fp = fopen(".disk", "rb");
	int block = 1;
	int offset = 5241344;
	//go to bitmap
	fseek(fp, offset, SEEK_SET);
	
	int i;
	int j;
	for(i = 0; i < bitmap_size; i++)
	{
		for(j = 0; j < 8; j++)
		{

			//block 0 is used for root entry 
			if(i == 0 && j== 0)
			{
				
			}

			else
			{
				//get current poisition pointer
                                long pos = ftell(fp);

				unsigned char cur_char = fgetc(fp);
				int cur_bit = (cur_char & ( 1 << j )) >> j;
				
                                if(cur_bit == 0)
                                {
                                        //found free block
                                        fclose(fp);
                                        update_bitmap(block, 0);
                                        return block;
                                }

                                else
                                {
                                        if(j == 7)
                                        {
                                                offset++;
                                                //go to next char (8 blocks)
                                                fseek(fp,offset, SEEK_SET);
                                                block++;
                                        }
                                        else
					{
                                                block++;
						fseek(fp, pos, SEEK_SET);
					}
                                }
			}
		}
		
	}
	
}

static void write_to_disk(int block_number, char* dir)
{
	FILE* fp;
	fp = fopen(".disk", "rb+");
	cs1550_root_directory root;
	get_root_dir(&root);

	//copy updated info to root
	struct cs1550_directory_entry new_dir;
	root.nDirectories++;
        strcpy(root.directories[root.nDirectories-1].dname, dir);

	//get free block to store directory
        int free_block = get_free_block();
        root.directories[root.nDirectories-1].nStartBlock = (long)free_block;

	//first write root back to disk
	fwrite(&root, sizeof(struct cs1550_root_directory), 1, fp);
	fclose(fp);

	fp = fopen(".disk", "rb+");
	fseek(fp, free_block * 512, SEEK_SET);
	fwrite(&new_dir, sizeof(struct cs1550_directory_entry), 1, fp);
	fclose(fp);
}

static void write_file_to_disk(char* file, char* dir, char* ext)
{
	cs1550_directory_entry sub;
	sub.nFiles++;
	
	strcpy(sub.files[sub.nFiles-1].fname, file);
	strcpy(sub.files[sub.nFiles-1].fext, ext);
	sub.files[sub.nFiles-1].fsize = 0;

	//get free block for inode of file
	int free = get_free_block();	
	sub.files[sub.nFiles-1].nStartBlock = (long)free;

	//write updated sub directory
	cs1550_root_directory root;
        get_root_dir(&root);


        int block_num;
        int offset;
        int i;
        for(i = 0; i < root.nDirectories; i++)
        {
                if(strcmp(dir, root.directories[i].dname) == 0)
                {
                        block_num = root.directories[i].nStartBlock;
                        break;
                }
        }

        FILE* fp;
        fp = fopen(".disk", "rb+");
        offset = (block_num*512);
        //seek to block number
        fseek(fp, offset, SEEK_SET);
	fwrite(&sub, sizeof(struct cs1550_directory_entry), 1, fp);
	fclose(fp);

	//write file inode to disk
	struct cs1550_inode inode;

	inode.magic_number = 0xFFFFFFFF;
	inode.children = 0;
	
        fp = fopen(".disk", "rb+");
	fseek(fp, free * 512, SEEK_SET);
	fwrite(&inode, sizeof(struct cs1550_inode), 1, fp);
	fclose(fp);	

}




/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not.
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	memset(stbuf, 0, sizeof(struct stat));

	//character buffers for filename, extension, and directory
	char filename[MAX_FILENAME+1];
	char extension[MAX_EXTENSION+1];
	char directory[MAX_FILENAME+1];

	memset(filename, 0, MAX_FILENAME+1);
	memset(extension, 0, MAX_EXTENSION+1);
	memset(directory, 0, MAX_FILENAME+1);

	//is path the root dir?
	if (strcmp(path, "/") == 0) 
	{
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} 
	else
	{
		//parse elements of path into their respective buffers
        	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		if(strlen(filename) == 0)
		{
	
			/* Is it a subdirectory? */
			if(check_dir(directory) == 1)
			{
				stbuf->st_mode = S_IFDIR | 0755;
				stbuf->st_nlink = 2;
				res = 0;
			}

			else
				res = -ENOENT;
		}

		else
		{
			/* Is it a file? */
			int fsize = check_file(directory, filename, extension);
			if(fsize != -1)
			{
				printf("filename is known\n");
				stbuf->st_mode = S_IFREG | 0666;
				stbuf->st_nlink = 1;
				stbuf->st_size = fsize;
				res = 0;
			}
			else
				res = -ENOENT;
		}
	}
	return res;
}

/*
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;

	//character buffers for filename, extension, and directory
        char filename[MAX_FILENAME+1];
        char extension[MAX_EXTENSION+1];
        char directory[MAX_FILENAME+1];

	//If path is root
	if (strcmp(path, "/") == 0)
	{
		cs1550_root_directory root;
		get_root_dir(&root);

		int i;
		for(i = 0; i < root.nDirectories; i++)
		{
			filler(buf, root.directories[i].dname, NULL, 0);
		}
	}

	else
	{
		//parse elements of path into their respective buffers
        	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

		//It is a subdirectory
		if(check_dir(directory) ==1)
		{
			//returns all filenames + extensions
			cs1550_root_directory root;
			get_root_dir(&root);

			cs1550_directory_entry sub_dir;
			get_sub_dir(directory, &sub_dir);

			int j;
			for(j = 0; j < sub_dir.nFiles; j++)
			{
				//need to add period to extention
				filler(buf, (strcat(strcat(sub_dir.files[j].fname, "."), sub_dir.files[j].fext)), NULL, 0);
			}
		}

		//directory not found
		else
			return -ENOENT;		
	}
	

	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	/*
	//add the user stuff (subdirs or files)
	//the +1 skips the leading '/' on the filenames
	filler(buf, newpath + 1, NULL, 0);
	*/
	return 0;
}

/*
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) mode;
	(void) path;
	

	//character buffers for filename, extension, and directory
        char filename[MAX_FILENAME+1];
        char extension[MAX_EXTENSION+1];
        char directory[MAX_FILENAME+1];
	memset(filename, 0, MAX_FILENAME+1);
	memset(extension, 0, MAX_EXTENSION+1);
	memset(directory, 0, MAX_FILENAME+1);

        //parse elements of path into their respective buffers
        sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	//check for errors
	if(strlen(directory) > 8)
		return -ENAMETOOLONG;

	if(strlen(filename) > 0)
		return -EPERM;

	//check to see if directory already exists
	if(check_dir(directory) == 1)
		return -EEXIST;

	//create new directory
	cs1550_root_directory root;
	get_root_dir(&root);

	//If there is room then add directory
	if(root.nDirectories < MAX_DIRS_IN_ROOT)
	{
		//write directory and root struct to disk
		write_to_disk(root.directories[root.nDirectories].nStartBlock, directory);				
	}
			
	return 0;
}

/*
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

/*
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
	(void) dev;
	(void) path;

	//character buffers for filename, extension, and directory
        char filename[MAX_FILENAME+1];
        char extension[MAX_EXTENSION+1];
        char directory[MAX_FILENAME+1];

	memset(filename, 0, MAX_FILENAME+1);
        memset(extension, 0, MAX_EXTENSION+1);
        memset(directory, 0, MAX_FILENAME+1);

	//parse elements of path into their respective buffers
        sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	

	if(strlen(filename) == 0)
		return -EPERM;
	else
	{
		printf("filename is %s\n", filename);
		if(strlen(filename) > 8)
                	return -ENAMETOOLONG;
		else
		{
			//check if file exists
        		if(check_file(directory, filename, extension) == 1)
        		{
                		return -EEXIST;
        		}

			//file doesn't exist
        		else
        		{
                		//create file
                		write_file_to_disk(filename, directory, extension);
        		}

		}

	}
	return 0;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

	char filename[MAX_FILENAME+1];
        char extension[MAX_EXTENSION+1];
        char directory[MAX_FILENAME+1];
        memset(filename, 0, MAX_FILENAME+1);
        memset(extension, 0, MAX_EXTENSION+1);
        memset(directory, 0, MAX_FILENAME+1);

        //parse elements of path into their respective buffers
        sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	if((check_dir(directory) == 1) || (strcmp(path, "/") == 0))
		return -EISDIR;

	if(check_file(directory, filename, extension) != -1)
		return -ENOENT;

	else
	{
		//look for requested file
                struct cs1550_directory_entry sub;
                get_sub_dir(directory, &sub);

                FILE* fp;
		int file_num;
                int i;

                for(i = 0; i < sub.nFiles; i++)
                {
                       if((strcmp(filename, sub.files[i].fname) == 0) && (strcmp(extension, sub.files[i].fext) == 0))
                       {
                              //found file
			      file_num = i;
                              break;
                       }
                }

		//free pointers
		cs1550_inode inode;

		
	}


    return 0;
}

/*
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	char filename[MAX_FILENAME+1];
        char extension[MAX_EXTENSION+1];
        char directory[MAX_FILENAME+1];
        memset(filename, 0, MAX_FILENAME+1);
        memset(extension, 0, MAX_EXTENSION+1);
        memset(directory, 0, MAX_FILENAME+1);

	//parse elements of path into their respective buffers
        sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	//see if path exists
        if(strlen(filename) == 0)
        {
                return -EISDIR;
        }

	else
	{
		if(size <= 0)
        	{
               		//error
        	}

		else
		{
			//look for requested file
                	struct cs1550_directory_entry sub;
                	get_sub_dir(directory, &sub);

			FILE* fp;

                	int i;
			int file_inode;
			int fsize;

                	for(i = 0; i < sub.nFiles; i++)
                	{
                        	if((strcmp(filename, sub.files[i].fname) == 0) && (strcmp(extension, sub.files[i].fext) == 0))
                        	{
                                	//found file
                                	file_inode = sub.files[i].nStartBlock;
                                	fsize = sub.files[i].fsize;
                                	break;
                        	}
                	}

			if(offset > fsize)
				return -EFBIG;

			else
			{
				int data_read = 0;
				cs1550_inode inode;
				fp = fopen(".disk", "rb");
				fseek(fp, file_inode*512, SEEK_SET);
				fread(&inode, sizeof(struct cs1550_inode), 1, fp);
				fclose(fp);

				//for now dont worry if children contain other inodes
				int num_blocks = inode.children;

				int start_block = offset / 512;         //block to start reading
                		int byte_offset = offset % 512;         //byte to start reading from
				
				//go to starting block to read from
				cs1550_disk_block data;
				fopen(".disk", "rb");
				fseek(fp, inode.pointers[start_block], SEEK_SET);
				fread(&data, sizeof(struct cs1550_disk_block), 1, fp);
				fclose(fp);

				memcpy(buf, data.data, sizeof(data.data));
				printf("data copied is %s\n", buf);
				data_read = sizeof(buf);
				printf("size of buf is %d\n", data_read);
				printf("size is %d\n", size);

				while(1)
				{
					if(data_read >= size)
					{
						printf("A\n");
						break;
					}

					else
					{
						printf("B\n");
						break;
						//read next block
					}
					
				}
			}
		}
				

	}

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//read in data
	//set size and return, or error

	size = 0;

	return size;
}

/*
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size,
			  off_t offset, struct fuse_file_info *fi)
{
	(void) buf;
	(void) offset;
	(void) fi;
	(void) path;

	char filename[MAX_FILENAME+1];
        char extension[MAX_EXTENSION+1];
        char directory[MAX_FILENAME+1];
	memset(filename, 0, MAX_FILENAME+1);
	memset(extension, 0, MAX_EXTENSION+1);
	memset(directory, 0, MAX_FILENAME+1);

	int file_inode;
	int fsize;

	//parse elements of path into their respective buffers
        sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	struct cs1550_directory_entry sub;
        get_sub_dir(directory, &sub);
	int file_num;

	//see if path exists
	if(strcmp(path, "/") == 0)
	{
		printf("Error, path is root\n");
		exit(0);
	}

	if(size <= 0)
	{
		//error
	}

	else
	{
		//look for requested file

		int i;
		for(i = 0; i < sub.nFiles; i++)
		{
			if((strcmp(filename, sub.files[i].fname) == 0) && (strcmp(extension, sub.files[i].fext) == 0))
			{
				//found file
				file_inode = sub.files[i].nStartBlock;
				file_num = i;
				fsize = sub.files[i].fsize;
				break;
			}
		}
	}

	if(offset > fsize)
		return -EFBIG;

	else
	{
		int start_block = offset / 512;		//block to start writing
		int byte_offset = offset % 512;		//byte to start writing to 

		//keep track of number of allocated blocks to know when to
		//allocate more if necessary
		int allocated = 0;

		printf("offset is %d\n", byte_offset);

		//number of disk blocks that are going to be written
		int num_blocks = size / MAX_DATA_IN_BLOCK;
		printf("number of blocks needed is %d\n", num_blocks);

		if(num_blocks == 0)
			num_blocks++;

		//number of inodes needed
		int num_inodes;

		if(num_blocks < (NUM_POINTERS_IN_INODE-1))
		{
			//only need 1 inode
			num_inodes = 1;
		}
		else
		{
			//need to know how many inodes will be needed
			num_inodes = num_blocks / (NUM_POINTERS_IN_INODE-1);
			if((num_blocks % (NUM_POINTERS_IN_INODE-1)) != 0)
				num_inodes++;
		}

		//inode of file (first)
		cs1550_inode inode;
		FILE* fp;
		fp = fopen(".disk", "rb+");
		fseek(fp, file_inode * 512, SEEK_SET);
		fread(&inode, sizeof(struct cs1550_inode), 1, fp);
		fclose(fp);


		//first write to a file
		if((start_block == 0) && (byte_offset == 0))
		{
			printf("first write to a file\n");
			//add disk block to inode pointer
			inode.children++;
			//point to offset for disk block
			inode.pointers[0] = get_free_block() * 512;

			//add to number of allocated disk blocks
			allocated++;	
		}

		else
		{
			allocated = (inode.children - start_block) + 1;
		}

		fp = fopen(".disk", "rb+");
		cs1550_disk_block data;

		if(num_blocks == 1)
		{
			//copy data in buf to disk block data array
                        memcpy(data.data, buf, size);

			data.magic_number = 0xF113DA7A;
			printf("data written is %s\n", data.data);

			//seek to disk block and write
			fseek(fp, inode.pointers[0], SEEK_SET);
			fwrite(&data, sizeof(struct cs1550_disk_block), 1, fp);
			fclose(fp);

			//write back inode
			fp = fopen(".disk", "rb+");
			fseek(fp, file_inode * 512, SEEK_SET);
                	fwrite(&inode, sizeof(struct cs1550_inode), 1, fp);
                	fclose(fp);

			sub.files[file_num].fsize+= 512;
		}

		else
		{	int i = start_block;
			int done = 0;
			cs1550_disk_block new;

			while(1)
			{
				if(allocated == 0)
				{
					//allocate more blocks
					inode.children++;
					inode.pointers[inode.children] = get_free_block() * 512;
					sub.files[file_num].fsize+= 512;
				}

				if(i == (NUM_POINTERS_IN_INODE -1))
				{
					//only 1 free space left so have to point to new inode
					//write inode to disk
					fp = fopen(".disk", "rb+");
					fseek(fp, file_inode * 512 , SEEK_SET);
					
					//point last pointer to a new inode
					inode.pointers[inode.children] = get_free_block() * 512;
					fwrite(&inode, sizeof(struct cs1550_inode), 1, fp);
					
					//set inode to new one
					memset(&inode, 0, sizeof(cs1550_inode));
					fseek(fp, inode.pointers[inode.children], SEEK_SET);
					
					//see if an inode is already there
					fread(&inode, sizeof(struct cs1550_inode), 1, fp);

					if(inode.magic_number == 0xFFFFFFFF)
					{
						//inode already there
					}

					else
					{
						//make new inode
						inode.magic_number = 0xFFFFFFFF;
						file_inode = ftell(fp) / 512;
					}

					//need to decrement so when to stop looping isnt thrown off
					done--;
				}	

				else
				{
					//write to disk block
					fseek(fp, inode.pointers[inode.children], SEEK_SET);

					//copy data in buf to disk block data array
                        		memcpy(new.data, buf, size);
                        		new.magic_number = 0xF113DA7A;
                        		printf("data written is %s\n", new.data);
					fwrite(&data, sizeof(struct cs1550_disk_block), 1, fp);
				}

				if(done == num_blocks)
                                        break;

				i++;	//increment block
				done++;
				allocated--;	//decrement # of allocated blocks

			}

			//write back inode
			fseek(fp, file_inode * 512, SEEK_SET);
			fwrite(&inode, sizeof(struct cs1550_inode), 1, fp);

			//write back sub directory
        		cs1550_root_directory root;
        		get_root_dir(&root);

        		int block_num;
        		int offset;
        		int k;
        		for(k = 0; k < root.nDirectories; k++)
        		{
                		if(strcmp(directory, root.directories[k].dname) == 0)
                		{
                        		block_num = root.directories[k].nStartBlock;
                       			break;
                		}
        		}

        		offset = (block_num*512);
        		//seek to block number
        		fseek(fp, offset, SEEK_SET);
        		fwrite(&sub, sizeof(struct cs1550_directory_entry), 1, fp);
			fclose(fp);

		}


	}



	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//write data
	//set size (should be same as input) and return, or error

	return size;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/*
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
