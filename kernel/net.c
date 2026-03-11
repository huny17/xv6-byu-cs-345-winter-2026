#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

// xv6's ethernet and IP addresses
static uint8 local_mac[ETHADDR_LEN] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15);

// qemu host's ethernet address.
static uint8 host_mac[ETHADDR_LEN] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };

static struct spinlock netlock;

struct port_table     
{                  
  // port number
  // bound flag
  // packet queue
  // pointer/index to queue head
  // pointer/index to queue tail

}; 
//static struct binding bindings[MAX_BINDINGS];

struct packet_queue
{
  //do I need 2 structs?
};



void
netinit(void)
{
  initlock(&netlock, "netlock");
}


/* *****LIST OF FUNC TO WORK ON*****
  -sys_bind(void) //net.c
  -sys_unbind(void) //net.c (opional)
  -sys_recv(void) //net.c
  -ip_rx(char *buf, int len) //net.c
  -vmprint(pagetable_t pagetable) //vm.c
*/


/*ORDER CALLED IN?
  bind
  ip_rx
  recv
*/


//UDP receive processing
  //e1000_receive() 
  //net_rx()
  //ip_rx()
  //udp_rx()
  //Enqueue packet for process to receive

  //Application
  //bind()
  //“Bind” port number to structure with queue for reception
  //recv()
  //Dequeue packet for process to receive
  //copyout()
  //Free packet


//
// bind(int port)
// prepare to receive UDP packets address to the port,
// i.e. allocate any queues &c needed.
//


/*
bind(short port): A process should call bind(port) 
before it calls recv(port, ...). If a UDP packet 
arrives with a destination port that hasn't been 
passed to bind(), net.c should discard that packet. 
The reason for this system call is to initialize 
any structures net.c needs in order to store arriving 
packets for a subsequent recv() call.  
(You will need to write this function.)
*/

uint64
sys_bind(void)  // Your code here.
{

  int port;
  //associates the socket with a local IP address and port so the OS 
  //knows which socket should receive incoming packets.



  //initialize any structures net.c needs in order to 
  port_table table;

  packet_queue pq;
  
  //store arriving packets for a subsequent recv() call
      //fill structure with ports given

  return -1;
}

//
// unbind(int port)
// release any resources previously created by bind(port);
// from now on UDP packets addressed to port should be dropped.
//
uint64
sys_unbind(void)
{
  //
  // Optional: Your code here.
  //

  return 0;
}

//
// recv(int dport, int *src, short *sport, char *buf, int maxlen)
// if there's a received UDP packet already queued that was
// addressed to dport, then return it.
// otherwise wait for such a packet.
//
// sets *src to the IP source address.
// sets *sport to the UDP source port.
// copies up to maxlen bytes of UDP payload to buf.
// returns the number of bytes copied,
// and -1 if there was an error.
//
// dport, *src, and *sport are host byte order.
// bind(dport) must previously have been called.
//

/*
bind(short port): A process should call bind(port) 
before it calls recv(port, ...). If a UDP packet 
arrives with a destination port that hasn't been 
passed to bind(), net.c should discard that packet. 
The reason for this system call is to initialize any 
structures net.c needs in order to store arriving packets 
for a subsequent recv() call.  (You will need to write 
this function.)
*/



uint64
sys_recv(void)   // Your code here.
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }

  //Called by the application to read the data that was 
  //placed in the socket buffer by the network stack.

  //If no packets are waiting, recv() should 
  //wait until a packet for dport arrives.
  
  //should see arriving packets for a given port in arrival order

  //copies the packet's 32-bit source IP address to *src, 
  //copies the packet's 16-bit UDP source port number to *sport, 
  //copies at most maxlen bytes of the packet's UDP payload to buf, 
  //and removes the packet from the queue


  e1000_recv();//do I need to send packet??

  //The system call returns the number of bytes of the UDP payload copied, 
  //or -1 if there was an error.


  return 0;
}

// This code is lifted from FreeBSD's ping.c, and is copyright by the Regents
// of the University of California.
static unsigned short
in_cksum(const unsigned char *addr, int len)
{
  int nleft = len;
  const unsigned short *w = (const unsigned short *)addr;
  unsigned int sum = 0;
  unsigned short answer = 0;

  /*
   * Our algorithm is simple, using a 32 bit accumulator (sum), we add
   * sequential 16 bit words to it, and at the end, fold back all the
   * carry bits from the top 16 bits into the lower 16 bits.
   */
  while (nleft > 1)  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if (nleft == 1) {
    *(unsigned char *)(&answer) = *(const unsigned char *)w;
    sum += answer;
  }

  /* add back carry outs from top 16 bits to low 16 bits */
  sum = (sum & 0xffff) + (sum >> 16);
  sum += (sum >> 16);
  /* guaranteed now that the lower 16 bits of sum are correct */

  answer = ~sum; /* truncate to 16 bits */
  return answer;
}

//
// send(int sport, int dst, int dport, char *buf, int len)
//

/*
send(short sport, int dst, short dport, char *buf, int len): 
This system call sends a UDP packet to the host with IP address 
dst, and (on that host) the process listening to port dport. 
The packet's source port number will be sport (this port number 
is reported to the receiving process, so that it can reply to 
the sender). The content ("payload") of the UDP packet will the 
len bytes at address buf. The return value is 0 on success, and 
-1 on failure. (This function is already provided.)
*/


uint64
sys_send(void)
{
  struct proc *p = myproc();
  int sport;
  int dst;
  int dport;
  uint64 bufaddr;
  int len;

  argint(0, &sport);
  argint(1, &dst);
  argint(2, &dport);
  argaddr(3, &bufaddr);
  argint(4, &len);

  int total = len + sizeof(struct eth) + sizeof(struct ip) + sizeof(struct udp);
  if(total > PGSIZE)
    return -1;

  char *buf = kalloc();
  if(buf == 0){
    printf("sys_send: kalloc failed\n");
    return -1;
  }
  memset(buf, 0, PGSIZE);

  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, host_mac, ETHADDR_LEN);
  memmove(eth->shost, local_mac, ETHADDR_LEN);
  eth->type = htons(ETHTYPE_IP);

  struct ip *ip = (struct ip *)(eth + 1);
  ip->ip_vhl = 0x45; // version 4, header length 4*5
  ip->ip_tos = 0;
  ip->ip_len = htons(sizeof(struct ip) + sizeof(struct udp) + len);
  ip->ip_id = 0;
  ip->ip_off = 0;
  ip->ip_ttl = 100;
  ip->ip_p = IPPROTO_UDP;
  ip->ip_src = htonl(local_ip);
  ip->ip_dst = htonl(dst);
  ip->ip_sum = in_cksum((unsigned char *)ip, sizeof(*ip));

  struct udp *udp = (struct udp *)(ip + 1);
  udp->sport = htons(sport);
  udp->dport = htons(dport);
  udp->ulen = htons(len + sizeof(struct udp));

  char *payload = (char *)(udp + 1);
  if(copyin(p->pagetable, payload, bufaddr, len) < 0){
    kfree(buf);
    printf("send: copyin failed\n");
    return -1;
  }

  e1000_transmit(buf, total);

  return 0;
}

  /*
  decide if the arriving packet is UDP, and whether its 
  destination port has been passed to bind(); if both are true, 
  it should save the packet where recv() can find it. However, 
  for any given port, no more than 16 packets should be saved; 
  if 16 are already waiting for recv(), an incoming packet for 
  that port should be dropped. The point of this rule is to prevent 
  a fast or abusive sender from forcing xv6 to run out of memory. 
  Furthermore, if packets are being dropped for one port because 
  it already has 16 packets waiting, that should not affect packets 
  arriving for other ports.

  The packet buffers that ip_rx() looks at contain a 14-byte 
  ethernet header, followed by a 20-byte IP header, followed by an 
  8-byte UDP header, followed by the UDP payload. You'll find C struct 
  definitions for each of these in kernel/net.h. 
  */

void
ip_rx(char *buf, int len)
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  //
  // Your code here.
  //

  //Triggered when a packet arrives from the network interface.

  //The IP layer processes the packet and determines which 
  //socket it belongs to (using the address/port information from bind).

  //The data is placed in the socket’s receive buffer.


  //decide if the arriving packet is UDP, and whether its 
  //destination port has been passed to bind()
    //true ->save the packet where recv() can find it

  //if 16 are already waiting for recv(), an incoming packet for 
  //that port should be dropped  
    // -> drop should not affect packets 
    //arriving for other ports.
  
}

//
// send an ARP reply packet to tell qemu to map
// xv6's ip address to its ethernet address.
// this is the bare minimum needed to persuade
// qemu to send IP packets to xv6; the real ARP
// protocol is more complex.
//
void
arp_rx(char *inbuf)
{
  static int seen_arp = 0;

  if(seen_arp){
    kfree(inbuf);
    return;
  }
  printf("arp_rx: received an ARP packet\n");
  seen_arp = 1;

  struct eth *ineth = (struct eth *) inbuf;
  struct arp *inarp = (struct arp *) (ineth + 1);

  char *buf = kalloc();
  if(buf == 0)
    panic("send_arp_reply");
  
  struct eth *eth = (struct eth *) buf;
  memmove(eth->dhost, ineth->shost, ETHADDR_LEN); // ethernet destination = query source
  memmove(eth->shost, local_mac, ETHADDR_LEN); // ethernet source = xv6's ethernet address
  eth->type = htons(ETHTYPE_ARP);

  struct arp *arp = (struct arp *)(eth + 1);
  arp->hrd = htons(ARP_HRD_ETHER);
  arp->pro = htons(ETHTYPE_IP);
  arp->hln = ETHADDR_LEN;
  arp->pln = sizeof(uint32);
  arp->op = htons(ARP_OP_REPLY);

  memmove(arp->sha, local_mac, ETHADDR_LEN);
  arp->sip = htonl(local_ip);
  memmove(arp->tha, ineth->shost, ETHADDR_LEN);
  arp->tip = inarp->sip;

  e1000_transmit(buf, sizeof(*eth) + sizeof(*arp));

  kfree(inbuf);
}

void
net_rx(char *buf, int len)
{
  struct eth *eth = (struct eth *) buf;

  if(len >= sizeof(struct eth) + sizeof(struct arp) &&
     ntohs(eth->type) == ETHTYPE_ARP){
    arp_rx(buf);
  } else if(len >= sizeof(struct eth) + sizeof(struct ip) &&
     ntohs(eth->type) == ETHTYPE_IP){
    ip_rx(buf, len);
  } else {
    kfree(buf);
  }
}
