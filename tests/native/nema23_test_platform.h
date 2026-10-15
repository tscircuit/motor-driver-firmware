#ifndef NEMA23_TEST_PLATFORM_H
#define NEMA23_TEST_PLATFORM_H
#include "test_platform.h"
#define i2c0 0
#define spi0 0
#define GPIO_FUNC_SPI 1
#define SPI_CPOL_0 0
#define SPI_CPHA_1 1
#define SPI_MSB_FIRST 0
void spi_init(int port, uint32_t hz);
void spi_set_format(int port,int bits,int cpol,int cpha,int order);
int spi_write_read_blocking(int port,const uint8_t *tx,uint8_t *rx,size_t n);
#endif
