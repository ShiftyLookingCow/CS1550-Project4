/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.

*/

//Cody Whited
//cjw51@pitt.edu

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
#define	MAX_DATA_IN_BLOCK BLOCK_SIZE

//How many pointers in an inode?
#define NUM_POINTERS_IN_INODE ((BLOCK_SIZE - sizeof(unsigned int) - sizeof(unsigned long)) / sizeof(unsigned long))

struct cs1550_directory_entry
{
	char dname[MAX_FILENAME	+ 1];	//the directory name (plus space for a nul)
	int nFiles;			//How many files are in this directory. 
					//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;			//file size
		long nStartBlock;		//where the first block is on disk
	} files[MAX_FILES_IN_DIR];		//There is an array of these
};

typedef struct cs1550_directory_entry cs1550_directory_entry;

struct cs1550_disk_block
{
	//And all of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

//If the directory exists, returns its location on disk and stores in entry parameter
//Otherwise, returns -1
static int get_directory(char * directory, struct cs1550_directory_entry * entry){
	FILE *file;
	file=fopen(".directories", "rb");
	
	int result = -1;
	
	if (!file)
	{
		file=fopen(".directories", "wb"); //Create new .directories file if it fails to open
	}
	else
	{		
		//Iterate through directories
		while(fread(entry, sizeof(struct cs1550_directory_entry), 1, file) >= 1)
		{				
			//Check if entry matches directory name
			if(strcmp(entry->dname, directory) == 0)
			{
				result = ftell(file) - sizeof(struct cs1550_directory_entry); //Location is one entry size back from file pointer
				break;
			}
		} 
	}
	
	fclose(file);
	
	return result;
}

//Parses the path into a directory, filename, and extension, and handles null terminators
void parse_path(char * path, char * directory, char * filename, char * extension)
{
	//Clear strings
	directory[0] = '\0';
	filename[0] = '\0';
	extension[0] = '\0';
	
	sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	
	//Null terminate strings
	directory[MAX_FILENAME] = '\0';
	filename[MAX_FILENAME] = '\0';
	extension[MAX_EXTENSION] = '\0';
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
	
	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
		//Parse path
		char directory[MAX_FILENAME + 1];
		char filename[MAX_FILENAME + 1];
		char extension[MAX_EXTENSION + 1];
		
		parse_path(path, directory, filename, extension);
		
		//Check if directory exists
		struct cs1550_directory_entry * entry = malloc(sizeof(struct cs1550_directory_entry));
		int location = get_directory(directory, entry);
		
		if(location != -1) //Found directory
		{
			if(strlen(filename) == 0) //If filename is blank, we are looking for the directory
			{
				stbuf->st_mode = S_IFDIR | 0755;
				stbuf->st_nlink = 2;
			}
			else //Otherwise, look for the file
			{
				//Check directory entry
				int i, found = 0;
				for(i=0; i<entry->nFiles; i++)
				{
					if(strcmp(entry->files[i].fname, filename) == 0 && strcmp(entry->files[i].fext, extension) == 0)
					{
						found = 1;
						break;
					}
				}
				
				if(found == 1)
				{
					//regular file, probably want to be read and write
					stbuf->st_mode = S_IFREG | 0666; 
					stbuf->st_nlink = 1; //file links
					stbuf->st_size = entry->files[i].fsize; //file size
				}
				else //File does not exist
				{
					res = -ENOENT;
				}
			}
		}
		else //Directory does not exist
		{
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

	//Check if path is root
	if (strcmp(path, "/") == 0)
	{
		filler(buf, ".", NULL, 0);
		filler(buf, "..", NULL, 0);
		
		//Find directories, add them to buffer
		FILE *file;
		file=fopen(".directories", "rb");
		if (!file)
		{
			file=fopen(".directories", "wb");
		}
		else
		{
			struct cs1550_directory_entry current;
			
			//Iterate through directories
			while(fread(&current, sizeof(struct cs1550_directory_entry), 1, file) >= 1)
			{			
				//Add directory to list
				filler(buf, current.dname, NULL, 0);
			} 
		}
		fclose(file);
	}
	else //Otherwise, look for files in subdirectory
	{
		//Parse path
		char directory[MAX_FILENAME + 1];
		char filename[MAX_FILENAME + 1];
		char extension[MAX_EXTENSION + 1];
		
		parse_path(path, directory, filename, extension);
		
		//Check if directory exists
		struct cs1550_directory_entry * entry = malloc(sizeof(struct cs1550_directory_entry));
		int location = get_directory(directory, entry);
		
		//Directory entry was found
		if(location != -1)
		{
			filler(buf, ".", NULL, 0);
			filler(buf, "..", NULL, 0);
		
			//Check if file already exists
			int i;
			for(i=0; i<entry->nFiles; i++)
			{
				//Construct full filename
				char fullFilename[MAX_FILENAME + MAX_EXTENSION + 2];
				strcpy(fullFilename, entry->files[i].fname);
				if(strlen(entry->files[i].fext) != 0)
				{
					strcat(fullFilename, ".");
					strcat(fullFilename, entry->files[i].fext);
				}
				
				filler(buf, fullFilename, NULL, 0);
			}
		}
		else //Directory entry was invalid or otherwise not found
		{
			return -ENOENT;
		}
	}

	return 0;
}

/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) path;
	(void) mode;
	
	int res = 0;
	
	//Parse path
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	
	parse_path(path, directory, filename, extension);
	
	if(strlen(directory) > MAX_FILENAME)
	{
		printf("Max directory name length is 8\n");
		return -ENAMETOOLONG;
	}
	
	//Check if directory exists
	struct cs1550_directory_entry * entry = malloc(sizeof(struct cs1550_directory_entry));
	int location = get_directory(directory, entry);
	
	if(location != -1) //found
	{
		res = -EEXIST;
	}
	
	FILE *file;
	//Append new path to file
	if(res == 0)
	{
		//Open .directories
		file=fopen(".directories", "ab");
		if(!file)
		{
			res = -1; //Failed to open .directories to append new directory
		}
		else
		{
			struct cs1550_directory_entry current;
			
			strcpy(current.dname, directory);
			current.nFiles = 0;
			
			if(fwrite(&current, sizeof(struct cs1550_directory_entry), 1, file) < 1)
			{
				printf("Failed to write entry to .directories\n");
			}
		}
		fclose(file);
	}

	return res;
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
	
	//Parse path
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	
	parse_path(path, directory, filename, extension);
	
	if(strlen(directory) > MAX_FILENAME || strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION)
	{
		return -ENAMETOOLONG;
	}
	
	//Make sure directory exists
	struct cs1550_directory_entry * entry = malloc(sizeof(struct cs1550_directory_entry));
	int diskLocation = get_directory(directory, entry);
	
	//Check if file already exists
	int i;
	for(i=0; i<entry->nFiles; i++)
	{
		if(strcmp(entry->files[i].fname, filename) == 0 && strcmp(entry->files[i].fext, extension) == 0)
		{
			return -EEXIST;
		}
	}
	
	//Check that directory is not full
	if(entry->nFiles == MAX_FILES_IN_DIR)
	{
		return -EPERM;
	}
	
	//Add file to directory entry
	int index = entry->nFiles; //array index of next open space is the same as the number of files
	
	strcpy(entry->files[index].fname, filename);
	strcpy(entry->files[index].fext, extension);
	entry->files[index].fsize = 0;
	entry->files[index].nStartBlock = -1;
	entry->nFiles++;
	
	//TODO: find a free block, assign it to file's nStartBlock
	
	//Write entry to .directories file
	FILE* file = fopen(".directories", "r+b");
	fseek(file, diskLocation, SEEK_SET); //Move file pointer to diskLocation, from beginning
	fwrite(entry, sizeof(struct cs1550_directory_entry), 1, file);
	fclose(file);
	
	return 0;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;
	
	//Parse path
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	
	parse_path(path, directory, filename, extension);

	if(strlen(filename) == 0)
	{
		return -EISDIR;
	}
	
	//Find directory
	struct cs1550_directory_entry * entry = malloc(sizeof(struct cs1550_directory_entry));
	int diskLocation = get_directory(directory, entry);
	
	//Find file
	int i, fileIndex = -1;
	for(i=0; i<entry->nFiles; i++)
	{
		if(strcmp(entry->files[i].fname, filename) == 0 && strcmp(entry->files[i].fext, extension) == 0)
		{
			fileIndex = i;
			break;
		}
	}
	if(fileIndex == -1)
	{
		return -ENOENT; //File not found
	}
	
	//Remove from bitmap
	FILE* file = fopen(".disk", "r+b");
	if (!file)
	{
		return -ENOENT; //Could not open .disk
	}
	
	//Calculate the location on disk to write
	int byteToWrite = (entry->files[fileIndex].nStartBlock-3) / 8;
	//Calculate the mask used to flip the correct bit
	int mask = 0b10000000 >> ((entry->files[fileIndex].nStartBlock-3) % 8);
	
	fseek(file, byteToWrite, SEEK_SET); //Move to the appropriate character
	unsigned char cur = fgetc(file);
	
	cur ^= mask;
	
	fseek(file, byteToWrite, SEEK_SET); //Move back
	int res = fputc(cur, file);
	
	fclose(file);	
	
	//Delete from directory entry
	entry->files[fileIndex] = entry->files[entry->nFiles - 1]; //Set deleted file to the current last file, no need to keep files in any order
	entry->nFiles--;
	
	//Write new directory
	file = fopen(".directories", "r+b");
	fseek(file, diskLocation, SEEK_SET); //Move file pointer to diskLocation, from beginning
	fwrite(entry, sizeof(struct cs1550_directory_entry), 1, file);
	fclose(file);

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

	//Parse path
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	
	parse_path(path, directory, filename, extension);
	
	//Find directory
	struct cs1550_directory_entry * entry = malloc(sizeof(struct cs1550_directory_entry));
	int diskLocation = get_directory(directory, entry); //Assume directory can be found
	
	//Find file
	int i, fileIndex = -1;
	for(i=0; i<entry->nFiles; i++)
	{
		if(strcmp(entry->files[i].fname, filename) == 0 && strcmp(entry->files[i].fext, extension) == 0)
		{
			fileIndex = i;
			break;
		}
	}
	
	//check that size is > 0
	if(size == 0)
	{
		return 0; //Success?
	}
	
	//check that offset is <= to the file size
	if(offset > entry->files[i].fsize)
	{
		return -EFBIG;
	}
	
	//Read in data
	int locationToRead = ((entry->files[fileIndex].nStartBlock * 512) + offset);
	
	FILE* file = fopen(".disk", "rb");
	fseek(file, locationToRead, SEEK_SET);
	int read = fread(buf, 1, size, file);
	fclose(file);

	return read;
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
	
	//Parse path
	char directory[MAX_FILENAME + 1];
	char filename[MAX_FILENAME + 1];
	char extension[MAX_EXTENSION + 1];
	
	parse_path(path, directory, filename, extension);

	if(strlen(filename) == 0)
	{
		return -EISDIR;
	}
	
	//Find directory
	struct cs1550_directory_entry * entry = malloc(sizeof(struct cs1550_directory_entry));
	int diskLocation = get_directory(directory, entry); //Assume directory can be found
	
	//Find file
	int i, fileIndex = -1;
	for(i=0; i<entry->nFiles; i++)
	{
		if(strcmp(entry->files[i].fname, filename) == 0 && strcmp(entry->files[i].fext, extension) == 0)
		{
			fileIndex = i;
			break;
		}
	}
	
	//check that size is > 0
	if(size == 0)
	{
		return 0; //Success?
	}
	
	//check that offset is <= to the file size
	if(offset > entry->files[i].fsize)
	{
		return -EFBIG;
	}
	
	FILE* file;
	//If file is not already assigned a block, use bitmap to find an open block
	if(entry->files[fileIndex].nStartBlock == -1) 
	{
		//Open .disk to read bitmap
		file = fopen(".disk", "r+b");
		if (!file)
		{
			return 0; //Could not open disk
		}
		
		//Use bitmap to find a free block and assign to file
		unsigned char cur;
		int freeBlockNumber = -1;
		for(i=0; i<(BLOCK_SIZE * 2.5); i++) //Bitmap is contained in first 2.5 blocks of disk
		{
			cur = fgetc(file);
			printf("Read %d from file\n", cur);
			int j;
			int mask = 0b10000000;
			
			for(j=0; j<8; j++) //Read character bit by bit
			{
				printf("Comparing mask %d and current byte %d\n", mask, cur);
				if((mask & cur) == 0)
				{
					//Found free block
					freeBlockNumber = ((i * 8) + j) + 3; //+ 3 is to skip the three blocks used by bitmap
					
					//Mark as now in use
					cur |= mask;
					
					//Move file pointer back to previous byte location in bitmap
					long prevLoc = ftell(file) - sizeof(unsigned char); 
					fseek(file, prevLoc, SEEK_SET);
					
					int res = fputc(cur, file);
					
					//printf("Wrote %d to file\nResult was %d\n", cur, res);
					fclose(file);				
					break;
				}
				mask >>= 1;
			}
			
			if(freeBlockNumber != -1)
				break;
		}
		
		if(freeBlockNumber == -1)
		{
			printf("Out of disk space\n");
			return 0;
		}
		
		entry->files[fileIndex].nStartBlock = freeBlockNumber;
	}
	
	int blockToUse = entry->files[fileIndex].nStartBlock;
	
	//Write data
	file = fopen(".disk", "r+b");
	fseek(file, ((blockToUse * BLOCK_SIZE) + offset), SEEK_SET);
	int elementsWritten = fwrite(buf, size, 1, file);
	fclose(file);
	
	//Update directory structure
	entry->files[fileIndex].fsize = size;
	
	file = fopen(".directories", "r+b");
	fseek(file, diskLocation, SEEK_SET);
	fwrite(entry, sizeof(struct cs1550_directory_entry), 1, file);
	fclose(file);
	
	if(elementsWritten != 1)
	{
		size = 0;
	}
	
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
