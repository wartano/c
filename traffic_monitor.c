#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <pthread.h>

void *traffic_monitor(void *);
unsigned short handle_ethernet(const u_char *);
struct sockaddr_in * (*handle_IP(const struct pcap_pkthdr *, const u_char *))[2];
void my_callback(u_char *, const struct pcap_pkthdr *, const u_char *);
void free_sockaddr_pair(struct sockaddr_in *(*)[2]);
void cleanupHandler(void *);
void *alloc_mem(size_t);

typedef struct str_thdata
{
    char dev[100];
    char filter_exp[65535];
} thdata;

void cleanupHandler(void *cleanupData)
{
    pcap_t *handle = (pcap_t *)cleanupData;
    if (handle != NULL)
        pcap_close(handle);
}

void *alloc_mem(size_t size)
{
    void *p = malloc(size);
    if (p == NULL) 
        exit(1);
    
    return p;
}

void free_sockaddr_pair(struct sockaddr_in *(*sockaddr_pair)[2])
{
    if ((*sockaddr_pair)[0] != NULL)
        free((*sockaddr_pair)[0]);
    if ((*sockaddr_pair)[1] != NULL)
        free((*sockaddr_pair)[1]);
    if (sockaddr_pair != NULL)
        free(sockaddr_pair);
}

unsigned short handle_ethernet(const u_char *packet)
{
        /* lets start with the ether header... */
        struct ethhdr *eth = (struct ethhdr *)packet;
        //fprintf(stdout, "|-Destination Address : %.2X-%.2X-%.2X-%.2X-%.2X-%.2X \n", eth->h_dest[0] , eth->h_dest[1] , eth->h_dest[2] , eth->h_dest[3] , eth->h_dest[4] , eth->h_dest[5] );
        //fprintf(stdout, "|-Source Address      : %.2X-%.2X-%.2X-%.2X-%.2X-%.2X \n", eth->h_source[0] , eth->h_source[1] , eth->h_source[2] , eth->h_source[3] , eth->h_source[4] , eth->h_source[5] );
        //fprintf(stdout, "|-Protocol            : %x \n",htons((unsigned short)eth->h_proto));

        return (unsigned short)eth->h_proto;
}

struct sockaddr_in * (*handle_IP(const struct pcap_pkthdr *pkthdr, const u_char *packet))[2]
{
        struct sockaddr_in *source = (struct sockaddr_in *)alloc_mem(sizeof(struct sockaddr_in));
        struct sockaddr_in *dest = (struct sockaddr_in *)alloc_mem(sizeof(struct sockaddr_in));
        struct sockaddr_in *(*sockaddr_pair)[2] = (struct sockaddr_in *(*)[2])alloc_mem(sizeof(int));
        unsigned int version, iphdrlen, length, proto;

        struct iphdr *iph = (struct iphdr *)(packet + sizeof(struct ethhdr));
        version = (unsigned int)iph->version;
        /* check version */
        if (version != 4)
        {
                fprintf(stdout,"Unknown version %d\n", version);
                free_sockaddr_pair(sockaddr_pair);
                return NULL;
        }
        iphdrlen = (unsigned int)(iph->ihl) * 4;
        /* check header length */
        if (iphdrlen < 20)
        {
                fprintf(stdout,"bad iphdrlen %d \n", iphdrlen);
                free_sockaddr_pair(sockaddr_pair);
                return NULL;
        }
        length = ntohs(iph->tot_len);
        /* check to see we have a packet of valid length */
        if (length < sizeof(struct iphdr))
        {
                printf("truncated ip %d", length);
                free_sockaddr_pair(sockaddr_pair);
                return NULL;
        }
        proto = (unsigned int)iph->protocol;

        memset(source, 0, sizeof(*source));
        source->sin_addr.s_addr = iph->saddr;
        (*sockaddr_pair)[0] = source;

        memset(dest, 0, sizeof(*dest));
        dest->sin_addr.s_addr = iph->daddr;
        (*sockaddr_pair)[1] = dest;


        //fprintf(stdout , "   |-IP Version        : %d\n", version);
        //fprintf(stdout , "   |-IP Header Length  : %d Bytes\n", iphdrlen);
        //fprintf(stdout , "   |-IP Total Length   : %d  Bytes(Size of Packet)\n", length);
        //fprintf(stdout , "   |-Protocol : %d\n", proto);
        //fprintf(stdout , "   |-Source IP        : %s\n" , inet_ntoa(source->sin_addr));
        //fprintf(stdout , "   |-Destination IP   : %s\n" , inet_ntoa(dest->sin_addr));

        return sockaddr_pair;
}


void my_callback(u_char *useless, const struct pcap_pkthdr *pkthdr, const u_char *packet)
{
        int size = pkthdr->len;
        struct sockaddr_in ip_source, ip_dest;

        unsigned short type = handle_ethernet(packet);

        if (htons(type) == ETH_P_IP)
        {/* handle IP packet */
                //printf("It's an IP packet\n");
                struct sockaddr_in *(*sockaddr_pair)[2] = handle_IP(pkthdr, packet);
                if (sockaddr_pair != NULL)
                {
                        ip_source = *(*sockaddr_pair)[0];
                        ip_dest = *(*sockaddr_pair)[1];
                        free_sockaddr_pair(sockaddr_pair);
                        
                        printf("Source ip is %s\n", inet_ntoa(ip_source.sin_addr));
                        printf("Destination ip is %s\n", inet_ntoa(ip_dest.sin_addr));
                }
                
        }
}

void *traffic_monitor(void *data)
{
        //int rc;
        //rc = pthread_detach(pthread_self());
        //if (rc)
        //        pthread_exit(NULL);

        
        
        thdata *tdata = (thdata *)data;
        pcap_t *handle = NULL;                 /* Session handle */
        char *dev = tdata->dev;         /* The device to sniff on */
        char errbuf[PCAP_ERRBUF_SIZE];  /* Error string */
        struct bpf_program fp;          /* The compiled filter */
        char *filter_exp = tdata->filter_exp;   /* The filter expression */
        bpf_u_int32 maskp;              /* Our netmask */
        bpf_u_int32 netp;               /* Our network address */
        char *net; /* dot notation of the network address */
        char *mask;/* dot notation of the network mask */
        struct pcap_pkthdr header;      /* The header that pcap gives us */
        const u_char *packet;           /* The actual packet */
        struct in_addr addr;

        pthread_cleanup_push(cleanupHandler, handle);
        
        /* print out device name */
        printf("DEV: %s\n",dev);

        /* Find the properties for the device */
        if (pcap_lookupnet(dev, &netp, &maskp, errbuf) == -1) {
                fprintf(stderr, "Couldn't get network properties for device %s: %s\n", dev, errbuf);
                pthread_exit(NULL);
        }
        /* get the network address in a human readable form */
        addr.s_addr = netp;
        net = inet_ntoa(addr);
        if (net == NULL)
        {
                fprintf(stderr, "Error in inet_ntoa");
                pthread_exit(NULL);
        }
        printf("NET: %s\n", net);

        /* get the network mask in a human readable form */
        addr.s_addr = maskp;
        mask = inet_ntoa(addr);
        if(mask == NULL)
        {
                fprintf(stderr, "Error in inet_ntoa");
                pthread_exit(NULL);
        }
        printf("MASK: %s\n", mask);

        handle = pcap_open_live(dev, BUFSIZ, 0, 0, errbuf);
        if (handle == NULL) {
                fprintf(stderr, "Couldn't open device %s: %s\n", dev, errbuf);
                pthread_exit(NULL);
        }
        /* Compile and apply the filter */
        if (pcap_compile(handle, &fp, filter_exp, 0, netp) == -1) {
                fprintf(stderr, "Couldn't parse filter %s: %s\n", filter_exp, pcap_geterr(handle));
                pthread_exit(NULL);
        }
        if (pcap_setfilter(handle, &fp) == -1) {
                fprintf(stderr, "Couldn't install filter %s: %s\n", filter_exp, pcap_geterr(handle));
                pthread_exit(NULL);
        }
        pcap_loop(handle, -1, my_callback, NULL);

        pthread_exit(NULL);
}


int main(int argc, char *argv[])
{
        int rc;
        pthread_t thread;
        thdata data;
        strcpy(data.dev, "eth1");
        strcpy(data.filter_exp, "dst host 10.74.68.10 || dst host 10.74.68.1");
        rc = pthread_create(&thread, NULL, (void *) &traffic_monitor, (void *) &data);
        if (rc)
                exit(1);
        else
                pthread_join(thread, NULL);

        return 0;
}
