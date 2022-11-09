#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"

int is_mounted = 0;
int is_written = 0;
int is_permission = 0;

uint32_t opcode_byte_generator(uint32_t block_id_val, uint32_t disk_id_val, uint32_t command_val, uint32_t reserved_val)
{
	// Set up a return value and temp values to hold each byte operations
	uint32_t return_val = 0x0;
	uint32_t tempa, tempb, tempc, tempd;
	tempa = block_id_val & 0xff;
	tempb = (disk_id_val & 0xff) << 8;
	tempc = (command_val & 0xff) << 12;
	tempd = (reserved_val & 0xff) << 18;
	return_val = tempa | tempb | tempc | tempd;
	return return_val;
}

int mdadm_mount(void)
{
	// Creating opcode for mount with shifting
	uint32_t opcode = opcode_byte_generator(0, 0, JBOD_MOUNT, 0);
	// if the jbod_operation result is successful(returns 0) then return 1. Else return -1
	// Mount only if it is unmounted
	if (is_mounted == 0)
	{
		jbod_operation(opcode, 0);
		is_mounted = 1;
		return 1;
	}
	else
	{
		return -1;
	}
}

int mdadm_unmount(void)
{
	// Creating opcode for unmount with shifting
	uint32_t opcode = opcode_byte_generator(0, 0, JBOD_UNMOUNT, 0);

	// if the jbod_operation result is successful(returns 0) then return 1. Else return -1
	// Unmount only if it is mounted
	if (is_mounted == 1)
	{
		jbod_operation(opcode, 0);
		is_mounted = 0;
		return 1;
	}
	else
	{
		return -1;
	}
}

int mdadm_write_permission(void)
{
	// If disk is unmounted, refuse write_permission
	uint32_t opcode = opcode_byte_generator(0, 0, JBOD_WRITE_PERMISSION, 0);
	int result = jbod_operation(opcode, 0);
	is_permission = result;
	return result; // 0 on success, -1 on failure
}

int mdadm_revoke_write_permission(void)
{

	uint32_t opcode = opcode_byte_generator(0, 0, JBOD_REVOKE_WRITE_PERMISSION, 0);
	int result = jbod_operation(opcode, 0);
	is_permission = ~result;
	return result; // 0 on success, -1 on failure
}

int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)
{
	// If disk is unmounted then end the function
	if (is_mounted == 0)
	{
		return -1;
	}
	else
	{
		// EDGE CASES
		// if length to read is bigger than 2048
		if (read_len > 2048)
		{
			return -1;
		}
		// when passed a NULL pointer and non-zero length return -1
		if (read_buf == NULL && read_len != 0)
		{
			return -1;
		}
		// if the end address is bigger than maximum size return -1
		uint32_t temp_val = start_addr + read_len;
		if (temp_val > JBOD_NUM_DISKS * JBOD_DISK_SIZE)
		{
			return -1;
		}
		
		
		// Final address
		int final_address_whole = (start_addr + read_len) - 1;

		// DISK ID
		// Divide the address by disk size so that it gets disk ID
		int starting_disk_ID = start_addr / JBOD_DISK_SIZE;
		int final_disk_ID = (final_address_whole / JBOD_DISK_SIZE);

		// BLOCK ID
		// Consider disk is a one big array and calculate block number.
		int starting_block_ID_whole = start_addr / JBOD_NUM_BLOCKS_PER_DISK;
		int final_block_ID_whole = final_address_whole / JBOD_NUM_BLOCKS_PER_DISK;
		int starting_block_ID_index = (start_addr / JBOD_NUM_BLOCKS_PER_DISK) - (starting_disk_ID * JBOD_NUM_BLOCKS_PER_DISK);

		// Total number of disk that needs to be operated
		int num_of_disks_to_operate = (final_disk_ID - starting_disk_ID) + 1;
		// Total number of blocks that needs to be operated
		int num_of_blocks_to_operate = (final_block_ID_whole - starting_block_ID_whole) + 1;
		
		// opcode for reading block
		int READ_BLOCK_opcode = opcode_byte_generator(0, 0, JBOD_READ_BLOCK, 0);

		// Buffer to store data in a block
		uint8_t block_buffer[JBOD_BLOCK_SIZE];

		// variables needed to copy block buffer to read_buf
		int current_address_whole = start_addr;
		int current_block_ID = starting_block_ID_whole;
		int read_buf_index = 0;
		int current_block_bytes_read = 0;
		int total_bytes_read = 0;

		// Iterate over number of disk needed to operate
		for (int currentDiskID = starting_disk_ID; currentDiskID <= final_disk_ID; currentDiskID++)
		{
			uint32_t seekToDiskOpcode = opcode_byte_generator(0, currentDiskID, JBOD_SEEK_TO_DISK, 0);
			jbod_operation(seekToDiskOpcode, 0);
			// if it's first disk then set diskID and blockID
			if (currentDiskID == starting_disk_ID)
			{
				//printf("First disk operation");
				uint32_t seekToBlockOpcode = opcode_byte_generator(starting_block_ID_index, 0, JBOD_SEEK_TO_BLOCK, 0);
				jbod_operation(seekToBlockOpcode, 0);
			}
			// if it's not first disk then set new diskID and set blockID to 0 since it starts from 0.
			else
			{
				uint32_t seekToBlockOpcode = opcode_byte_generator(0, 0, JBOD_SEEK_TO_BLOCK, 0);
				jbod_operation(seekToBlockOpcode, 0);
			}

			// iterate over the number of blocks that needs to be operated
			for (int i = 0; i < (num_of_blocks_to_operate); i++)
			{
				// get the current block ID as if jbod is a big one array
				int current_block_ID_whole = (current_address_whole) / JBOD_NUM_BLOCKS_PER_DISK;
				// get the diskID which the current block belongs to
				int current_block_master_disk = current_block_ID_whole / JBOD_NUM_BLOCKS_PER_DISK;
				// If the DiskID of current block doesn't equal to current element go to next disk and perform seekDisk
				if (current_block_master_disk != currentDiskID && num_of_disks_to_operate != 1)
				{
					continue;
				}

				// if it's only block or last block to operate
				if (i == num_of_blocks_to_operate - 1)
				{
					// final - current gives us the bytes needed to be read
					current_block_bytes_read = (final_address_whole - current_address_whole) + 1;
				}
				else
				{
					// if it's no last block to operate that means blocks has to be read from current to 255 from each block
					// So calculate end_address in a block and subtract it from current address to get the whole byte size that should be read
					int end_address_in_block = ((current_block_ID + 1) * JBOD_BLOCK_SIZE) - 1;
					current_block_bytes_read = (end_address_in_block - current_address_whole) + 1;
				}
				
				// Look up cache first
				//printf("\n****************Cache start****************\n");
				int c_block_in_disk = (current_address_whole/256)-(currentDiskID*256);
				//printf("\nDisk id : (%d) | Block id : (%d)\n",currentDiskID,c_block_in_disk);
				int result = cache_lookup(currentDiskID, c_block_in_disk, block_buffer);
				//printf("\nresult : (%d)\n",result);
				//printf("\n****************Cache end****************\n");

				if (result == -1) {
					// Read an entire block and save it into block_buffer
					jbod_operation(READ_BLOCK_opcode, block_buffer);
				}
				
				// Copy values that needed to be copied into read_buf
				int block_buffer_index = current_address_whole - (current_block_ID_whole * JBOD_BLOCK_SIZE);
				memcpy(&read_buf[read_buf_index], &block_buffer[block_buffer_index], current_block_bytes_read);

				// Increment values that needed to be incremented
				current_address_whole += current_block_bytes_read;
				read_buf_index += current_block_bytes_read;
				current_block_ID += 1;
				total_bytes_read += current_block_bytes_read;
				if (num_of_disks_to_operate != 1)
				{
					num_of_blocks_to_operate -= 1;
				}
			}
		}
		return total_bytes_read;
	}
	return 0;
}

int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf)
{
	// If there is no write permission terminate the function
	if (is_permission == -1)
	{
		return -1;
	}
	// if length to read is biggers than 2048 terminate the function
	if (write_len > 2048)
	{
		return -1;
	}
	// when passed a NULL pointer and non-zero length terminate the function
	if (write_buf == NULL && write_len != 0)
	{
		return -1;
	}
	// if the end address is bigger than maximum size terminate the function
	uint32_t temp_val = start_addr + write_len;
	if (temp_val > JBOD_NUM_DISKS * JBOD_DISK_SIZE)
	{
		return -1;
	}
	// if the disk is not mounted terminate the function
	if (is_mounted == 0)
	{
		return -1;
	}
	// Don't write anything since there is nothing to write on the disk
	if (write_buf == NULL && write_len == 0)
	{
		return 0;
	}

	// Final address
	int final_address_whole = (start_addr + write_len) - 1;

	// DISK ID
	// Divide the address by disk size so that it gets disk ID
	int starting_disk_ID = start_addr / JBOD_DISK_SIZE;

	// BLOCK ID
	// Consider disk is a one big array and calculate block number.
	int starting_block_ID_whole = start_addr / JBOD_NUM_BLOCKS_PER_DISK;
	int final_block_ID_whole = final_address_whole / JBOD_NUM_BLOCKS_PER_DISK;
	int starting_block_ID_index = (start_addr / JBOD_NUM_BLOCKS_PER_DISK) - (starting_disk_ID * JBOD_NUM_BLOCKS_PER_DISK);

	// opcode for reading block
	int WRITE_BLOCK_opcode = opcode_byte_generator(0, 0, JBOD_WRITE_BLOCK, 0);
	int READ_BLOCK_opcode = opcode_byte_generator(0, 0, JBOD_READ_BLOCK, 0);

	// variables needed for write operation
	int current_address_whole = start_addr;
	int write_buf_index = 0;
	int current_block_bytes_written = 0;
	int total_bytes_written = 0;
	int current_block_within_disk;

	// Read buffer to read a block from a disk
	// To not overwrite on a block read from block and modify only the parts that needs to be modified
	uint8_t read_buf[256];
	

	// Seek first disk and fisrt block
	int current_disk_ID = starting_disk_ID;
	uint32_t seekToDiskOpcode = opcode_byte_generator(0, current_disk_ID, JBOD_SEEK_TO_DISK, 0);
	jbod_operation(seekToDiskOpcode, 0);
	uint32_t seekToBlockOpcode = opcode_byte_generator(starting_block_ID_index, 0, JBOD_SEEK_TO_BLOCK, 0);
	jbod_operation(seekToBlockOpcode, 0);


	// iterate over the number of blockfs that needs to be operated
	for (int current_block_ID_whole = starting_block_ID_whole; current_block_ID_whole <= final_block_ID_whole; current_block_ID_whole++)
	{
		// Find the disk that the block bleongs to
		int current_block_master_disk = current_block_ID_whole / JBOD_NUM_BLOCKS_PER_DISK;

		// If the disk doesn't match the block then insert new disk
		if (current_disk_ID != current_block_master_disk)
		{
			// Set up a new disk ID and new block
			current_disk_ID = current_block_master_disk;
			// Block ID within a disk starting from 0 to 255
			current_block_within_disk = current_block_ID_whole - (current_disk_ID * JBOD_NUM_BLOCKS_PER_DISK);
			uint32_t seekToDiskOpcode = opcode_byte_generator(0, current_block_master_disk, JBOD_SEEK_TO_DISK, 0);
			jbod_operation(seekToDiskOpcode, 0);
			// Since inserting new disk means it will start from 0th block, insert 0 block
			seekToBlockOpcode = opcode_byte_generator(current_block_within_disk, 0, JBOD_SEEK_TO_BLOCK, 0);
			jbod_operation(seekToBlockOpcode, 0);

			// From the new disk and block number read a block and restore block since read operation moves 1 block
			// Look up in the cache first and then if it doesn't exists, look up in jbod
			int result = cache_lookup(current_disk_ID, current_block_within_disk, read_buf);
			if (result == -1) {
				jbod_operation(READ_BLOCK_opcode, read_buf);
				jbod_operation(seekToBlockOpcode, 0);
			}
			
		} else {
			// Block ID within a disk starting from 0 to 255
			current_block_within_disk = current_block_ID_whole - (current_disk_ID * JBOD_NUM_BLOCKS_PER_DISK);

			// From the new block number read a block and restore block since read operation moves 1 block
			// Look up in the cache first and then if it doesn't exists, look up in jbod
			int result = cache_lookup(current_disk_ID, current_block_within_disk, read_buf);
			if (result == -1) {
				jbod_operation(READ_BLOCK_opcode, read_buf);
				seekToBlockOpcode = opcode_byte_generator(current_block_within_disk, 0, JBOD_SEEK_TO_BLOCK, 0);
				jbod_operation(seekToBlockOpcode, 0);
			}
			
		}
		
		// if it's only block or last block to operate
		if (current_block_ID_whole == final_block_ID_whole)
		{
			// store number of bytes that needs to be written
			current_block_bytes_written = (final_address_whole - current_address_whole) + 1;
		}
		else
		{
			// store number of bytes that needs to be written
			// if it's not last block to operate that means blocks has to be write from current to 255 from each block
			// So calculate end_address in a block and subtract it from current address to get the whole byte size that should be read
			int end_address_in_block = ((current_block_ID_whole + 1) * JBOD_BLOCK_SIZE) - 1;
			current_block_bytes_written = (end_address_in_block - current_address_whole) + 1;
		}
		
		// Find the current address so that it can format the read buffer(BLOCK information) to write it into a disk
		int current_address = (current_address_whole - (JBOD_DISK_SIZE * current_disk_ID)) - (current_block_within_disk * JBOD_BLOCK_SIZE);
		// Copy the information needed from write_buf into a read buffer
		memcpy(&read_buf[current_address], &write_buf[write_buf_index], current_block_bytes_written);
		// write read_buf into a disk
		jbod_operation(WRITE_BLOCK_opcode, read_buf);

		// Increment values that needed to be incremented
		current_address_whole += current_block_bytes_written;
		write_buf_index += current_block_bytes_written;
		total_bytes_written += current_block_bytes_written;
	}
	
	return total_bytes_written;
}
