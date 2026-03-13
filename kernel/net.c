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

#define QUEUE_SIZE 16
#define MAX_SOCKETS 8

struct socket     
{                  
  int port_number; //0 if free
  uint64 packet_queue[QUEUE_SIZE];
  int head;
  int tail;
  //increment forever -> subtract and mod
  //modular -> make sure head doesn't reach tail
}; 

static struct socket sockets[MAX_SOCKETS];


void
netinit(void)
{
  initlock(&netlock, "netlock");
}

int
full(int head, int tail){
  if(head-tail == QUEUE_SIZE){
    return 1;
  }
  return 0;
}

int
empty(int head, int tail){
  if(head==tail){
    return 1;
  }
  return 0;
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

  acquire(&netlock);
  int port;
  //associates the socket with a local IP address and port so the OS 
  //knows which socket should receive incoming packets.
  argint(0, &port);

  //initialize any structures net.c needs in order to 
  for (int i = 0; i < MAX_SOCKETS; i++){
    if (sockets[i].port_number == port){
      release(&netlock);
      return -1;
    }
    if (sockets[i].port_number == 0){
        sockets[i].port_number = port;
        sockets[i].head = 0;
        sockets[i].tail = 0;
        release(&netlock);
        return 0;
    }
  }
  
  //store arriving packets for a subsequent recv() call
      //fill structure with ports given
  release(&netlock);
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
  acquire(&netlock);

  struct proc *p = myproc();
  int dport;
  int src;
  int sport;
  uint64 bufaddr;
  int maxlen;

  argint(0, &dport);
  argint(1, &src);
  argint(2, &sport);
  argaddr(3, &bufaddr);
  argint(4, &maxlen);

  struct socket *found = 0;
  uint64 packet;  
  int pay_size;

  //The IP layer processes the packet and determines which 
  //socket it belongs to (using the address/port information from bind).
  for (int i = 0; i < MAX_SOCKETS; i++){
    if(dport == sockets[i].port_number){ //destination port has been passed to bind()
      found = &sockets[i];
      while(empty(sockets[i].head, sockets[i].tail)==1){
        sleep(found, &netlock);
      }
      packet = found->packet_queue[found->tail];
      found->tail = (found->tail + 1)%QUEUE_SIZE; //add to the head, remove from the tail
      
      

      struct ip *ip_p = (struct ip *)((uint64)packet + sizeof(struct eth));
      struct udp *udp_p = (struct udp *)((uint64)ip_p + sizeof(struct ip)); 
      char * payload = (char *)((uint64)udp_p + sizeof(struct udp));
      //pagetable_t pagetable, uint64 dstva, char *src, uint64 len

      int h_src= ntohl(ip_p->ip_src);
      int h_sport = ntohs(udp_p->sport);

      pay_size = ntohs(udp_p->ulen) - sizeof(struct udp);

      //Payload
      if(copyout(p->pagetable, bufaddr, payload, pay_size)<0){ 
        kfree((void*)packet);
        release(&netlock);
        return -1;
      }
      //Src IP
      if(copyout(p->pagetable, (uint64)src, (char *)&h_src,  sizeof(ip_p->ip_src))<0){
        kfree((void*)packet);
        release(&netlock);
        return -1;
      }
      //Src port
      if(copyout(p->pagetable, (uint64)sport, (char *)&h_sport, sizeof(udp_p->sport))<0){
        kfree((void*)packet);
        release(&netlock);
        return -1;
      }
      kfree((void*)packet);
      break;
    }
  }
  if(found == 0){
    return -1;
  }
  release(&netlock);
  return pay_size;
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


  //flip endian again

  //e1000_recv();//do I need to send packet??

  //The system call returns the number of bytes of the UDP payload copied, 
  //or -1 if there was an error.


  


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
udp_rx(char *buf, int port){

  struct socket *found = 0;

  acquire(&netlock);
  //The IP layer processes the packet and determines which 
  //socket it belongs to (using the address/port information from bind).
  for (int i = 0; i < MAX_SOCKETS; i++){
    if(port == sockets[i].port_number){ //destination port has been passed to bind()
      //The data is placed in the socket’s receive buffer.
      //true ->save the packet where recv() can find it
      if(!full(sockets[i].head, sockets[i].tail)){
        found = &sockets[i];
        found->packet_queue[found->head] = ((uint64)buf);
        found->head = (found->head + 1)%QUEUE_SIZE; //add to the head, remove from the tail
        wakeup(found);
        release(&netlock);
        return;
      }
      else{
        kfree(buf);
        release(&netlock);
        return;
      }
    }
  }
  if(found == 0){
    kfree(buf);
  }
  release(&netlock);
  return;
}

void
ip_rx(char *buf, int len)  // Your code here, triggered when a packet arrives from the network interface.
{
  // don't delete this printf; make grade depends on it.
  static int seen_ip = 0;
  if(seen_ip == 0)
    printf("ip_rx: received an IP packet\n");
  seen_ip = 1;

  struct ip *ip_p = (struct ip *)((uint64)buf + sizeof(struct eth));
  if(ip_p->ip_p != IPPROTO_UDP){
    kfree(buf);
    return;
  }

  struct udp *udp_p = (struct udp *)((uint64)ip_p + sizeof(struct ip));   //e -> ip -> udp : decide if the arriving packet is UDP
  
  int port = ntohs(udp_p->dport); //get port from udp struc using hs to change endian
  
  udp_rx(buf, port);
  
  //if 16 are already waiting for recv(), an incoming packet for 
  //that port should be dropped  
    //drop should not affect packets arriving for other ports.
  
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
