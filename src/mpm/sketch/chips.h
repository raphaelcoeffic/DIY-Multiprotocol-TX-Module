#pragma once

// The RF chips of the MULTI-Module, emulated on the radio: the interfaces of
// the protocol files (registers, constants: Multiprotocol/iface_*.h) and the
// functions of *_SPI.ino, mpm/multi/a7105.c, cc2500.c, cyrf6936.c and
// nrf24l01.c. The XN297 emulation is mpm/multi/xn297.h. mpm-v2 has its own
// chips.h (mpm/upstream), on its copies of iface_*.h

#include "mpm_compat.h"

#include "iface_a7105.h"
#include "iface_cc2500.h"
#include "iface_cyrf6936.h"
#include "iface_nrf24l01.h"

// A7105_SPI.ino on the CC26xx (a7105.c)
void A7105_WriteData(uint8_t len, uint8_t channel);
void A7105_ReadData(uint8_t len);
void A7105_WriteReg(uint8_t address, uint8_t data);
uint8_t A7105_ReadReg(uint8_t address);
void A7105_SetTxRxMode(uint8_t mode);
uint8_t A7105_Reset();
void A7105_WriteID(uint32_t ida);
void A7105_SetPower();
void A7105_Strobe(uint8_t address);
void A7105_AdjustLOBaseFreq(uint8_t cmd);
void A7105_Init(void);

// CC2500_SPI.ino on the CC26xx (cc2500.c)
void CC2500_WriteReg(uint8_t address, uint8_t data);
uint8_t CC2500_ReadReg(uint8_t address);
void CC2500_WriteRegisterMulti(uint8_t address, const uint8_t data[], uint8_t length);
void CC2500_ReadRegisterMulti(uint8_t address, uint8_t data[], uint8_t length);
void CC2500_ReadData(uint8_t *dpbuffer, uint8_t len);
void CC2500_WriteData(uint8_t *dpbuffer, uint8_t len);
void CC2500_Strobe(uint8_t state);
void CC2500_SetTxRxMode(uint8_t mode);
uint8_t CC2500_Reset();
void CC2500_SetPower();
void CC2500_SetFreqOffset();

// CYRF6936_SPI.ino on the CC26xx (cyrf6936.c)
void CYRF_WriteRegister(uint8_t address, uint8_t data);
uint8_t CYRF_ReadRegister(uint8_t address);
uint8_t CYRF_Reset();
void CYRF_Idle();  // between protocol callbacks
void CYRF_GetMfgData(uint8_t data[]);
void CYRF_SetTxRxMode(uint8_t mode);
void CYRF_ConfigRFChannel(uint8_t ch);
void CYRF_SetPower(uint8_t val);
void CYRF_ConfigCRCSeed(uint16_t crc_seed);
void CYRF_ConfigSOPCode(const uint8_t *sopcodes);
void CYRF_ConfigDataCode(const uint8_t *datacodes);
void CYRF_WritePreamble(uint32_t preamble);
void CYRF_ReadDataPacketLen(uint8_t dpbuffer[], uint8_t length);
void CYRF_WriteDataPacketLen(const uint8_t dpbuffer[], uint8_t len);
void CYRF_WriteDataPacket(const uint8_t dpbuffer[]);
void CYRF_FindBestChannels(uint8_t *channels, uint8_t len, uint8_t minspace, uint8_t min, uint8_t max, uint8_t forced);

// NRF24l01_SPI.ino on the CC26xx (nrf24l01.c)
// Multiprotocol.h
enum {
	PKT_PENDING = 0,
	PKT_ACKED,
	PKT_TIMEOUT
};

extern uint8_t rf_setup;

void NRF24L01_Initialize();
void NRF24L01_WriteReg(uint8_t reg, uint8_t data);
void NRF24L01_WriteRegisterMulti(uint8_t reg, uint8_t* data, uint8_t length);
void NRF24L01_WritePayload(uint8_t* data, uint8_t length);
uint8_t NRF24L01_ReadReg(uint8_t reg);
void NRF24L01_ReadPayload(uint8_t* data, uint8_t length);
void NRF24L01_Strobe(uint8_t state);
void NRF24L01_FlushTx();
void NRF24L01_FlushRx();
uint8_t NRF24L01_GetDynamicPayloadSize();
void NRF24L01_SetBitrate(uint8_t bitrate);
void NRF24L01_SetPower();
void NRF24L01_SetTxRxMode(enum TXRX_State mode);
void NRF24L01_Reset();
uint8_t NRF24L01_packet_ack();
