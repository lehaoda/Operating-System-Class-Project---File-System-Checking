//
//  fcheck.c
//  File System Checking
//
//  Created by Haoda LE on 2019/11/25.
//  Copyright © 2019 haoda le. All rights reserved.
//

#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>

#include "types.h"
#include "fs.h"

#define BLOCK_SIZE (BSIZE)
char bitarr[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
#define BITSET(bitmapblocks, blockaddr) ((*(bitmapblocks + blockaddr / 8)) & (bitarr[blockaddr % 8]))

typedef struct _image_t {
    uint numinodeblocks;
    uint numbitmapblocks;
    uint firstdatablock;
    struct superblock *sb;
    char *inodeblocks;
    char *bitmapblocks;
    char *datablocks;
    char *mmapimage;
} image_t;

//function definition.
//rule 1, 2, 3, 4, 5
void inode_check(image_t *image);
//rule 6
void bitmap_check(image_t *image);
//rule 7, 8
void blockaddrs_check(image_t *image);
//rule 9, 10, 11, 12
void directory_check(image_t *image);


int
main(int argc, char *argv[])
{
    int fsfd;
    image_t image;
    char *mmapimage;
    struct stat fileStat;
    
    //If no image file is provided, should print the usage error.
    if(argc < 2){
      fprintf(stderr, "Usage: fcheck <file_system_image>\n");
      exit(1);
    }

    //open img
    //If the file system image does not exist, should print image not found.
    fsfd = open(argv[1], O_RDONLY);
    if(fsfd < 0){
        perror(argv[1]);
        exit(1);
    }
    
    //get file stat
    if(fstat(fsfd,&fileStat) < 0){
        exit(1);
    }
        
    //mmap
    mmapimage = mmap(NULL, fileStat.st_size, PROT_READ, MAP_PRIVATE, fsfd, 0);
    if (mmapimage == MAP_FAILED){
        perror("mmap failed");
        exit(1);
    }
    
    image.mmapimage=mmapimage;
    
    //super block.
    image.sb=(struct superblock*)(mmapimage+1*BLOCK_SIZE);
    //number of blocks to store inode.
    image.numinodeblocks=(image.sb->ninodes/(IPB))+1;
    //number of blocks to store bitmap.
    image.numbitmapblocks=(image.sb->size/(BPB))+1;
    //start address of inode blocks.
    image.inodeblocks=(char *)(mmapimage+2*BLOCK_SIZE);
    //start address of bitmap blocks.
    image.bitmapblocks=(char *)(image.inodeblocks+image.numinodeblocks*BLOCK_SIZE);
    //start address of data blocks.
    image.datablocks=(char *)(image.bitmapblocks+image.numbitmapblocks*BLOCK_SIZE);
    //logical block number of first data blocks.
    image.firstdatablock=image.numinodeblocks+image.numbitmapblocks+2;
    
    //rule group to check inode, including rule 1, 2, 3, 4, 5
    inode_check(&image);
    
    //rule group to check bitmap, including rule 6
    bitmap_check(&image);
    
    //rule group to check block address, including rule 7, 8
    blockaddrs_check(&image);
    
    //rule group to check directory, including rule 9, 10, 11, 12
    directory_check(&image);
    
    exit(0);
}

//rule 1
//check inode type
//Each in-use inode is one of the valid types (T_FILE:2, T_DIR:1, T_DEV:3).
void check_inode_type(struct dinode *inode){
    if(inode->type!=T_FILE && inode->type!=T_DIR && inode->type!=T_DEV){
        printf("ERROR: bad inode.\n");
        exit(1);
    }
}

//rule 2
//check inode address, direct.
//check if the in-use direct block is invalid(points to a valid datablock address within the image).
void check_inode_direct_blocks(image_t *image, struct dinode *inode){
    int i;
    uint blockaddr;
    for(i=0;i<NDIRECT;i++){
        blockaddr=inode->addrs[i];
        if(blockaddr==0)
            continue;
        if(blockaddr<0 || blockaddr>=image->sb->size){
            printf("ERROR: bad direct address in inode.\n");
            exit(1);
        }
    }
}

//rule 2
//check inode address, indirect
//check if the in-use indirect block is invalid(points to a valid datablock address within the image).
void check_inode_indirect_blocks(image_t *image, struct dinode *inode){
    uint blockaddr;
    blockaddr=inode->addrs[NDIRECT];
    uint *indirectblk;
    int i;
    
    if(blockaddr==0){
        return;
    }
    if(blockaddr<0 || blockaddr>=image->sb->size){
        printf("ERROR: bad indirect address in inode.\n");
        exit(1);
    }
    
    //the block address where store the indirect addr.
    indirectblk=(uint *)(image->mmapimage+blockaddr*BLOCK_SIZE);
    for(i=0; i<NINDIRECT; i++, indirectblk++){
        blockaddr=*indirectblk;
        if(blockaddr==0){
            continue;
        }
        if(blockaddr<0 || blockaddr>=image->sb->size){
            printf("ERROR: bad indirect address in inode.\n");
            exit(1);
        }
    }
}

//rule 3 and rule 4
//rule 3: Root directory exists, its inode number is 1, and the parent of the root directory is itself.
//rule 4: Each directory contains . and .. entries, and the . entry points to the directory itself.
void check_dir(image_t *image, struct dinode *inode, int inum){
    int i, j, pfound, cfound;
    uint blockaddr;
    struct dirent *de;
    //cfound for '.', pfound for '..'
    pfound=cfound=0;
    
    for(i=0; i<NDIRECT; i++){
        blockaddr=inode->addrs[i];
        if(blockaddr==0)
            continue;
        
        de=(struct dirent *)(image->mmapimage+blockaddr*BLOCK_SIZE);
        for(j=0; j<DPB; j++, de++){
            
            //rule 4
            //if "." not point to itself, then error.
            if(!cfound && strcmp(".", de->name)==0){
                cfound=1;
                if(de->inum!=inum){
                    printf("ERROR: directory not properly formatted.\n");
                    exit(1);
                }
            }
            
            //rule 3
            //if inode 1's parent if not itself or if inode's parent is itself but inode number is not 1, then error.
            if(!pfound && strcmp("..",de->name)==0){
                pfound=1;
                if((inum!=1 && de->inum==inum) || (inum==1 && de->inum!=inum)){
                    printf("ERROR: root directory does not exist.\n");
                    exit(1);
                }
            }
            
            if(pfound && cfound) break;
        }
        
        if(pfound && cfound) break;
    }
    //rule 4
    //if "." or ".." not found, then error.
    if(!pfound || !cfound){
        printf("ERROR: directory not properly formatted.\n");
        exit(1);
    }
}

//rule 5
//For in-use inodes, each address in use is also marked in use in the bitmap.
void check_bitmap_addr(image_t *image, struct dinode *inode){
    int i,j;
    uint blockaddr;
    uint *indirect;
    
    for(i=0; i<(NDIRECT+1); i++){
        blockaddr=inode->addrs[i];
        if(blockaddr==0){
            continue;
        }
        
        if(!BITSET(image->bitmapblocks, blockaddr)){
            printf("ERROR: address used by inode but marked free in bitmap.\n");
            exit(1);
        }
        
        //for indirect address.
        if(i==NDIRECT){
            indirect=(uint *)(image->mmapimage+blockaddr*BLOCK_SIZE);
            for(j=0; j<NINDIRECT; j++, indirect++){
                blockaddr=*(indirect);
                if(blockaddr==0){
                    continue;
                }
                
                if(!BITSET(image->bitmapblocks, blockaddr)){
                    printf("ERROR: address used by inode but marked free in bitmap.\n");
                    exit(1);
                }
            }
        }
    }
}


//rule group to check inode, for rule 1, 2, 3, 4, 5.
//go through each inode, and check the rules if the inode is in use.
void inode_check(image_t *image){
    struct dinode *inode;
    int i;
    int count_not_allocated=0;
    
    inode=(struct dinode*)(image->inodeblocks);
    
    for(i=0;i<image->sb->ninodes;i++,inode++){
        if(inode->type==0){
            count_not_allocated++;
            continue;
        }
        
        //rule 1
        check_inode_type(inode);
        
        //rule 2
        check_inode_direct_blocks(image, inode);
        check_inode_indirect_blocks(image, inode);
        
        //rule 3 && rule 4 for inode 1
        if(i==1){
            //if inode 1 type is not directory, then error.
            if(inode->type!=T_DIR){
                printf("ERROR: root directory does not exist.\n");
                exit(1);
            }
            check_dir(image, inode, 1);
        }
        
        //rule 4 for inode!=1
        if(i!=1 && inode->type==T_DIR){
            check_dir(image, inode, i);
        }
        
        //rule 5
        check_bitmap_addr(image, inode);
        
    }
}


//function for rule 6
//look through the inode corresponding address. direct and indirect.
//mark if the block is actually in use.
void get_used_dbs(image_t *image, struct dinode *inode, int *used_dbs){
    int i, j;
    uint blockaddr;
    uint *indirect;
    
    for(i=0; i<(NDIRECT+1); i++){
        blockaddr=inode->addrs[i];
        if(blockaddr==0){
            continue;
        }
        
        used_dbs[blockaddr-image->firstdatablock]=1;
        
        //check inode's indirect address
        if(i==NDIRECT){
            indirect=(uint *)(image->mmapimage+blockaddr*BLOCK_SIZE);
            for(j=0; j<NINDIRECT; j++, indirect++){
                blockaddr=*(indirect);
                if(blockaddr==0){
                    continue;
                }
                
                used_dbs[blockaddr-image->firstdatablock]=1;
            }
        }
    }
}

//rule 6
//rule group to check bitmap, for rule 6.
//For blocks marked in-use in bitmap, the block should actually be in-use in an inode or indirect block somewhere.
void bitmap_check(image_t *image){
    struct dinode *inode;
    int i;
    int used_dbs[image->sb->nblocks];
    uint blockaddr;
    memset(used_dbs, 0, image->sb->nblocks*sizeof(int));
    
    //go through all inodes, mark the block which is actually in use.
    inode=(struct dinode*)(image->inodeblocks);
    for(i=0; i<image->sb->ninodes; i++, inode++){
        if(inode->type==0){
            continue;
        }
        get_used_dbs(image, inode, used_dbs);
    }
    
    //go through all data blocks, check if there's any block which is not in use but bitmap marked in use.
    for(i=0; i<image->sb->nblocks; i++){
        blockaddr=(uint)(i+image->firstdatablock);
        if(used_dbs[i]==0 && BITSET(image->bitmapblocks, blockaddr)){
            printf("ERROR: bitmap marks block in use but it is not in use.\n");
            exit(1);
        }
    }
}


//function for rule 7
//record the used direct address
void fill_duaddrs(image_t *image, struct dinode *inode, uint *duaddrs){
    int i;
    uint blockaddr;

    for (i=0; i<NDIRECT; i++) {
        blockaddr=inode->addrs[i];
        if(blockaddr==0){
            continue;
        }
        duaddrs[blockaddr-image->firstdatablock]++;
    }
}

//function for rule 8
//record the used indirect address
void fill_iuaddrs(image_t *image, struct dinode *inode, uint *iuaddrs){
    int i;
    uint *indirect;
    uint blockaddr=inode->addrs[NDIRECT];
    
    indirect=(uint *)(image->mmapimage+blockaddr*BLOCK_SIZE);
    for(i=0; i<NINDIRECT; i++, indirect++) {
        blockaddr=*(indirect);
        if(blockaddr==0){
            continue;
        }
        iuaddrs[blockaddr-image->firstdatablock]++;
    }
}

//rule 7, 8
//rule group to check block address
//rule 7: For in-use inodes, each direct address in use is only used once.
//rule 8: For in-use inodes, each indirect address in use is only used once.
void blockaddrs_check(image_t *image){
    struct dinode *inode;
    int i;
    //used direct address count.
    uint duaddrs[image->sb->nblocks];
    memset(duaddrs, 0, sizeof(uint)* image->sb->nblocks);

    //used indirect address count.
    uint iuaddrs[image->sb->nblocks];
    memset(iuaddrs, 0, sizeof(uint)* image->sb->nblocks);

    inode = (struct dinode*)(image->inodeblocks);
    
    for(i=0; i<image->sb->ninodes; i++, inode++) {
        if (inode->type==0){
            continue;
        }
        //count used direct address.
        fill_duaddrs(image, inode, duaddrs);
        //count used indirect address.
        fill_iuaddrs(image, inode, iuaddrs);
    }
    
    //go through all data blocks. check if any block has been used more than once.
    for (i=0; i<image->sb->nblocks; i++) {
        //rule 7
        if (duaddrs[i] > 1) {
            printf("ERROR: direct address used more than once.\n");
            exit(1);
        }

        //rule 8
        if (iuaddrs[i] > 1) {
            printf("ERROR: indirect address used more than once.\n");
            exit(1);
        }
    }
}


//function for rule 9, 10, 11, 12
//traverse all directories and count for inodemap (how many times each inode number has been refered by directory).
void traverse_dirs(image_t *image, struct dinode *rootinode, int *inodemap){
    int i, j;
    uint blockaddr;
    uint *indirect;
    struct dinode *inode;
    struct dirent *dir;
    
    if(rootinode->type==T_DIR){
        //traverse direct address
        for(i=0; i<NDIRECT; i++) {
            blockaddr=rootinode->addrs[i];
            if(blockaddr==0){
                continue;
            }

            dir=(struct dirent *)(image->mmapimage+blockaddr*BLOCK_SIZE);
            for(j=0; j<DPB; j++, dir++) {
                if(dir->inum!=0 && strcmp(dir->name, ".")!=0 && strcmp(dir->name, "..")!=0){
                    inode=((struct dinode *)(image->inodeblocks))+dir->inum;
                    inodemap[dir->inum]++;
                    //recursion.
                    traverse_dirs(image, inode, inodemap);
                }
            }
        }

        //traverse indirect address
        blockaddr=rootinode->addrs[NDIRECT];
        if(blockaddr!=0){
            indirect=(uint *)(image->mmapimage+blockaddr*BLOCK_SIZE);
            for(i=0; i<NINDIRECT; i++, indirect++){
                blockaddr=*(indirect);
                if(blockaddr==0){
                    continue;
                }

                dir=(struct dirent *)(image->mmapimage+blockaddr*BLOCK_SIZE);

                for(j=0; j<DPB; j++, dir++){
                    if(dir->inum!=0 && strcmp(dir->name, ".")!=0 && strcmp(dir->name, "..")!=0){
                        inode=((struct dinode *)(image->inodeblocks))+dir->inum;
                        inodemap[dir->inum]++;
                        //recursion.
                        traverse_dirs(image, inode, inodemap);
                    }
                }
            }
        }
    }
}


//rule 9, 10, 11, 12
//rule group to check directory
//rule 9: For all inodes marked in use, each must be referred to in at least one directory.
//rule 10: For each inode number that is referred to in a valid directory, it is actually marked in use.
//rule 11: Reference counts (number of links) for regular files match the number of times file is referred to in directories.
//rule 12: No extra links allowed for directories (each directory only appears in one other directory).
void directory_check(image_t *image){
    int i;
    int inodemap[image->sb->ninodes];
    memset(inodemap, 0, sizeof(int)* image->sb->ninodes);
    struct dinode *inode, *rootinode;

    inode=(struct dinode *)(image->inodeblocks);
    rootinode=++inode;
    
    inodemap[0]++;
    inodemap[1]++;
    
    //traverse all directories. and count how many times each inode number has been refered by directory
    traverse_dirs(image, rootinode, inodemap);
    
    inode++;
    //go through all inodes to check rule 9-12.
    for(i=2; i<image->sb->ninodes; i++, inode++) {
        //rule 9
        if(inode->type!=0 && inodemap[i]==0){
            printf("ERROR: inode marked use but not found in a directory.\n");
            exit(1);
        }

        //rule 10
        if(inodemap[i]>0 && inode->type==0){
            printf("ERROR: inode referred to in directory but marked free.\n");
            exit(1);
        }

        //rule 11
        //reference count check for all files.
        if(inode->type==T_FILE && inode->nlink!=inodemap[i]){
            printf("ERROR: bad reference count for file.\n");
            exit(1);
        }
    
        //rule 12
        if(inode->type==T_DIR && inodemap[i]>1){
            printf("ERROR: directory appears more than once in file system.\n");
            exit(1);
        }
    }
}
