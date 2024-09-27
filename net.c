#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n bytes from fd; returns true on success and false on
 * failure */
bool nread(int fd, int len, uint8_t *buf) {
  //stores the number of bytes read in
  int bytesReadIn = 0;
  
  //reads in the data and increments the bytesReadIn
  while(bytesReadIn < len){
    //Holds the result, used for return
    int temp = read(fd, buf + bytesReadIn, len - bytesReadIn);
    if(temp <=0){
      return false;
    }
    bytesReadIn += temp;
  }
  //returns true when all is read in
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on
 * failure */
bool nwrite(int fd, int len, uint8_t *buf) {
  //Holds the number of bytes wrriten into buffer
  int bytesWritten = 0;

  //writes all data to buffer 
  while(bytesWritten< len){
    int temp = write(fd, buf + bytesWritten, len - bytesWritten);
    if(temp <=0){
      return false;
    }
    bytesWritten += temp;
    
  }
  return true;
}

/* attempts to receive a packet from fd; returns true on success and false on
 * failure */
bool recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block) {  
  /*
  //makes a buffer that will be used for the packer
  uint8_t packet[HEADER_LEN + JBOD_BLOCK_SIZE];

  if(!nread(fd, sizeof(uint32_t) + sizeof(uint8_t), packet)){
    perror("Cannor read into packet header"); 
    return false;
  }
  //Sets the info code to the 5th byte
  memcpy(op, packet, sizeof(uint32_t));
  *op=ntohl(*op); //Makes it in network big endian

  memcpy(ret, packet + sizeof(uint32_t), sizeof(uint8_t)); 
  *ret = ntohs(*ret); //Network format

  //Determines if the payload exists by checking the 2nd to last bit
  if(*ret & 0x02){
    if(!nread(fd, JBOD_BLOCK_SIZE, block)){
      return false;
    }
  }*/
  //Makes a packet buffer
  uint8_t packet[HEADER_LEN];
  
  //Reads in the header
  if(!nread(fd, HEADER_LEN, packet)){
    return false;
  }

  //Gets op
  memcpy(op, packet, sizeof(*op));
  *op = ntohl(*op);
  
  //Gets the return code
  memcpy(ret, packet + sizeof(*op), sizeof(*ret));
  
  //Reads in the payload if it exists
  if(*ret & 0x02){
    if(!nread(fd, JBOD_BLOCK_SIZE, block))
    return false;
  }
  //Return true if complete
  return true;

}
/* attempts to send a packet to sd; returns true on success and false on
 * failure */
bool send_packet(int sd, uint32_t op, uint8_t *block) {
  /*
  //Determines if a payload exists, then set the info code to the appropriate value
  uint8_t ret =  (block != NULL) ? 0x02 : 0x00;

  //makes a buffer to hold the packet
  uint8_t packet[HEADER_LEN + JBOD_BLOCK_SIZE];

  //Copies the data into the buffer
  op |= (JBOD_WRITE_BLOCK << 12);
  op = htonl(op);
  memcpy(packet, &op, sizeof(uint32_t));
  //ret = htons(ret);
  memcpy(packet + sizeof(uint32_t), &ret, sizeof(uint8_t));

  //Writes the header to the buffer
  if(!nwrite(sd, sizeof(uint32_t) + sizeof(uint8_t), packet)){
      perror("Cannot write header within send_packet");
      return false;
  }

  //Checks if a payload should be written as well
  if(ret & 0x02){
    if(!nwrite(sd, JBOD_BLOCK_SIZE, block)){
      perror("Cannot write block within send_packet");
      return false;
    }
  } */

  //Makes a buffer to hold the packet
  uint8_t packet[HEADER_LEN + JBOD_BLOCK_SIZE];
  //holds the return code
  uint8_t ret=0; 

  //Sees if the command is JBOD_WRITE, set bit to 1
  if(((op >>12) & 0x3f) == JBOD_WRITE_BLOCK){
    ret =2;
  }

  //Sets the command part of op
  op = htonl(op);
  memcpy(packet, &op, sizeof(op));
  
  //Checks if a payload exists
  if(ret ==2){
    //Copies the return code into packet
    memcpy(packet + sizeof(uint32_t), &ret, sizeof(uint8_t)); 
    //copies the block into packet
    memcpy(packet + HEADER_LEN, block, JBOD_BLOCK_SIZE); 
    //Write functions
    if(!nwrite(sd, HEADER_LEN + JBOD_BLOCK_SIZE, packet)){
      return false;
    }
  }else{//No payload
    memcpy(packet + sizeof(uint32_t), &ret, sizeof(uint8_t)); //No return code
    if(!nwrite(sd, HEADER_LEN, packet)){
      return false;
    }
  } 

  return true;

}

/* connect to server and set the global client variable to the socket */
bool jbod_connect(const char *ip, uint16_t port) {
  struct sockaddr_in caddr;

  //Creates socket and test if it fails
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (cli_sd == -1) {
      perror("Error creating socket. JBOD_Connect");
      return false;
  }

  //Makes server address
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  if (inet_aton(ip, &caddr.sin_addr) ==0) {
      perror("Error converting IP address. JBOD_Connect");
      //close(cli_sd);
      return false;
  }
 /* printf("cli_sd: %d\n", cli_sd);
  printf("caddr size: %lu\n", sizeof(caddr));
  printf("Error code: %d\n", errno); */
  //Connects to server
  if (connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) ==-1) {
      perror("Error connecting to server. JBOD_Connect");
      //close(cli_sd);
      return false;
  }

  return true;
}

void jbod_disconnect(void) {
  //Checks if it already disconnected and does so if not
  if(cli_sd!= -1){
    close(cli_sd);
    cli_sd =-1;
  }

}

int jbod_client_operation(uint32_t op, uint8_t *block) {
  uint8_t ret;
  if(cli_sd ==-1){//fails if socket is closed
    return -1;
  }

  //Sends packet 
  if(send_packet(cli_sd, op, block)){
    if(recv_packet(cli_sd, &op, &ret, block))
      {return 0;}
  }
  return -1;
}
