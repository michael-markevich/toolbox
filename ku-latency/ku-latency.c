/*

Modified 2023, Michael Markevich, https://github.com/michael-markevich

Modified 2019, Emil Hammarstrom, https://github.com/ehammarstrom

Copyright (c) 2008, Max Vilimpoc, https://vilimpoc.org

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above
      copyright notice, this list of conditions and the following
      disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials
      provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products
      derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/time.h>

#include <sys/ioctl.h>
#include <linux/if.h>

#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <byteswap.h>

#include <signal.h>

#include <getopt.h>

#define BUFSIZE (1048576)

typedef enum
{
    STRERROR_LEN           = 80,
    NUM_LATENCIES          = 32,
    DEFAULT_PORT           = 1025,
} CONSTS;

static int                 inSocket;
static struct sockaddr_in  inInAddr;

static int                 totalUsec;
static int                 totalPackets;

static int                 latencies[NUM_LATENCIES];
static int                 ic;
static int                 rollingAverage;

static bool                keepRunning;



void Usage(const char * const progName)
{
    printf("\n");
    printf("Usage: %s [-i IP address] [-e ethx] [-p port]\n", progName);
    printf("\n");
    printf("       -h          Help.\n");
    printf("       -i          Specifies IP address of interface you want to listen to.\n");
    printf("       -e          Specifies the ethernet interface name you want to listen to.\n");
    printf("       -p          Specifies port number of packets you want to see.\n");
    printf("       -l          Log (RTP Sequence Number, Kernel Latency) to file for each packet.\n");
    printf("       -n          Stop after N packets (default N=3000).\n");
    printf("       -v          Verbose, print kernel latency stats to stdout.\n");
    printf("\n");
    printf("If no option is specified (they are all optional), then the program\n");
    printf("will listen on IPADDR_ANY (all interfaces), port 1025.");
    printf("\n");
    printf("\n");
}


static void printError(int errorCode, const char * const lastFunction)
{
    static char strError[STRERROR_LEN];
    memset(strError, 0, STRERROR_LEN);

    /* Old school. */
    perror(lastFunction);

    /* New school. */
    strerror_r(errorCode, strError, STRERROR_LEN);

    printf("%s", strError);
    printf("\n");
}


void catchIntr(int signalNumber)
{
    /* Exit the main loop. */
    keepRunning = false;
}

int main(int argc, char **argv)
{
    int rc = 0;

    /* Defaults */
    inInAddr.sin_addr.s_addr    = INADDR_ANY;
    inInAddr.sin_port           = htons(DEFAULT_PORT);
    inInAddr.sin_family         = AF_INET;

    inSocket = socket(PF_INET, SOCK_DGRAM, 0);
    if (0 > inSocket)
    {
        printf("socket() call failed.\n");
        printError(inSocket, "socket");
        rc = -1;
        goto socket_failed;
    }

    /* Process cmdline opts. */
    char *shortOpts = "hi:e:p:v:n:l";
    int   getoptRet;
    int verbose = false;
    int log = false;
    unsigned int stop_num_packets = 3000;

    while(-1 != (getoptRet = getopt(argc, argv, shortOpts)))
    {
        switch(getoptRet)
        {
            case 'i':
                inInAddr.sin_addr.s_addr = inet_addr(optarg);
                break;
            case 'e':
                {
                struct ifreq fetchIfInfo;
                memset(&fetchIfInfo, 0, sizeof(struct ifreq));
                memcpy(fetchIfInfo.ifr_name, optarg, IFNAMSIZ - 1);

                /* Fetch the IP address to listen to based on interface name. */
                ioctl(inSocket, SIOCGIFADDR, &fetchIfInfo);

                struct sockaddr_in * const sockInfo = (struct sockaddr_in * const) &fetchIfInfo.ifr_addr;
                inInAddr.sin_addr.s_addr   = sockInfo->sin_addr.s_addr;
                }
                break;
            case 'p':
                inInAddr.sin_port        = htons(atoi(optarg));
                break;
            case 'l':
                log = true;
                break;
            case 'n':
                stop_num_packets = atoi(optarg);
                break;
            case 'v':
                verbose = true; 
                break;
            case 'h':
            case '?':
            default:
                Usage(argv[0]);
                goto normal_exit;
                break;
        }
    }


    printf("Listening to: %s:%d\n", inet_ntoa(inInAddr.sin_addr),
                                    ntohs(inInAddr.sin_port));

    int timestampOn = 1;
    rc = setsockopt(inSocket, SOL_SOCKET, SO_TIMESTAMP, (int *) &timestampOn, sizeof(timestampOn));
    if (0 > rc)
    {
        printf("setsockopt(SO_TIMESTAMP) failed.\n");
        printError(rc, "setsockopt");
        goto setsockopt_failed;
    }

    int bufsize = BUFSIZE;
    socklen_t bufsize_len = sizeof(bufsize);

    rc = setsockopt(inSocket, SOL_SOCKET, SO_RCVBUFFORCE, &bufsize, bufsize_len);
    if (0 > rc)
    {
        printf("setsockopt(SO_RCVBUFFORCE) failed.\n");
        printError(rc, "setsockopt");
        goto setsockopt_failed;
    }

    rc = getsockopt(inSocket, SOL_SOCKET, SO_RCVBUFFORCE, &bufsize, &bufsize_len);
    printf("getsockopt(SO_RCVBUFFORCE) = %d\n", bufsize);

    rc = bind(inSocket, (struct sockaddr *) &inInAddr, sizeof(struct sockaddr_in));
    if (0 > rc)
    {
        printf("UDP bind() failed.\n");
        printError(rc, "bind");
        goto bind_failed;
    }

    struct msghdr   msg;
    struct iovec    iov;
    char            pktbuf[4096];

    char            ctrl[CMSG_SPACE(sizeof(struct timeval))];
    struct cmsghdr *cmsg = (struct cmsghdr *) &ctrl;

    msg.msg_control      = (char *) ctrl;
    msg.msg_controllen   = sizeof(ctrl);

    msg.msg_name         = &inInAddr;
    msg.msg_namelen      = sizeof(inInAddr);
    msg.msg_iov          = &iov;
    msg.msg_iovlen       = 1;
    iov.iov_base         = pktbuf;
    iov.iov_len          = sizeof(pktbuf);

    struct timeval  time_kernel, time_user;
    int             timediff;

    char *logbuffer;
    FILE *logfile = NULL;

    if (log) {
        logbuffer = calloc(BUFSIZE, sizeof(char));
        logfile = (FILE *) fmemopen((void *) logbuffer, BUFSIZE, "w");

        if (logfile == NULL) {
            printf("fmemopen: failed\n");
            exit(1);
        }
    }

    unsigned int num_packets = 0;

    for(keepRunning = true; keepRunning;)
    {
        rc = recvmsg(inSocket, &msg, 0);
        gettimeofday(&time_user, NULL);


        if (cmsg->cmsg_level == SOL_SOCKET &&
            cmsg->cmsg_type == SCM_TIMESTAMP &&
            cmsg->cmsg_len == CMSG_LEN(sizeof(time_kernel)))
        {
            memcpy(&time_kernel, CMSG_DATA(cmsg), sizeof(time_kernel));
        }

        if (verbose) {
            printf("\n");
            printf("time_kernel                  : %d.%06d\n", (int)time_kernel.tv_sec,
                   (int)time_kernel.tv_usec);
            printf("time_user                    : %d.%06d\n", (int)time_user.tv_sec,
                   (int)time_user.tv_usec);

            timediff = (time_user.tv_sec - time_kernel.tv_sec) * 1000000 +
                       (time_user.tv_usec - time_kernel.tv_usec);

            printf("Time diff                    : %d us\n", timediff);

            totalUsec += timediff;
            ++totalPackets;

            rollingAverage += timediff;
            rollingAverage -= latencies[ic];
            latencies[ic] = timediff;
            ic = (ic + 1) % NUM_LATENCIES;

            printf("Total Average                : %d/%d = %.2f us\n", totalUsec,
                   totalPackets,
                   (float)totalUsec / totalPackets);
            printf("Rolling Average (%d samples) : %.2f us\n", NUM_LATENCIES,
                   (float)rollingAverage / NUM_LATENCIES);
        }

        if (log && rc > 0) {
            unsigned short *pkt = iov.iov_base;

            unsigned short seq_num = __bswap_16(*(pkt + 1));

            timediff = (time_user.tv_sec - time_kernel.tv_sec) * 1000000 +
                       (time_user.tv_usec - time_kernel.tv_usec);

            fprintf(logfile, "%d %d\n", seq_num, timediff);
        }

        if (rc > 0)
            ++num_packets;

        if (num_packets >= stop_num_packets)
            break;
    }

    if (log) {
        fclose(logfile);

        char *str_logfile = calloc(1024, sizeof(char));
        sprintf(str_logfile, "ku-latency.log");
        FILE *real_logfile = fopen(str_logfile, "w+");

        fwrite(logbuffer, sizeof(char), strlen(logbuffer), real_logfile);
        fclose(real_logfile);
    }

bind_failed:
setsockopt_failed:
    close(inSocket);
socket_failed:
normal_exit:
    return rc;
}
