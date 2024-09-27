#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"
#include "net.h"
//int is_mounted = 0;
//int is_written = 0;

//Represents if drives are mounted (1) or not (0). Write permisison store whether there are permisionns
static short isMounted = 0, permissions = 0; 

//this function will create the operation used in each command and takes care off offsetting
uint32_t createOp(uint8_t cmd, uint8_t diskID, uint16_t blockID){
  //Creates op and initializes it to 32 0s
  uint32_t op=0x00000000;

  //Sets the diskID, BlockID, and command
  op = (cmd <<12)| (blockID<<4) | diskID ;

  //Returns op
  return op;
}
//seeks to the current block and disk
void toBlockDisk(uint8_t currentDisk, uint16_t currentBlock){
	//Seeks to disk
	uint32_t op = createOp(JBOD_SEEK_TO_DISK, currentDisk, 0);
	jbod_client_operation(op, NULL);
	//Seeks to block
	op = createOp(JBOD_SEEK_TO_BLOCK, currentDisk, currentBlock);
	jbod_client_operation(op, NULL);
	
}

//sets up the block and disk when going across blocks
void setupBlockDisk(uint16_t currentBlock, uint8_t currentDisk){
	//cmdis the command, op is being manually set to avoid return errors from createOp
	uint8_t cmd = JBOD_SEEK_TO_DISK;
	uint32_t op= (cmd << 12) | (currentDisk);
	jbod_client_operation(op, NULL);

	//seeks to block
	cmd = JBOD_SEEK_TO_BLOCK;
	op = (cmd << 12) | (currentBlock << 4) | (currentDisk);
	jbod_client_operation(op, NULL);
}
int mdadm_mount(void) {
  
	// YOUR CODE GOES HERE
	//Mounts the disks using op. Returns -1 if already mounted, 1 if not already
  	uint32_t op =createOp(JBOD_MOUNT, 0,0);
 	if (isMounted == 1 || jbod_client_operation(op, NULL) ==-1){
    	return -1;
  	}

  	//Sets isMounted to 1 to signify it is mounted. Returns 1 for successful op
  	isMounted = 1;
	return 1;
	
}

int mdadm_unmount(void) {
  
	// YOUR CODE GOES HERE

	//Inverse of mount
	uint32_t op =createOp(JBOD_UNMOUNT, 0,0);
	if(isMounted ==0 || jbod_client_operation(op, NULL) ==-1){
		return -1;
	}

	//sets isMounted to 0, returns 1 for a successful operation
	isMounted = 0;
	return 1;
}


int mdadm_write_permission(void){
 
	// YOUR CODE GOES HERE
	//returns -1 if unmounted, already has write permissions, or write permission opertation failure. return 0 on success and adds permission
	uint32_t op = createOp(JBOD_WRITE_PERMISSION, 0,0);
	if(isMounted == 0 || permissions ==1|| jbod_client_operation(op, NULL) ==-1){
		return -1;
	}
	permissions =1;
	return 0;

}


int mdadm_revoke_write_permission(void){

	// YOUR CODE GOES HERE
	//Return -1 if failed, already revoked, or unmounted, return 0 on success and revokes permission
	int32_t op = createOp(JBOD_REVOKE_WRITE_PERMISSION, 0,0);
	if(isMounted ==0 || permissions ==0 || jbod_client_operation(op, NULL) ==-1){
		return -1;
	}
	permissions=0;
	return 0;
}


int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)  {

	// YOUR CODE GOES HERE
  	//Returns -1 if the drives are unmounted
	if(isMounted == 0){
		return -1;
	}
	
	//Returns -1 if the read from out-of-bounds, read_len is larger than 1024 bytes, or a read goes out-of-bounds
	if (read_len > 1024 || start_addr + read_len > 1048576){
		return -1;
	}

	//Fails a 0-length read with a NULL pointer
	if (read_len >0 && read_buf == NULL){
		return -1;
	}
	//Passes read_len = 0 and read_buf = NULL. We already accounted for >0
	if (read_buf == NULL){
		return 0;
	}
	
	//Finds what disk and block start_addr is at. 
	//Offset tracts the offset from the starting point of a block. bytleLocation is the byte we start at 
	
	uint8_t currentDisk = start_addr / (JBOD_DISK_SIZE);
	uint16_t currentBlock =  (start_addr % (JBOD_DISK_SIZE)) / JBOD_BLOCK_SIZE;
	uint32_t byteLocation= (start_addr % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE, offset =0;
	
	//Makes a pointer buffer and a cache buffer
	uint8_t pb[JBOD_BLOCK_SIZE];
	uint8_t cacheBuf[JBOD_BLOCK_SIZE];

	//Seeks to the specific disk and block
	toBlockDisk(currentDisk,currentBlock);
	
  
	//reads the bytes into the read_buf
  	while (read_len > 0) {
		//Determines if the number of bytes left is the rest of the read_len or standard amount
		uint32_t bytesLeft = (read_len < (JBOD_BLOCK_SIZE - byteLocation)) ? read_len : (JBOD_BLOCK_SIZE - byteLocation);		

		if(cache_enabled() && cache_lookup(currentDisk, currentBlock, cacheBuf) ==1){
			memcpy(read_buf + offset, cacheBuf + byteLocation, bytesLeft);
		}else{//not in cache, then read from storage
			//Creates the operation and reads the block into buffer 
			uint32_t op = createOp(JBOD_READ_BLOCK, currentDisk, currentBlock);
			//Returns -1 if operation fails
			if (jbod_client_operation(op, pb) == -1){
				return -1;
			}
			//Insert into cache
			cache_insert(currentDisk, currentBlock, pb);

			//Uses memcpy to copy the value from the storage to read_buf. Accounts for the offset point and current byte
			memcpy(read_buf + offset, pb + byteLocation, bytesLeft);
		}
		offset += bytesLeft;
		byteLocation = 0;
		currentBlock++;
		read_len -= bytesLeft;
		setupBlockDisk(currentBlock, currentDisk);

		//reinistializes the disk when going into a new disk
		if(currentBlock == JBOD_NUM_BLOCKS_PER_DISK) {
			currentBlock= 0;
			currentDisk++;
			//reinitializes the disk and block
			setupBlockDisk(currentBlock, currentDisk);
		}
	}

  return offset;
}


int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf) {
	

	// YOUR CODE GOES HERE
	//Returns -1 if the drives are unmounted or there are no write permissions
	if(isMounted == 0 || permissions ==0) {
		return -1;
	}
	
	//Returns -1 if the write goes out-of-bounds or write_len is larger than 1024 bytes, 
	if (write_len > 1024 || start_addr + write_len > 1048576){
		return -1;
	}
	//Fails non 0-length write with a NULL pointer
	if (write_len >0 && write_buf == NULL){
		return -1;
	}
	if(write_buf == NULL &&  write_len==0){
		return write_len;
	}
	//Returns 0 when write_len = 0 and read_buf = NULL. We already accounted for >0
	if (write_buf == NULL){
		return 0;
	}
	

	//Finds what disk and block start_addr is at. 
	//Offset tracts the offset from the starting point of a block. bytleLocation is the byte we start at
	uint8_t currentDisk = start_addr / (JBOD_DISK_SIZE);
	uint16_t currentBlock =  (start_addr % (JBOD_DISK_SIZE)) / JBOD_BLOCK_SIZE;
	uint32_t offset =0, byteLocation= (start_addr % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE; 
	
	//Makes a pointer buffer
	uint8_t pb[JBOD_BLOCK_SIZE];

	//Seeks to the specific disk. Returns -1 if a failure occurs
	toBlockDisk(currentDisk, currentBlock);
	//printf("\n before while loop \n");
	//Loops through and writes for the length
	while(write_len>0){
		//Determines if the number of bytes left is the rest of the write_len or standard amount
		uint32_t bytesLeft = (write_len < JBOD_BLOCK_SIZE - byteLocation) ? write_len : (JBOD_BLOCK_SIZE - byteLocation);
		
		//find start address of current block, reads it into buffer, and reverts back to the block and disk
		uint32_t start_ad = currentDisk * JBOD_DISK_SIZE + currentBlock *JBOD_BLOCK_SIZE ;
		//printf("\n before read in write func \n");
		mdadm_read(start_ad, JBOD_BLOCK_SIZE, pb);
		toBlockDisk(currentDisk, currentBlock);

		//Uses memcpy to copy the value from the write_buf to storage. Accounts for the offset point and current byte
		memcpy(pb + byteLocation, write_buf + offset, bytesLeft);

		//Writes to memory using JBOD
		uint32_t op = createOp(JBOD_WRITE_BLOCK, currentDisk, currentBlock);
		//Returns -1 if operation fails
		if (jbod_client_operation(op, pb) == -1){
			return -1;
		}

		//Writes to cache
		cache_update(currentDisk, currentBlock, pb);
		
		//Adjusts the offset and read_len
		offset += bytesLeft;
		write_len -= bytesLeft;
		
		//Moves towards next block. Resets byteLocation to 0
		currentBlock++;
		byteLocation = 0;

		//Determines whether or not to move to the next disk. Increments currentBlock 
		if(currentBlock == JBOD_NUM_BLOCKS_PER_DISK){
			currentDisk++;
			currentBlock = 0;

			toBlockDisk(currentDisk, currentBlock);
		}
	}

	//Return the write_len
	return offset;
}