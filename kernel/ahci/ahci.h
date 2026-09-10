#ifndef NOTUX_AHCI_H
#define NOTUX_AHCI_H
#include "../types.h"
typedef struct {
    uint32_t cap;       
    uint32_t ghc;        
    uint32_t is;          
    uint32_t pi;          
    uint32_t vs;         
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t  reserved[0xA0 - 0x2C];
    uint8_t  vendor[0x100 - 0xA0];
} __attribute__((packed)) hba_mem_t;
typedef struct {
    uint32_t clb, clbu;     
    uint32_t fb, fbu;        
    uint32_t is, ie;          
    uint32_t cmd;            
    uint32_t reserved0;
    uint32_t tfd;             
    uint32_t sig;             
    uint32_t ssts, sctl, serr, sact; 
    uint32_t ci;               
    uint32_t sntf, fbs;
    uint8_t  reserved1[0x70 - 0x44];
    uint8_t  vendor[0x80 - 0x70];
} __attribute__((packed)) hba_port_t;
#define GHC_AE   (1u << 31)
#define GHC_HR   (1u << 0)
#define PXCMD_ST    (1u << 0)
#define PXCMD_FRE   (1u << 4)
#define PXCMD_FR    (1u << 14)
#define PXCMD_CR    (1u << 15)
#define SSTS_DET_PRESENT 3
typedef struct {
    uint8_t  cfl:5;    
    uint8_t  a:1, w:1, p:1;
    uint8_t  r:1, b:1, c:1, rsv0:1, pmp:4;
    uint16_t prdtl;    
    uint32_t prdbc;    
    uint32_t ctba, ctbau; 
    uint32_t reserved[4];
} __attribute__((packed)) hba_cmd_header_t;
typedef struct {
    uint32_t dba, dbau;
    uint32_t reserved0;
    uint32_t dbc:22, reserved1:9, i:1;
} __attribute__((packed)) hba_prdt_entry_t;
#define AHCI_PRDT_ENTRIES 8
typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  reserved[48];
    hba_prdt_entry_t prdt[AHCI_PRDT_ENTRIES];
} __attribute__((packed)) hba_cmd_tbl_t;
#define FIS_TYPE_REG_H2D 0x27
typedef struct {
    uint8_t  fis_type;
    uint8_t  pmport:4, rsv0:3, c:1;
    uint8_t  command;
    uint8_t  featurel;
    uint8_t  lba0, lba1, lba2, device;
    uint8_t  lba3, lba4, lba5, featureh;
    uint16_t count;
    uint8_t  icc, control;
    uint8_t  reserved[4];
} __attribute__((packed)) fis_reg_h2d_t;
#define ATA_CMD_READ_DMA_EX  0x25
#define ATA_CMD_WRITE_DMA_EX 0x35
#define ATA_CMD_IDENTIFY     0xEC
typedef struct {
    hba_port_t *port;
    int         port_num;
    uint64_t    sectors;
    void       *clb_virt;   
    void       *fb_virt;    
    void       *ctba_virt[32];
} ahci_device_t;
int            ahci_init(void);
ahci_device_t *ahci_get_device(int index);
int            ahci_read_sectors(ahci_device_t *dev, uint64_t lba, uint32_t count, void *buf);
int            ahci_write_sectors(ahci_device_t *dev, uint64_t lba, uint32_t count, const void *buf);
uint64_t       ahci_device_sectors(ahci_device_t *dev);
#endif
