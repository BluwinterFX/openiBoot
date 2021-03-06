#include "openiboot.h"
#include "commands.h"
#include "nor.h"
#include "hardware/nor.h"
#include "util.h"
#include "timer.h"
#include "spi.h"
#include "gpio.h"
#include "hardware/spi.h"

static int NORSectorSize = 4096;
static int Prepared = 0;
static int WritePrepared = FALSE;
static int NORVendor;

static void nor_prepare()
{
	if(Prepared == 0)
		spi_set_baud(0, 12000000, SPIWordSize8, 1, 0, 0);

	Prepared++;
}

static void nor_unprepare()
{
	Prepared--;
}

static void probeNOR()
{
	nor_prepare();

	uint8_t command = NOR_SPI_JEDECID;
	uint8_t deviceID[3];
	memset(deviceID, 0, sizeof(deviceID));

	gpio_pin_output(NOR_CS, 0);
	spi_tx(0, &command, 1, TRUE, 0);
	spi_rx(0, deviceID, 3, TRUE, 0);
	gpio_pin_output(NOR_CS, 1);
	uint16_t vendor = deviceID[0];
	uint16_t device = deviceID[2];

	// Unprotect NOR
	command = NOR_SPI_EWSR;
	gpio_pin_output(NOR_CS, 0);
	spi_tx(0, &command, 1, TRUE, 0);
	gpio_pin_output(NOR_CS, 1);

	uint8_t wrsrCommand[2] = {NOR_SPI_WRSR, 0};
	gpio_pin_output(NOR_CS, 0);
	spi_tx(0, wrsrCommand, 2, TRUE, 0);
	gpio_pin_output(NOR_CS, 1);

	NORVendor = vendor;

	bufferPrintf("NOR vendor=%x, device=%x\r\n", (uint32_t) vendor, (uint32_t) device);

	nor_unprepare();
}

static uint8_t nor_get_status() {
	nor_prepare();
	uint8_t data;

	uint8_t command[1];
	command[0] = NOR_SPI_RDSR;

	gpio_pin_output(NOR_CS, 0);
	spi_tx(0, command, sizeof(command), TRUE, 0);
	spi_rx(0, &data, sizeof(data), TRUE, 0);
	gpio_pin_output(NOR_CS, 1);

	nor_unprepare();

	return data;
}

static int nor_wait_for_ready(int timeout) {
	if((nor_get_status() & (1 << NOR_SPI_SR_BUSY)) == 0) {
		return 0;
	}

	uint64_t startTime = timer_get_system_microtime();
	while((nor_get_status() & (1 << NOR_SPI_SR_BUSY)) != 0) {
		if(has_elapsed(startTime, timeout * 1000)) {
			bufferPrintf("nor: timed out waiting for ready\r\n");
			return -1;
		}
	}

	return 0;
}

static void nor_write_enable() {
	nor_prepare();

	if(nor_wait_for_ready(100) != 0) {
		nor_unprepare();
		return;
	}

	uint8_t command[1];
	command[0] = NOR_SPI_WREN;

	gpio_pin_output(NOR_CS, 0);
	spi_tx(0, command, sizeof(command), TRUE, 0);
	gpio_pin_output(NOR_CS, 1);

	nor_unprepare();
}

static void nor_write_disable() {
	nor_prepare();

	if(nor_wait_for_ready(100) != 0) {
		nor_unprepare();
		return;
	}

	uint8_t command[1];
	command[0] = NOR_SPI_WRDI;

	gpio_pin_output(NOR_CS, 0);
	spi_tx(0, command, sizeof(command), TRUE, 0);
	gpio_pin_output(NOR_CS, 1);

	nor_unprepare();

	WritePrepared = FALSE;
}

static int nor_serial_prepare_write(uint32_t offset, uint16_t data) {
	nor_prepare();

	if(nor_wait_for_ready(100) != 0) {
		nor_unprepare();
		return -1;
	}

	nor_write_enable();

	uint8_t command[6];
	command[0] = NOR_SPI_AIPG;
	command[1] = (offset >> 16) & 0xFF;
	command[2] = (offset >> 8) & 0xFF;
	command[3] = offset & 0xFF;
	command[4] = data & 0xFF;
	command[5] = (data >> 8) & 0xFF;

	gpio_pin_output(NOR_CS, 0);
	spi_tx(0, command, sizeof(command), TRUE, 0);
	gpio_pin_output(NOR_CS, 1);

	nor_unprepare();

	WritePrepared = TRUE;
	
	return 0;
}

static int nor_serial_write_byte(uint32_t offset, uint8_t data) {
	nor_prepare();

	if(nor_wait_for_ready(100) != 0) {
		nor_unprepare();
		return -1;
	}

	nor_write_enable();

	uint8_t command[5];
	command[0] = NOR_SPI_PRGM;
	command[1] = (offset >> 16) & 0xFF;
	command[2] = (offset >> 8) & 0xFF;
	command[3] = offset & 0xFF;
	command[4] = data & 0xFF;

	gpio_pin_output(NOR_CS, 0);
	spi_tx(0, command, sizeof(command), TRUE, 0);
	gpio_pin_output(NOR_CS, 1);

	nor_unprepare();

	return 0;
}

int nor_write_word(uint32_t offset, uint16_t data) {
	nor_prepare();
	
	if(NORVendor == 0xBF)
	{
		// SST has the auto-increment program opcode that lets programming happen more quickly
		if(!WritePrepared) {
			nor_serial_prepare_write(offset, data);
		} else {
			if(nor_wait_for_ready(100) != 0) {
				nor_unprepare();
				return -1;
			}

			uint8_t command[3];
			command[0] = NOR_SPI_AIPG;
			command[1] = data & 0xFF;
			command[2] = (data >> 8) & 0xFF;

			gpio_pin_output(NOR_CS, 0);
			spi_tx(0, command, sizeof(command), TRUE, 0);
			gpio_pin_output(NOR_CS, 1);
		}
	} else
	{
		nor_serial_write_byte(offset, data & 0xFF);
		nor_serial_write_byte(offset + 1, (data >> 8) & 0xFF);
	}

	nor_unprepare();

	return 0;
}

uint16_t nor_read_word(uint32_t offset) {
	uint16_t data;
	nor_prepare();

	uint8_t command[4];
	command[0] = NOR_SPI_READ;
	command[1] = (offset >> 16) & 0xFF;
	command[2] = (offset >> 8) & 0xFF;
	command[3] = offset & 0xFF;

	gpio_pin_output(NOR_CS, 0);
	spi_tx(0, command, sizeof(command), TRUE, 0);
	spi_rx(0, (uint8_t*) &data, sizeof(data), TRUE, 0);
	gpio_pin_output(NOR_CS, 1);

	nor_unprepare();

	return data;
}

int nor_erase_sector(uint32_t offset) {
	nor_prepare();

	if(nor_wait_for_ready(100) != 0) {
		nor_unprepare();
		return -1;
	}

	nor_write_enable();

	uint8_t command[4];
	command[0] = NOR_SPI_ERSE_4KB;
	command[1] = (offset >> 16) & 0xFF;
	command[2] = (offset >> 8) & 0xFF;
	command[3] = offset & 0xFF;

	gpio_pin_output(NOR_CS, 0);
	spi_tx(0, command, sizeof(command), TRUE, 0);
	gpio_pin_output(NOR_CS, 1);

	nor_write_disable();

	nor_unprepare();

	return 0;
}

void nor_read(void* buffer, int offset, int len) {
	nor_prepare();
	uint8_t command[4];
	uint8_t* data = buffer;
	while(len > 0) {
		int toRead = (len > 0x10) ? 0x10 : len;

		command[0] = NOR_SPI_READ;
		command[1] = (offset >> 16) & 0xFF;
		command[2] = (offset >> 8) & 0xFF;
		command[3] = offset & 0xFF;

		gpio_pin_output(NOR_CS, 0);
		spi_tx(0, command, sizeof(command), TRUE, 0);
		if(spi_rx(0, data, toRead, TRUE, 0) < 0)
		{
			gpio_pin_output(NOR_CS, 1);
			continue;
		}
		gpio_pin_output(NOR_CS, 1);

		len -= toRead;
		data += toRead;
		offset += toRead;
	}

	nor_unprepare();
}

int nor_write(void* buffer, int offset, int len) {
	nor_prepare();

	int startSector = offset / NORSectorSize;
	int endSector = (offset + len) / NORSectorSize;

	int numSectors = endSector - startSector + 1;
	uint8_t* sectorsToChange = (uint8_t*) malloc(NORSectorSize * numSectors);
	nor_read(sectorsToChange, startSector * NORSectorSize, NORSectorSize * numSectors);

	int offsetFromStart = offset - (startSector * NORSectorSize);

	memcpy(sectorsToChange + offsetFromStart, buffer, len);

	int i;
	for(i = 0; i < numSectors; i++) {
		if(nor_erase_sector((i + startSector) * NORSectorSize) != 0) {
			nor_unprepare();
			return -1;
		}

		int j;
		uint16_t* curSector = (uint16_t*)(sectorsToChange + (i * NORSectorSize));
		for(j = 0; j < (NORSectorSize / 2); j++) {
			if(nor_write_word(((i + startSector) * NORSectorSize) + (j * 2), curSector[j]) != 0) {
				nor_unprepare();
				nor_write_disable();
				return -1;
			}
		}
		nor_write_disable();
	}

	free(sectorsToChange);

	nor_unprepare();

	return 0;
}

int getNORSectorSize() {
	return NORSectorSize;
}

int nor_setup() {
	probeNOR();

	return 0;
}

void cmd_nor_read(int argc, char** argv) {
    if(argc < 4) {
        bufferPrintf("Usage: %s <address> <offset> <len>\r\n", argv[0]);
        return;
    }
    
    uint32_t address = parseNumber(argv[1]);
    uint32_t offset = parseNumber(argv[2]);
    uint32_t len = parseNumber(argv[3]);
    bufferPrintf("Reading 0x%x - 0x%x to 0x%x...\r\n", offset, offset + len, address);
    nor_read((void*)address, offset, len);
    bufferPrintf("Done.\r\n");
}
COMMAND("nor_read", "read a block of NOR into RAM", cmd_nor_read);

void cmd_nor_write(int argc, char** argv) {
	if(argc < 4) {
		bufferPrintf("Usage: %s <address> <offset> <len>\r\n", argv[0]);
		return;
	}

	uint32_t address = parseNumber(argv[1]);
	uint32_t offset = parseNumber(argv[2]);
	uint32_t len = parseNumber(argv[3]);
	bufferPrintf("Writing 0x%x - 0x%x to 0x%x...\r\n", address, address + len, offset);
	nor_write((void*)address, offset, len);
	bufferPrintf("Done.\r\n");
}
COMMAND("nor_write", "write RAM into NOR", cmd_nor_write);

void cmd_nor_erase(int argc, char** argv) {
	if(argc < 3) {
		bufferPrintf("Usage: %s <address> <address again for confirmation>\r\n", argv[0]);
		return;
	}
	
	uint32_t addr1 = parseNumber(argv[1]);
	uint32_t addr2 = parseNumber(argv[2]);

	if(addr1 != addr2) {
		bufferPrintf("0x%x does not match 0x%x\r\n", addr1, addr2);
		return;
	}

	bufferPrintf("Erasing 0x%x - 0x%x...\r\n", addr1, addr1 + getNORSectorSize());
	nor_erase_sector(addr1);
	bufferPrintf("Done.\r\n");
}
COMMAND("nor_erase", "erase a block of NOR", cmd_nor_erase);

