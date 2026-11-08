#ifndef XV6_FS_H
#define XV6_FS_H
#include <stdint.h>
#define BSIZE 512
#define XV6_MAGIC 0x10203040
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint32_t))
#define NDINDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIRECT)
#define ROOTINO 1
struct superblock {
    uint32_t magic;      
    uint32_t size;       
    uint32_t nblocks;    
    uint32_t ninodes;    
    uint32_t nlog;       
    uint32_t logstart;   
    uint32_t inodestart; 
    uint32_t bmapstart;  
};
struct dinode {
    int16_t type;               
    int16_t major;              
    int16_t minor;              
    int16_t nlink;              
    uint32_t size;              
    uint32_t addrs[NDIRECT+2];  
};
#define IPB (BSIZE / sizeof(struct dinode))
#define IBLOCK(i, sb) ((i) / IPB + (sb).inodestart)
#define BPB (BSIZE * 8)
#define BBLOCK(b, sb) ((b) / BPB + (sb).bmapstart)
#define DIRSIZ 14
struct dirent {
    uint16_t inum;
    char name[DIRSIZ];
};
#endif
