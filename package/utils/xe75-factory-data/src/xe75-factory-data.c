// SPDX-License-Identifier: GPL-2.0-or-later
/* Read the unit's original inputs; write only a new RAM staging directory.
 * No vendor board files, calibration or factory identities are distributed.
 */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SIZE 0x20000
static uint8_t cal[3][SIZE], board[3][SIZE], mac[6];
static const unsigned offsets[] = { 0x1000, 0x4c000, 0x26800 };
static const char *boards[] = {
	"IPQ5018/bdwlan_XE75Pro_DE_v1.b24",
	"qcn6122/bdwlan_XE75Pro_DE_v1.b60",
	"qcn6122/bdwlan_XE75Pro_DE_v1.bb0"
};
static const char *calnames[] = {
	"IPQ5018/hw1.0/cal-ahb-c000000.wifi.bin",
	"QCN6122/hw1.0/cal-ahb-b00a040.wifi.bin",
	"QCN6122/hw1.0/cal-ahb-b00b040.wifi.bin"
};

static void fail(const char *msg)
{
	fprintf(stderr, "xe75-factory-data: %s\n", msg);
	exit(1);
}

static void path(char *out, size_t size, const char *root, const char *name)
{
	if (snprintf(out, size, "%s/%s", root, name) >= (int)size)
		fail("path too long");
}

static void read_exact(const char *name, uint8_t *out, size_t size, off_t offset,
		       off_t expected)
{
	int fd = open(name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	struct stat st;
	if (fd < 0 || fstat(fd, &st))
		fail("input unavailable");
	/* Character MTD devices report size zero; actual reads must still complete. */
	if (!(S_ISREG(st.st_mode) || S_ISCHR(st.st_mode)) ||
	    (S_ISREG(st.st_mode) && st.st_size != expected))
		fail("unexpected input type or length");
	size_t done = 0;
	while (done < size) {
		ssize_t n = pread(fd, out + done, size - done, offset + done);
		if (n <= 0)
			fail("short input read");
		done += n;
	}
	if (close(fd))
		fail("input close failed");
}

static uint16_t checksum(const uint8_t *data)
{
	uint16_t sum = 0;
	for (unsigned i = 0; i < SIZE; i += 2)
		sum ^= data[i] | (data[i + 1] << 8);
	return sum;
}

static void update_checksum(uint8_t *data)
{
	data[10] = data[11] = 0;
	uint16_t sum = checksum(data) ^ 0xffff;
	data[10] = sum & 0xff;
	data[11] = sum >> 8;
}

static void radio_mac(uint8_t *out, unsigned add)
{
	memcpy(out, mac, 6);
	for (int i = 5; i >= 0 && add; i--) {
		add += out[i];
		out[i] = add & 0xff;
		add >>= 8;
	}
	if (add || (out[0] & 1))
		fail("factory MAC range overflows");
}

static void le32(uint8_t *p, unsigned n)
{
	for (unsigned i = 0; i < 4; i++)
		p[i] = (n >> (i * 8)) & 0xff;
}

static size_t board_entry(uint8_t *p, const char *name, const uint8_t *data)
{
	size_t len = strlen(name), padded = (len + 3) & ~3;
	le32(p, 0); le32(p + 4, 8 + padded + 8 + SIZE);
	le32(p + 8, 0); le32(p + 12, len);
	memcpy(p + 16, name, len);
	memset(p + 16 + len, 0x6d, padded - len);
	le32(p + 16 + padded, 1); le32(p + 20 + padded, SIZE);
	memcpy(p + 24 + padded, data, SIZE);
	return 24 + padded + SIZE;
}

static void write_new(const char *root, const char *name, const void *data, size_t size)
{
	char dest[4096];
	path(dest, sizeof(dest), root, name);
	int fd = open(dest, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0)
		fail("cannot create output");
	size_t done = 0;
	while (done < size) {
		ssize_t n = write(fd, (const uint8_t *)data + done, size - done);
		if (n <= 0)
			fail("output write failed");
		done += n;
	}
	if (close(fd))
		fail("output close failed");
}

int main(int argc, char **argv)
{
	char name[4096], product[4097];
	if (argc != 5)
		fail("usage: xe75-factory-data ART factory-directory firmware-directory NEW-output-directory");
	path(name, sizeof(name), argv[2], "product-info");
	int fd = open(name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	ssize_t n = fd < 0 ? -1 : read(fd, product, sizeof(product) - 1);
	if (n <= 0 || n >= (ssize_t)sizeof(product) - 1 || close(fd))
		fail("factory product information unavailable");
	product[n] = 0;
	if (!strstr(product, "\nproduct_name:XE75\n") ||
	    !strstr(product, "\nproduct_ver:3.0.0\n") ||
	    !strstr(product, "\nspecial_id:45550000\n"))
		fail("only XE75 EU v3 factory layout is supported");
	path(name, sizeof(name), argv[2], "default-mac");
	read_exact(name, mac, 6, 0, 6);
	if ((mac[0] & 1) || !memcmp(mac, "\0\0\0\0\0\0", 6))
		fail("invalid factory MAC");
	/* Validate every input before creating any output. */
	for (unsigned i = 0; i < 3; i++) {
		read_exact(argv[1], cal[i], SIZE, offsets[i], 0x100000);
		path(name, sizeof(name), argv[3], boards[i]);
		read_exact(name, board[i], SIZE, 0, SIZE);
		if (checksum(cal[i]) != 0xffff || checksum(board[i]) != 0xffff ||
		    memcmp(cal[i], "\1\0\4\4", 4) || cal[i][8] != 0 || cal[i][9] != 0x80 ||
		    board[i][0x45c] != 3)
			fail("unsupported or damaged radio data");
		/* Firmware HK2.7 schema compatibility, preserving RF tables. */
		board[i][0x45c] = 0;
		if (i == 2) {
			static const uint8_t de[] = {0x14,1,0x37,0,2,0x44,0x45,0,0x44,0x45,0,0x28,0xa0,0,0,0};
			static const uint8_t es[] = {0xd4,2,0x37,0,0,0x45,0x53,0,0x45,0x53,0,0x28,0xa0,0,0,0};
			if (memcmp(board[i] + 0x12baa, de, sizeof(de)) ||
			    memcmp(board[i] + 0x1322a, es, sizeof(es)))
				fail("unsupported EU six-GHz regulatory table");
			/* Spain uses this EU board's existing ETSI domain; no power edits. */
			board[i][0x1322e] = board[i][0x12bae];
		}
		update_checksum(board[i]);
		/* OEM identities: label +2/+3/+4 for 2.4/5/6 GHz. */
		radio_mac(cal[i] + 0xe, i + 2);
		cal[i][0x3e] = 1; cal[i][0x3f] = 0;
		update_checksum(cal[i]);
	}
	uint8_t dual[2 * SIZE + 512] = "QCA-ATH11K-BOARD";
	memset(dual + 17, 0x6d, 3);
	size_t len = 20;
	len += board_entry(dual + len,
		"bus=ahb,qmi-chip-id=0,qmi-board-id=255,variant=TP-Link-Deco-XE75-v3-5G", board[1]);
	len += board_entry(dual + len,
		"bus=ahb,qmi-chip-id=0,qmi-board-id=255,variant=TP-Link-Deco-XE75-v3-6G", board[2]);
	if (mkdir(argv[4], 0700))
		fail("output directory must be new");
	const char *dirs[] = {"IPQ5018", "IPQ5018/hw1.0", "QCN6122", "QCN6122/hw1.0"};
	for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
		path(name, sizeof(name), argv[4], dirs[i]);
		if (mkdir(name, 0700))
			fail("cannot create staging directory");
	}
	for (unsigned i = 0; i < 3; i++)
		write_new(argv[4], calnames[i], cal[i], SIZE);
	write_new(argv[4], "IPQ5018/hw1.0/board.bin", board[0], SIZE);
	write_new(argv[4], "QCN6122/hw1.0/board-2.bin", dual, len);
	write_new(argv[4], "default-mac", mac, 6);
	puts("Validated factory radio data staged; original inputs were read only.");
	return 0;
}
