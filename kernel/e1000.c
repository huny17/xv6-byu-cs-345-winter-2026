#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static char *tx_bufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static char *rx_bufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_bufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_bufs[i] = kalloc();
    if (!rx_bufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_bufs[i];
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

/* *****LIST OF FUNC TO WORK ON*****
  -e1000_transmit(char *buf, int len) //e1000.c 
  -e1000_recv(void) //e1000.c
  -sys_bind(void) //net.c
  -sys_unbind(void) //net.c (opional)
  -sys_recv(void) //net.c
  -ip_rx(char *buf, int len) //net.c
  -vmprint(pagetable_t pagetable) //vm.c
*/


//
// buf contains an ethernet frame; program it into
// the TX descriptor ring so that the e1000 sends it. Stash
// a pointer so that it can be freed after send completes.
//

// [E1000 3.3.3]
/*   
struct tx_desc     
{                  
  uint64 addr;    // Address of buffer 
  uint16 length;  // Length of data
  uint8 cso;      // Unused
  uint8 cmd;      // Command 3.3.3.1 
  uint8 status;   // Status 3.3.3.2 
  uint8 css;      // Unused
  uint16 special; // Unused 
}; 
*/

int
e1000_transmit(char *buf, int len) // Your code here.
{

//First sanity check buffer and length arguments. Return -1 if they look bad.

//First ask the E1000 for the TX ring index at which it's expecting the next packet, 
//by reading the E1000_TDT control register (regs[E1000_TDT]).  
uint index = regs[E1000_TDT];

//Sanity check the ring index returned. panic if it’s bad
if(index > (TX_RING_SIZE-1)){
  panic("e1000_transmit, bad index"); //Other things that could make the index invalid include pointing to a descriptor that is still owned by the hardware, referring to a slot that hasn’t been freed/processed yet, or being out of sync with the head/tail pointers that track which entries are safe to use.
}

//Then check if the the ring is overflowing. If E1000_TXD_STAT_DD is not set in the 
//descriptor indexed by E1000_TDT, the E1000 hasn't finished the corresponding 
//previous transmission request, so return an error.
    //status & E1000_TXD_STAT_DD 
if(tx_ring[index].status & E1000_TXD_STAT_DD == 0){
  return -1;
}


//Otherwise, use kfree() to free the last buffer that was transmitted from that 
//descriptor (if there was one).

int prev = -1;

for(int i=0; i<(TX_RING_SIZE-1); i++){
  if(tx_ring[i].cmd & E1000_TXD_CMD_EOP == 1){
    if(tx_ring[i].status & E1000_TXD_STAT_DD == 1){
      prev = i;
    }
  }
}

if(prev == -1){
  return -1;
}
kfree(tx_ring[prev].addr);

//Then fill in the descriptor. Set the necessary cmd flags (look at Section 
//3.3.3.1 EOP & RS in the E1000 manual) and stash away a pointer to the buffer 
//for later freeing. Set length. Set unused fields to zero.
  //cmd 🡸 E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS

tx_ring[index].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
tx_ring[index].addr = buf;
tx_ring[index].length = len;

//Finally, update the ring position by adding one to E1000_TDT modulo TX_RING_SIZE.
regs[E1000_TDT] = (index+1)%TX_RING_SIZE; 


//If e1000_transmit() added the packet successfully to the ring, return 0. 
//On failure (e.g., there is no descriptor available), return -1 so that the 
//caller knows to free the buffer.
  return 0;
}


//
// Check for packets that have arrived from the e1000
// Create and deliver a buf for each packet (using net_rx()).
//

// [E1000 3.2.3] 
/*
struct rx_desc                                                     
{                                                                  
  uint64 addr;    // Address of buffer
  uint16 length;  // Length of data
  uint16 csum;    // Frame checksum
  uint8 status;   // Status
  uint8 errors;   // Errors
  uint16 special; // Unused
}; 
*/

static void
e1000_recv(void) // Your code here.
{
//In loop (to handle case of more than one packet per interrupt):

  //First ask the E1000 for the ring index at which the next waiting 
  //received packet (if any) is located, by fetching the E1000_RDT control 
  //register and adding one modulo RX_RING_SIZE. 
      //Sanity check it. Panic if it fails.

  //Then check if a new packet is available by checking for the 
  //E1000_RXD_STAT_DD bit in the status portion of the descriptor. 
  //If not, stop.

  //Deliver the packet buffer to the network stack by calling net_rx().

  //Then allocate a new buffer using kalloc() to replace the one just 
  //given to net_rx(). Clear the descriptor's status bits to zero.

  //Finally, update the E1000_RDT register to be the index of the 
  //last ring descriptor processed.

  //e1000_init() initializes the RX ring with buffers, and you'll 
  //want to look at how it does that and perhaps borrow code.

  //At some point the total number of packets that have ever arrived 
  //will exceed the ring size (16); make sure your code can handle that.
}






void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
