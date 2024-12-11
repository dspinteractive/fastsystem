

#define ATA_BLOCKSIZE 512

#define ATA_IRQ0	32+14
#define ATA_IRQ1	32+15
#define ATA_IRQ2	32+11
#define ATA_IRQ3	32+9

#define ATA_BASE0	0x1F0
#define ATA_BASE1	0x170
#define ATA_BASE2	0x1E8
#define ATA_BASE3	0x168

#define ATA_TIMEOUT     3

#define ATA_DATA	0   /* data register */
#define ATA_ERROR	1   /* error register */
#define ATA_COUNT	2   /* sectors to transfer */
#define ATA_SECTOR	3   /* sector number */
#define ATA_CYL_LO	4   /* low byte of cylinder number */
#define ATA_CYL_HI	5   /* high byte of cylinder number */
#define ATA_FDH		6   /* flags, drive and head */
#define ATA_STATUS	7
#define ATA_COMMAND	7
#define ATA_CONTROL	0x206

#define ATA_FLAGS_ECC	0x80	/* enable error correction */
#define ATA_FLAGS_LBA	0x40	/* enable linear addressing */
#define ATA_FLAGS_SEC	0x20	/* enable 512-byte sectors */
#define ATA_FLAGS_SLV	0x10	/* address the slave drive */

#define ATA_STATUS_BSY	0x80    /* controller busy */
#define ATA_STATUS_RDY	0x40    /* drive ready */
#define ATA_STATUS_WF	0x20    /* write fault */
#define ATA_STATUS_SC	0x10    /* seek complete (obsolete) */
#define ATA_STATUS_DRQ	0x08    /* data transfer request */
#define ATA_STATUS_CRD	0x04    /* corrected data */
#define ATA_STATUS_IDX	0x02    /* index pulse */
#define ATA_STATUS_ERR	0x01    /* error */

#define ATA_COMMAND_IDLE		0x00
#define ATA_COMMAND_READ		0x20    /* read data */
#define ATA_COMMAND_WRITE		0x30    /* write data */
#define ATA_COMMAND_IDENTIFY		0xec

#define ATAPI_COMMAND_IDENTIFY 0xa1
#define ATAPI_COMMAND_PACKET   0xa0

#define ATAPI_FEATURE	1
#define ATAPI_IRR 2
#define ATAPI_SAMTAG 3
#define ATAPI_COUNT_LO 4
#define ATAPI_COUNT_HI 5
#define ATAPI_DRIVE 6
#define ATAPI_BLOCKSIZE 2048

#define SCSI_READ10            0x28
#define SCSI_SENSE             0x03

#define ATA_CONTROL_RESET	0x04
#define ATA_CONTROL_DISABLEINT	0x02

static const int ata_base[4] = {
	ATA_BASE0,
	ATA_BASE0,
	ATA_BASE1,
	ATA_BASE1
};

struct ata_drive_info
{
	unsigned char  attached;
	const char *name;
	int block_size;
	unsigned long long size;
};

static struct ata_drive_info drives[4];

void ata_reset(int id) {
	outb(ata_base[id] + ATA_CONTROL, ATA_CONTROL_RESET);
	msleep(10);
	outb(ata_base[id] + ATA_CONTROL, 0);
	msleep(10);
}

static int ata_wait(int id, int mask, int state) {
	int t;
	int time=0;
	while(1) {
		t = inb(ata_base[id] + ATA_STATUS);
		if((t&mask) == state) {
			return 1;
		}
		if(t&ATA_STATUS_ERR) {
			ata_reset(id);
			return 0;
		}
		if(time>=ATA_TIMEOUT*100)
		{
			ata_reset(id);
			return 0;
		}
		msleep(1);
		time++;
	}
}

static void ata_pio_read(int id, void *buffer, int size) {
	unsigned short *wbuffer = (unsigned short*)buffer;
	while(size > 0) {
		*wbuffer = inw(ata_base[id] + ATA_DATA);
		wbuffer++;
		size -= 2;
	}
}

static void ata_pio_write(int id, const void *buffer, int size) {
	unsigned short *wbuffer = (unsigned short*)buffer;
	while(size > 0) {
		outw(ata_base[id] + ATA_DATA, *wbuffer);
		wbuffer++;
		size-=2;
	}
}

static int ata_begin(int id, int command, int nblocks, int offset) {
	int base = ata_base[id];
	int sector, clow, chigh, flags;

	flags = ATA_FLAGS_ECC | ATA_FLAGS_LBA | ATA_FLAGS_SEC;
	if(id % 2) flags |= ATA_FLAGS_SLV;

	sector = (offset >> 0) & 0xff;
	clow = (offset >> 8) & 0xff;
	chigh = (offset >> 16) & 0xff;
	flags |= (offset >> 24) & 0x0f;

	if(!ata_wait(id, ATA_STATUS_BSY, 0)) return 0;
	outb(base + ATA_FDH, flags);
	int ready;
	if(command == ATAPI_COMMAND_IDENTIFY) ready = ata_wait(id, ATA_STATUS_BSY,0);
	else ready = ata_wait(id, ATA_STATUS_BSY | ATA_STATUS_RDY, ATA_STATUS_RDY);

	if(!ready) return 0;

	outb(base + ATA_CONTROL, 0);
	outb(base + ATA_COUNT, nblocks);
	outb(base + ATA_SECTOR, sector);
	outb(base + ATA_CYL_LO, clow);
	outb(base + ATA_CYL_HI, chigh);
	outb(base + ATA_FDH, flags);
	outb(base + ATA_COMMAND, command);

	return 1;
}

static int ata_read_unlocked(int id, void *buffer, int nblocks, int offset) {
	int i;
	if(!ata_begin(id,ATA_COMMAND_READ,nblocks,offset)) return 0;
	for(i = 0;i < nblocks; i++) {
		if(!ata_wait(id, ATA_STATUS_DRQ, ATA_STATUS_DRQ)) return 0;
		ata_pio_read(id,buffer, ATA_BLOCKSIZE);
		buffer = ((char*)buffer) + ATA_BLOCKSIZE;
		offset++;
	}
	if(!ata_wait(id, ATA_STATUS_BSY, 0)) return 0;
	return nblocks;
}


int ata_read(int id, void *buffer, int nblocks, int offset) {
	int result;
	result = ata_read_unlocked(id, buffer, nblocks, offset);
	return result;
}

int ata_read_bytes(int id, void *buffer, int nbytes, int offset)
{
	int nblocks=nbytes/drives[id].block_size;
	if(nbytes % drives[id].block_size)
		nblocks++;
	int result = ata_read(id, buffer, nblocks, offset / drives[id].block_size);
	return result;
}

static int atapi_begin(int id, void *data, int length) {
	int base = ata_base[id];
	int flags;

	flags = ATA_FLAGS_ECC | ATA_FLAGS_LBA | ATA_FLAGS_SEC;
	if(id%2) flags |= ATA_FLAGS_SLV;
	if(!ata_wait(id, ATA_STATUS_BSY,0)) return 0;
	outb(base+ATA_FDH, flags);

	if(!ata_wait(id, ATA_STATUS_BSY,0)) return 0;

	outb(base+ ATAPI_FEATURE, 0);
	outb(base+ ATAPI_IRR, 0);
	outb(base+ ATAPI_SAMTAG, 0);
	outb(base + ATAPI_COUNT_LO, length & 0xff);
	outb(base + ATAPI_COUNT_HI, length >> 8);
	outb(base + ATA_COMMAND, ATAPI_COMMAND_PACKET);

	if(!ata_wait(id, ATA_STATUS_BSY | ATA_STATUS_DRQ, ATA_STATUS_DRQ)) {}
	ata_pio_write(id, data, length);

	return 1;
}

static int atapi_read_unlocked(int id, void *buffer, int nblocks, int offset) {
	unsigned char packet[12];
	int length = sizeof(packet);
	int i;

	packet[0] = SCSI_READ10;
	packet[1] = 0;
	packet[2] = offset >>24;
	packet[3] = offset >>16;
	packet[4] = offset >>8;
	packet[5] = offset >>0;
	packet[6] = 0;
	packet[7] = nblocks >>8;
	packet[8] = nblocks >>0;
	packet[9] = 0;
	packet[10] = 0;
	packet[11] = 0;

	if(!atapi_begin(id,packet,length)) return 0;

	for(i = 0;i < nblocks; i++) {
		if(!ata_wait(id, ATA_STATUS_DRQ, ATA_STATUS_DRQ)) return 0;
		ata_pio_read(id, buffer, ATAPI_BLOCKSIZE);
		buffer = ((char*)buffer) + ATAPI_BLOCKSIZE;
		offset++;
	}

	return nblocks;
}

int atapi_read(int id, void *buffer, int nblocks, int offset) {

	int result;
	result = atapi_read_unlocked(id, buffer, nblocks, offset);
	return result;
}

static int ata_write_unlocked(int id, const void *buffer, int nblocks, int offset) {
	int i;
	if(!ata_begin(id, ATA_COMMAND_WRITE, nblocks, offset)) return 0;

	for(i = 0; i < nblocks; i++) {
		if(!ata_wait(id, ATA_STATUS_DRQ, ATA_STATUS_DRQ)) return 0;
		ata_pio_write(id, buffer, ATA_BLOCKSIZE);
		buffer = ((char*)buffer) + ATA_BLOCKSIZE;
		offset++;
	}
	if(!ata_wait(id, ATA_STATUS_BSY, 0)) return 0;
	return nblocks;
}

int ata_write(int id, void *buffer, int nblocks, int offset) {
	int result;
	result = ata_write_unlocked(id, buffer, nblocks, offset);
	return result;
}
int ata_write_bytes(int id, void *buffer, int nbytes, int offset)
{
	return ata_write(id, buffer, nbytes*drives[id].block_size, offset);
}


static int ata_identify(int id, int command, void *buffer) {
	if(!ata_begin(id, command, 0, 0)) return 0;
	if(!ata_wait(id, ATA_STATUS_DRQ, ATA_STATUS_DRQ)) return 0;
	ata_pio_read(id, buffer, 512);
	return 1;
}

int get_ata_probe_name( int id, int *nblocks, int *blocksize, char *name )
{
	unsigned short buffer[256];
	char *cbuffer = (char*)buffer;

	unsigned char t = inb(ata_base[id] + ATA_STATUS);
	if(t == 0xff) {
		drives[id].name="Not Attached";
		drives[id].block_size=0;
		drives[id].size=0;
		drives[id].attached=0;
		return 0;
	}
	ata_reset(id);
	memset(cbuffer,0,512);
	if(ata_identify(id, ATA_COMMAND_IDENTIFY, cbuffer)) {
		*nblocks = buffer[1] * buffer[3] * buffer[6];
		*blocksize = 512;
 	} else if(ata_identify(id, ATAPI_COMMAND_IDENTIFY, cbuffer)) {
		*nblocks = 337920;
		*blocksize = 2048;
	} else {
		drives[id].name="Failed";
		drives[id].block_size=0;
		drives[id].size=0;
		drives[id].attached=0;
		return 0;
	}

	unsigned long i;
	for(i = 0; i < 512; i += 2) {
		t = cbuffer[i];
		cbuffer[i] = cbuffer[i + 1];
		cbuffer[i + 1] = t;
	}

	cbuffer[256]=0;
	strcpy(name, &cbuffer[54]);
	name[40] = 0;
	drives[id].name=name;
	drives[id].block_size=*blocksize;
	drives[id].size=*nblocks * *blocksize;
	return 1;
}


int ata_block_count;
int ata_block_sizes[4];
char ata_disk_name[40][4];

void detectide(void)
{

	// Primary IDE Master
	print("Primary IDE Master: ");
	if (get_ata_probe_name(0, &ata_block_count, &ata_block_sizes[0], ata_disk_name[0]))
	{
		puts(ata_disk_name[0]);
	}
	else
	{
		puts("None");
	}

	// Primary IDE Slave
	print("Primary IDE Slave: ");
	if (get_ata_probe_name(1, &ata_block_count, &ata_block_sizes[1], ata_disk_name[1]))
	{
		puts(ata_disk_name[1]);
	}
	else
	{
		puts("None");
	}

	// Secondary IDE Master
	print("Secondary IDE Master: ");
	if (get_ata_probe_name(2, &ata_block_count, &ata_block_sizes[2], ata_disk_name[2]))
	{
		puts(ata_disk_name[2]);
	}
	else
	{
		puts("None");
	}

	// Secondary IDE Slave
	print("Secondary IDE Slave: ");
	if (get_ata_probe_name(3, &ata_block_count, &ata_block_sizes[3], ata_disk_name[3]))
	{
		puts(ata_disk_name[3]);
	}
	else
	{
		puts("None");
	}

}


