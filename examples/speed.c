/*  cryptodev_test - simple benchmark tool for cryptodev
 *
 *    Copyright (C) 2010 by Phil Sutter <phil.sutter@viprinet.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <linux/netlink.h>
#include "../ncr.h"

#define ALIGN_NL __attribute__((aligned(NLA_ALIGNTO)))

static double udifftimeval(struct timeval start, struct timeval end)
{
	return (double)(end.tv_usec - start.tv_usec) +
	       (double)(end.tv_sec - start.tv_sec) * 1000 * 1000;
}

static int must_finish = 0;

static void alarm_handler(int signo)
{
        must_finish = 1;
}

static void value2human(double bytes, double time, double* data, double* speed,char* metric)
{
        if (bytes > 1000 && bytes < 1000*1000) {
                *data = ((double)bytes)/1000;
                *speed = *data/time;
                strcpy(metric, "Kb");
                return;
        } else if (bytes >= 1000*1000 && bytes < 1000*1000*1000) {
                *data = ((double)bytes)/(1000*1000);
                *speed = *data/time;
                strcpy(metric, "Mb");
                return;
        } else if (bytes >= 1000*1000*1000) {
                *data = ((double)bytes)/(1000*1000*1000);
                *speed = *data/time;
                strcpy(metric, "Gb");
                return;
        } else {
                *data = (double)bytes;
                *speed = *data/time;
                strcpy(metric, "bytes");
                return;
        }
}


int encrypt_data_ncr_direct(int cfd, int algo, int chunksize)
{
	char *buffer, iv[32];
	static int val = 23;
	struct timeval start, end;
	double total = 0;
	double secs, ddata, dspeed;
	char metric[16];
	ncr_key_t key;
	struct __attribute__((packed)) {
		struct ncr_key_generate f;
		struct nlattr algo_head ALIGN_NL;
		uint32_t algo ALIGN_NL;
		struct nlattr bits_head ALIGN_NL;
		uint32_t bits ALIGN_NL;
	} kgen;
	struct ncr_session_once_op_st nop;

	key = ioctl(cfd, NCRIO_KEY_INIT);
	if (key == -1) {
		fprintf(stderr, "Error: %s:%d\n", __func__, __LINE__);
		perror("ioctl(NCRIO_KEY_INIT)");
		return 1;
	}

	memset(&kgen.f, 0, sizeof(kgen.f));
	kgen.f.input_size = sizeof(kgen);
	kgen.f.key = key;
	kgen.algo_head.nla_len = NLA_HDRLEN + sizeof(kgen.algo);
	kgen.algo_head.nla_type = NCR_ATTR_ALGORITHM;
	kgen.algo = NCR_ALG_AES_CBC;
	kgen.bits_head.nla_len = NLA_HDRLEN + sizeof(kgen.bits);
	kgen.bits_head.nla_type = NCR_ATTR_SECRET_KEY_BITS;
	kgen.bits = 128; /* 16 bytes */

	if (ioctl(cfd, NCRIO_KEY_GENERATE, &kgen)) {
		fprintf(stderr, "Error: %s:%d\n", __func__, __LINE__);
		perror("ioctl(NCRIO_KEY_GENERATE)");
		return 1;
	}


	buffer = malloc(chunksize);
	memset(iv, 0x23, 32);

	printf("\tEncrypting in chunks of %d bytes: ", chunksize);
	fflush(stdout);

	memset(buffer, val++, chunksize);

	must_finish = 0;
	alarm(5);

	gettimeofday(&start, NULL);
	do {
		memset(&nop, 0, sizeof(nop));
		nop.init.algorithm = algo;
		nop.init.key = key;
		nop.init.op = NCR_OP_ENCRYPT;
		nop.op.data.udata.input = buffer;
		nop.op.data.udata.input_size = chunksize;
		nop.op.data.udata.output = buffer;
		nop.op.data.udata.output_size = chunksize;
		nop.op.type = NCR_DIRECT_DATA;

		if (ioctl(cfd, NCRIO_SESSION_ONCE, &nop)) {
			fprintf(stderr, "Error: %s:%d\n", __func__, __LINE__);
			perror("ioctl(NCRIO_SESSION_ONCE)");
			return 1;
		}

		total+=chunksize;
	} while(must_finish==0);
	gettimeofday(&end, NULL);

	secs = udifftimeval(start, end)/ 1000000.0;
	
	value2human(total, secs, &ddata, &dspeed, metric);
	printf ("done. %.2f %s in %.2f secs: ", ddata, metric, secs);
	printf ("%.2f %s/sec\n", dspeed, metric);

	return 0;
}

int main(void)
{
	int fd, i;

	signal(SIGALRM, alarm_handler);

	if ((fd = open("/dev/crypto", O_RDWR, 0)) < 0) {
		perror("open()");
		return 1;
	}

	fprintf(stderr, "\nTesting NCR-DIRECT with NULL cipher: \n");
	for (i = 256; i <= (64 * 1024); i *= 2) {
		if (encrypt_data_ncr_direct(fd, NCR_ALG_NULL, i))
			break;
	}


	fprintf(stderr, "\nTesting NCR-DIRECT with AES-128-CBC cipher: \n");
	for (i = 256; i <= (64 * 1024); i *= 2) {
		if (encrypt_data_ncr_direct(fd, NCR_ALG_AES_CBC, i))
			break;
	}

	close(fd);
	return 0;
}
