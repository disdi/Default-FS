#ifndef RAMDEVICE_H
#define RAMDEVICE_H

#define RB_SECTOR_SIZE 512
#define RB_DEVICE_SIZE 1024 /* sectors */
/* Array where the disk stores its data */
static u8 *dev_data;

extern int ramdevice_init(void);
extern void ramdevice_cleanup(void);
extern void ramdevice_write(sector_t sector_off, u8 *buffer, unsigned int sectors);
extern void ramdevice_read(sector_t sector_off, u8 *buffer, unsigned int sectors);
#endif
