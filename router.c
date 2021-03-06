#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h> // inet_addr, htons
#include <time.h>
#include <assert.h>
#include "router.h"
#include "console.h"
#include "packet.h"
#include "utils.h"

#define BUF_SIZE 1024
#define BROADCAST_PERIOD 5
#define FWD_DELAY_IN_MS 10
#define INFINITY 8
int MY_ID;
/* ========================================= */
/* ========== FORWARD DATA PACKET ========== */
/* ========================================= */

/* ========================================================================= */
/* ************************ A FAIRE PAR LES ETUDIANTS ********************** */
/* ========================================================================= */
int forward_packet(packet_data_t *packet, int psize, routing_table_t *rt)
{

    for (unsigned short int i = 0; i < rt->size; ++i)
    {
        //si adresse destination existe, envoyer le paquet
        if (rt->tab[i].dest == packet->dst_id)
        {
            send_packet(packet, psize, rt->tab[i].nexthop.ipv4, rt->tab[i].nexthop.port);
            return 1;
        }
    }
    return 0;
}

/* ==================================================================== */
/* ========================== HELLO THREAD ============================ */
/* ==================================================================== */

/* ========================================================================= */
/* ************************ A FAIRE PAR LES ETUDIANTS ********************** */
/* ========================================================================= */

// Build distance vector packet
void build_dv_packet(packet_ctrl_t *p, routing_table_t *rt)
{
    p->type = CTRL;
    p->src_id = MY_ID;
    p->dv_size = rt->size;
    for (unsigned short int i = 0; i < rt->size; ++i)
    {
        p->dv[i].dest = rt->tab[i].dest;
        p->dv[i].metric = rt->tab[i].metric;
    }
}

// DV to prevent (partially) count to infinity problem
// Build a DV that contains the routes that have not been learned via
// this neighbour
void build_dv_specific(packet_ctrl_t *p, routing_table_t *rt, node_id_t neigh)
{
    p->type = CTRL;
    unsigned short int size = 0;
    p->src_id = MY_ID;
    for (unsigned short int i = 0; i < rt->size; ++i)
    {
        if (rt->tab[i].nexthop.id != neigh)
        {

            p->dv[size].dest = rt->tab[i].dest;
            p->dv[size].metric = rt->tab[i].metric;
            size++;
        }
    }
    p->dv_size = size;

    /* TODO */
}

// Remove old RT entries
void remove_entry(routing_table_t *rt, unsigned short int index)
{
    //index: l'indice de l'element a suprimmer
    rt->tab[index] = rt->tab[rt->size - 1]; // echanger le dernier elem avec l'elem a suprimmer
    rt->size--;
}
void remove_obsolete_entries(routing_table_t *rt)
{
    for (unsigned short int i = 0; i < rt->size; ++i)
    {
        if (rt->tab[i].dest != MY_ID)
        { //Recuperation du TTL
            double ttl = difftime(time(NULL), rt->tab[i].time);
            if (ttl > BROADCAST_PERIOD)
            {
                remove_entry(rt, i);
            }
            //suppression de la route si metric superieur ?? 16
             if (rt->tab[i].metric > 16)
            {
                remove_entry(rt, i);
            }
        }
    }

    /* TODO */
}

// Hello thread to broadcast state to neighbors
void *hello(void *args)
{

    /* Cast the pointer to the right type */
    struct th_args *pargs = (struct th_args *)args;
    /* Get routing and neighbors table */
    routing_table_t *rt = pargs->rt;
    neighbors_table_t *nt = pargs->nt;

    /* >>>>>>>>>> A COMPLETER PAR LES ETUDIANTS - DEB <<<<<<<<<< */

    /* TODO */

    while (1)
    {

        /* TODO */
        packet_ctrl_t p;

        //build_dv_packet(&p, rt);
        //Envoie du paquet a tous les voisins
        for (unsigned short int i = 0; i < nt->size; ++i)
        {
            build_dv_specific(&p, rt, nt->tab[i].id);
            send_packet(&p, sizeof(packet_ctrl_t), nt->tab[i].ipv4, nt->tab[i].port);

            log_dv(&p, nt->tab[i].id, 1);
        }

        /* >>>>>>>>>> A COMPLETER PAR LES ETUDIANTS - FIN <<<<<<<<<< */
        sleep(BROADCAST_PERIOD);
        remove_obsolete_entries(rt);
    }
}

/* ==================================================================== */
/* ======================== UDP SERVER THREAD ========================= */
/* ==================================================================== */

/* ========================================================================= */
/* ************************ A FAIRE PAR LES ETUDIANTS ********************** */
/* ========================================================================= */
int existe(routing_table_t *rt, node_id_t dest)
{
    for (short int i = 0; i < rt->size; ++i)
        if (rt->tab[i].dest == dest)
            return i;
    return -1;
}

// Update routing table from received distance vector
int update_rt(routing_table_t *rt, overlay_addr_t *src, dv_entry_t dv[], int dv_size)
{
    for (int i = 0; i < dv_size; ++i)
    { //verification de l'existance de la route dans la table de routage
        int res = existe(rt, dv[i].dest);
        if (res == -1)
        {
            //ajout d'une nouvelle route
            add_route(rt, dv[i].dest, src, dv[i].metric + 1);
        }
        else if (rt->tab[res].metric > dv[i].metric + 1 || rt->tab[res].nexthop.id == src->id)
        {
            //mis ?? jour de la route avec une meilleur metric
            rt->tab[res].metric = dv[i].metric + 1;
            rt->tab[res].time = time(NULL);
            rt->tab[res].nexthop = *src;
        }
    }

    /* TODO */
    return 1;
}

// Server thread waiting for input packets
void *process_input_packets(void *args)
{

    int sock;
    struct sockaddr_in my_adr, neigh_adr;
    socklen_t adr_len = sizeof(struct sockaddr_in);
    char buffer_in[BUF_SIZE];
    /* Cast the pointer to the right type */
    struct th_args *pargs = (struct th_args *)args;
    /* Get routing and neighbors table */
    routing_table_t *rt = pargs->rt;
    neighbors_table_t *nt = pargs->nt;

    int port = PORT(MY_ID);
    int size = 0;

    /* Create (server) socket */
    /* ---------------------- */
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    /* Bind address and port */
    /*-----------------------*/
    /* Init server adr  */
    memset(&my_adr, 0, sizeof(my_adr));
    my_adr.sin_family = AF_INET;
    my_adr.sin_port = htons(port);
    my_adr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&my_adr, sizeof(my_adr)) < 0)
    {
        perror("bind error");
        close(sock);
        exit(EXIT_FAILURE);
    }

    logger("SERVER TH", "waiting for incoming messages");
    while (1)
    {

        if ((size = recvfrom(sock, buffer_in, BUF_SIZE, 0, (struct sockaddr *)&neigh_adr, &adr_len)) < 0)
        {
            perror("recvfrom error");
            exit(EXIT_FAILURE);
        }

        switch (buffer_in[0])
        {

        case DATA:
            logger("SERVER TH", "DATA packet received");
            packet_data_t *pdata = (packet_data_t *)buffer_in;
            if (pdata->dst_id == MY_ID)
            {
                switch (pdata->subtype)
                {
                case ECHO_REQUEST:
                    send_ping_reply(pdata, rt);
                    break;
                case ECHO_REPLY:
                    print_ping_reply(pdata);
                    break;
                case TR_REQUEST:
                    send_traceroute_reply(pdata, rt);
                    break;
                case TR_TIME_EXCEEDED:
                    print_traceroute_path(pdata);
                    break;
                case TR_ARRIVED:
                    print_traceroute_last(pdata);
                    break;
                default:
                    logger("SERVER TH", "unidentified data packet received");
                }
            }
            else
            {
                //Reduction du TTL
                pdata->ttl--;
                if (pdata->ttl != 0)
                {
                    //envoie du paquet si TTL > 0
                    forward_packet(pdata, size, rt);
                }
                else
                {
                    send_time_exceeded(pdata, rt);
                }

                /* I am NOT the recipient ==> forward packet */
                /* >>>>>>>>>> A COMPLETER PAR LES ETUDIANTS - DEB <<<<<<<<<< */
                //int forward_packet(packet_data_t *packet, int psize, routing_table_t *rt)
                //pdata->ttl--;

                /* TODO */

                /* >>>>>>>>>> A COMPLETER PAR LES ETUDIANTS - FIN <<<<<<<<<< */
            }
            break;

        case CTRL:
            logger("SERVER TH", "CTRL packet received");

            packet_ctrl_t *pctrl = (packet_ctrl_t *)buffer_in;

            /* >>>>>>>>>> A COMPLETER PAR LES ETUDIANTS - DEB <<<<<<<<<< */
            for (short int i = 0; i < nt->size; ++i)
            {
                if (nt->tab[i].id == pctrl->src_id)
                {
                    //mis ?? jour de la table de routage
                    update_rt(rt, &(nt->tab[i]), pctrl->dv, pctrl->dv_size);
                }
            }
            log_dv(pctrl, pctrl->src_id, 0);

            /* >>>>>>>>>> A COMPLETER PAR LES ETUDIANTS - FIN <<<<<<<<<< */
            break;

        default:
            // drop
            logger("SERVER TH", "unidentified packet received.");
            break;
        }
    }
}

/* ==================================================================== */
/* ========================== MAIN PROGRAM ============================ */
/* ==================================================================== */

int main(int argc, char **argv)
{

    routing_table_t myrt;
    neighbors_table_t mynt;
    pthread_t th1_id, th2_id;
    struct th_args args;
    int test_forwarding = 0;

    // printf("sizeof(routing table) = %lu bytes\n", sizeof(myrt));
    // printf("sizeof(neighbors table) = %lu bytes\n", sizeof(myrt));

    if (argc != 3)
    {
        printf("Usage: %s <id> <net_topo_conf>\n", argv[0]);
        printf("or\n");
        printf("Usage: %s <id> --test-forwarding\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // ==== Init ROUTER ====
    myrt.size = 0;
    mynt.size = 0;
    int rid = atoi(argv[1]);
    MY_ID = rid; // shared ID between threads
    printf("**************\n");
    printf("* RTR ID : %d *\n", MY_ID);
    printf("**************\n");

    if (strcmp(argv[2], "--test-forwarding") == 0)
    {
        init_full_routing_table(&myrt);
        test_forwarding = 1;
    }
    else
    {
        read_neighbors(argv[2], rid, &mynt);
        init_routing_table(&myrt);
    }
    // ====================
    // print_neighbors(&mynt);
    // print_rt(&myrt);
    args.rt = &myrt;
    args.nt = &mynt;

    /* Create a new thread th1 (process input packets) */
    pthread_create(&th1_id, NULL, &process_input_packets, &args);
    logger("MAIN TH", "process input packets thread created with ID %u", (int)th1_id);

    if (!test_forwarding)
    {
        /* Create a new thread th2 (hello broadcast) */
        pthread_create(&th2_id, NULL, &hello, &args);
        logger("MAIN TH", "hello thread created with ID %u", (int)th2_id);
    }

    int quit = 0, len;
    char *command = NULL;
    size_t size;
    while (!quit)
    {
        print_prompt();
        len = getline(&command, &size, stdin);
        command[len - 1] = '\0'; // remove newline
        quit = !strcmp("quit", command) || !strcmp("exit", command);
        if (!quit)
            process_command(command, &myrt, &mynt);
        free(command);
        command = NULL;
    }

    /* Make sure threads have finished */
    // pthread_join(th1_id, NULL);
    // pthread_join(th2_id, NULL);

    return EXIT_SUCCESS;
}
