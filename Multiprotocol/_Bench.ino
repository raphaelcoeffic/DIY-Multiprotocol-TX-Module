/*
 This project is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Multiprotocol is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Multiprotocol.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
 Bench mode: drive the RF chips directly from a host over the debug serial port
 (native USB or FTDI) to test over-the-air interoperability with other hardware.

 Build: STM32 board with a debug option + MULTI_BENCH defined (see _Bench_Config.h).
 The module does not wait for a TX: loop() hands over to Bench_loop() at boot.

 Protocol: one ASCII command per line, numbers are decimal or 0x-prefixed hex,
 <hex> is a byte string like "0a1b2c" or "0a 1b 2c".
 Every command answers with one "@ok ..." or "@err ..." line, events are sent as
 "@rx ..." / "@ev ..." lines. Any other line is debug output and can be ignored.

   info                         chip presence, current chip and job
   chip <cc2500|nrf|a7105|cyrf> select a chip: RF switch + reset of this chip
   preset <name> [arg]          load a known-good config from an existing protocol
                                  cc2500: frskyd frskyx frskyxeu 250k  (arg=freq offset)
                                  a7105:  afhds2a flysky hubsan bugs height height8 joysway kyosho
                                          kyoshohype pelikan wfly2 (A7105_Init() table of the protocol)
                                  cyrf:   dsm (data packets: 8DR, SOP, length, CRC; PN row 0, SOP column 0,
                                          CRC seed 0), dsmbind (SDR, no SOP / length / CRC)
                                  nrf:    nrf
   xn <crc> <scramble> <250k> [force_nrf] [enhanced]   select the XN297 emulation (NRF or CC2500),
                                 enhanced: packet control field (unscrambled: TX CRC without the
                                 xorout of the RX side, see XN297_EMU.ino)
   xaddr <hex>                  XN297 TX and RX address (3..5 bytes)
   w <reg> <hex>                write register(s), burst if several bytes
                                  (cyrf: no address increment, all bytes go to reg)
   r <reg> [n]                  read n register(s)               -> @ok <hex>
   s <cmd>                      strobe / command byte
   ch <n>                       RF channel
   tx <hex>                     send one packet
   txp <period_us> <count> <hex>  send periodically, count=0 forever -> @ev txdone <n>
   seq <idx>                    put an 8 bit counter at byte idx of txp packets (255=off)
   rx [len]                     receive continuously -> @rx <us> <ch> <hex> [crc=ok|bad] [rpd=0|1]
                                  cc2500: len=bytes to read from FIFO (incl. appended status),
                                          0=read the FIFO when the radio goes back to IDLE
                                  nrf:    len=payload size, 0=RX_PW_P0 or dynamic payload
                                  a7105:  len=payload size (required, the FIFO length register is write only)
                                  xn297:  len=payload size (required)
                                  cyrf:   len ignored, RX_COUNT bytes are read
   stop                         stop the current job             -> @ok stop tx=<n> rx=<n> err=<n> [out_drop=<n>]
   chan <i> <value>             Channel_data[i] (0..2047) used by "proto"
   proto <num> [sub] [rxnum] [option] [bind]  run a built-in protocol of the table
   user [hex]                   run Bench_user_init(hex)/Bench_user_callback() from _Bench_User.ino

 Host tool, build script, examples, interoperability tests and results:
 https://github.com/raphaelcoeffic/mpmv2, docs/bench.md
*/

#if defined(MULTI_BENCH)

#include <stdarg.h>
#include <libmaple/usb_cdcacm.h>
#include "iface_xn297.h"
#ifdef A7105_INSTALLED
	#include "iface_a7105.h"
#endif
#ifdef CYRF6936_INSTALLED
	#include "iface_cyrf6936.h"
#endif

#define BENCH_VERSION		1
#define BENCH_LINE_MAX		200
#define BENCH_PKT_MAX		64
#define BENCH_RX_POLL		200			// us between RX polls
#define BENCH_MAX_CB		16000		// us, doubled in a 16 bit timer: must stay below 0x8000 ticks
#define BENCH_USB_POLL		5			// ms, max time between two command polls
#define BENCH_A7105_SETTLE	100			// us after the RX strobe before the A7105 state is valid

// RX flags
#define BENCH_RX_CRC		0x01		// CRC status is known
#define BENCH_RX_CRC_OK		0x02
#define BENCH_RX_RPD		0x04		// NRF received power detector

enum BENCH_CHIP {
	BENCH_NONE		= 0,
	BENCH_CC2500	= 1,
	BENCH_NRF		= 2,
	BENCH_A7105		= 3,
	BENCH_XN297		= 4,
	BENCH_CYRF		= 5,
};
enum BENCH_JOB {
	BENCH_IDLE	= 0,
	BENCH_TX	= 1,
	BENCH_RX	= 2,
	BENCH_PROTO	= 3,
	BENCH_USER	= 4,
};

const char * const bench_chip_names[] = { "none", "cc2500", "nrf", "a7105", "xn297", "cyrf" };
const char * const bench_job_names[] = { "idle", "tx", "rx", "proto", "user" };

uint8_t bench_chip = BENCH_NONE;
uint8_t bench_job = BENCH_IDLE;
uint8_t bench_rf_ch = 0;
uint16_function_t bench_callback = 0;

static uint8_t bench_tx_buf[BENCH_PKT_MAX], bench_tx_len;
static uint32_t bench_tx_period, bench_tx_left, bench_tx_count;
static uint8_t bench_seq_idx = 0xFF, bench_seq;
static uint8_t bench_rx_len;
static uint32_t bench_rx_armed;
static uint8_t bench_xn_addr[5], bench_xn_addr_len;
static bool bench_xn_enhanced;
static uint8_t bench_ack_pl[32], bench_ack_pl_len;
static uint32_t bench_stat_tx, bench_stat_rx, bench_stat_err;

static char bench_line[BENCH_LINE_MAX];
static uint8_t bench_line_len;
static char bench_out[96];

// Host output goes through a RAM ring drained without blocking. Serial.write()
// waits for room in the 256 byte USB buffer: when the host is slow to fetch
// it (23 ms measured), the RX polling stopped with it and the chips missed
// packets. A line that doesn't fit is dropped whole and counted (out_drop).
#define BENCH_OUT_SIZE		2048		// power of 2
static uint8_t bench_out_ring[BENCH_OUT_SIZE];
static uint16_t bench_out_head, bench_out_tail, bench_out_line;
static bool bench_out_dropping;
static uint32_t bench_out_drop;

// RF activity LEDs: each packet sent lights LED, each packet received LED2
// (PA2, also the USART2 TX of the telemetry, unused in bench mode) for
// BENCH_LED_HOLD ms: a packet stream looks steady, a single packet still shows.
#define BENCH_LED_HOLD		20			// ms
static uint32_t bench_led_tx_end, bench_led_rx_end;
static bool bench_led_tx_on, bench_led_rx_on;

static void bench_led_init()
{
	LED_output;
	LED_off;
	LED2_output;
	LED2_off;
}

static void bench_led_tx()
{
	LED_on;
	bench_led_tx_on = true;
	bench_led_tx_end = millis() + BENCH_LED_HOLD;
}

static void bench_led_rx()
{
	LED2_on;
	bench_led_rx_on = true;
	bench_led_rx_end = millis() + BENCH_LED_HOLD;
}

static void bench_led_poll()
{
	uint32_t now = millis();
	if(bench_led_tx_on && (int32_t)(now - bench_led_tx_end) >= 0)
	{
		LED_off;
		bench_led_tx_on = false;
	}
	if(bench_led_rx_on && (int32_t)(now - bench_led_rx_end) >= 0)
	{
		LED2_off;
		bench_led_rx_on = false;
	}
}

//*************************
//***  Output helpers   ***
//*************************
static void bench_write(const uint8_t *buf, uint16_t len)
{
	bool eol = memchr(buf, '\n', len) != NULL;
	if(!bench_out_dropping)
	{
		if(len < ((bench_out_tail - bench_out_head - 1) & (BENCH_OUT_SIZE - 1)))
		{
			for(uint16_t i = 0; i < len; i++)
			{
				bench_out_ring[bench_out_head] = buf[i];
				bench_out_head = (bench_out_head + 1) & (BENCH_OUT_SIZE - 1);
			}
			if(eol)
				bench_out_line = bench_out_head;
			return;
		}
		bench_out_head = bench_out_line;	// No room: drop the line, its start too
		bench_out_dropping = true;
		bench_out_drop++;
	}
	if(eol)
		bench_out_dropping = false;
}

static void bench_puts(const char *str)
{
	bench_write((const uint8_t*)str, strlen(str));
}

static void bench_out_flush()
{
	while(bench_out_tail != bench_out_line)
	{ // Whole lines only: the one being written may still be dropped
		uint16_t end = bench_out_line > bench_out_tail ? bench_out_line : BENCH_OUT_SIZE;
		uint32_t n = usb_cdcacm_tx(bench_out_ring + bench_out_tail, end - bench_out_tail);
		if(n == 0)
			return;
		bench_out_tail = (bench_out_tail + n) & (BENCH_OUT_SIZE - 1);
	}
}

void bench_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(bench_out, sizeof(bench_out), fmt, ap);
	va_end(ap);
	bench_puts(bench_out);
}

void bench_print_hex(const uint8_t *buf, uint8_t len)
{
	static const char hex[] = "0123456789abcdef";
	char out[2];
	for(uint8_t i = 0; i < len; i++)
	{
		out[0] = hex[buf[i] >> 4];
		out[1] = hex[buf[i] & 0x0F];
		bench_write((const uint8_t*)out, 2);
	}
}

void bench_report_rx(const uint8_t *buf, uint8_t len, uint8_t flags)
{
	bench_led_rx();
	bench_printf("@rx %lu %u ", micros(), bench_rf_ch);
	bench_print_hex(buf, len);
	if(flags & BENCH_RX_CRC)
		bench_puts((flags & BENCH_RX_CRC_OK) ? " crc=ok" : " crc=bad");
	if(bench_chip == BENCH_NRF)
		bench_puts((flags & BENCH_RX_RPD) ? " rpd=1" : " rpd=0");
	bench_puts("\r\n");
}

//*************************
//***  Parsing helpers  ***
//*************************
static char *bench_token(char **p)
{
	char *s = *p;
	while(*s == ' ' || *s == '\t') s++;
	if(*s == 0) return 0;
	char *tok = s;
	while(*s && *s != ' ' && *s != '\t') s++;
	if(*s) *s++ = 0;
	*p = s;
	return tok;
}

static bool bench_number(char **p, uint32_t *val)
{
	char *tok = bench_token(p);
	if(tok == 0) return false;
	char *end;
	*val = strtoul(tok, &end, 0);
	return *end == 0;
}

static uint32_t bench_number_or(char **p, uint32_t def)
{
	uint32_t val;
	return bench_number(p, &val) ? val : def;
}

static int8_t bench_hex_digit(char c)
{
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// Parse the rest of the line as hex bytes, spaces and 0x prefixes allowed between bytes
static int16_t bench_hex(char **p, uint8_t *buf, uint8_t max)
{
	char *s = *p;
	uint8_t len = 0;
	bool token_start = true;
	while(*s)
	{
		if(*s == ' ' || *s == '\t') { s++; token_start = true; continue; }
		if(token_start && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
		token_start = false;
		int8_t hi = bench_hex_digit(s[0]);
		int8_t lo = hi < 0 ? -1 : bench_hex_digit(s[1]);
		if(lo < 0 || len >= max) return -1;
		buf[len++] = (hi << 4) | lo;
		s += 2;
	}
	*p = s;
	return len;
}

//****************************
//***  Chip level helpers  ***
//****************************
static bool bench_chip_reset(uint8_t chip)
{
	switch(chip)
	{
		#ifdef CC2500_INSTALLED
			case BENCH_CC2500:
				rf_switch(SW_CC2500);
				return CC2500_Reset();
		#endif
		#ifdef NRF24L01_INSTALLED
			case BENCH_NRF:
				rf_switch(SW_NRF);
				NRF24L01_Reset();
				// No reset detection on the NRF: check that a register can be written
				NRF24L01_WriteReg(NRF24L01_03_SETUP_AW, 0x02);
				if(NRF24L01_ReadReg(NRF24L01_03_SETUP_AW) != 0x02)
					return false;
				NRF24L01_WriteReg(NRF24L01_03_SETUP_AW, 0x03);
				return NRF24L01_ReadReg(NRF24L01_03_SETUP_AW) == 0x03;
		#endif
		#ifdef A7105_INSTALLED
			case BENCH_A7105:
				rf_switch(SW_A7105);
				return A7105_Reset();
		#endif
		#ifdef CYRF6936_INSTALLED
			case BENCH_CYRF:
				rf_switch(SW_CYRF);
				return CYRF_Reset();
		#endif
	}
	return false;
}

static void bench_write_regs(uint8_t reg, const uint8_t *data, uint8_t len)
{
	switch(bench_chip)
	{
		#ifdef CC2500_INSTALLED
			case BENCH_CC2500:
				if(len == 1)
					CC2500_WriteReg(reg, data[0]);
				else
					CC2500_WriteRegisterMulti(reg, data, len);
				break;
		#endif
		#ifdef NRF24L01_INSTALLED
			case BENCH_NRF:
				NRF24L01_WriteRegisterMulti(reg, (uint8_t*)data, len);
				break;
		#endif
		#ifdef A7105_INSTALLED
			case BENCH_A7105:
				A7105_CSN_off;
				SPI_Write(reg);
				for(uint8_t i = 0; i < len; i++)
					SPI_Write(data[i]);
				A7105_CSN_on;
				break;
		#endif
		#ifdef CYRF6936_INSTALLED
			case BENCH_CYRF:	// no address increment: all bytes go to reg (0x20..0x25 buffers: write them whole)
				CYRF_WriteRegisterMulti(reg, data, len);
				break;
		#endif
	}
}

static void bench_read_regs(uint8_t reg, uint8_t *data, uint8_t len)
{
	switch(bench_chip)
	{
		#ifdef CC2500_INSTALLED
			case BENCH_CC2500:
				if(len == 1 && reg < 0x30)
					data[0] = CC2500_ReadReg(reg);
				else // status registers need the burst bit
					CC2500_ReadRegisterMulti(reg, data, len);
				break;
		#endif
		#ifdef NRF24L01_INSTALLED
			case BENCH_NRF:
				NRF_CSN_off;
				SPI_Write(R_REGISTER | (REGISTER_MASK & reg));
				for(uint8_t i = 0; i < len; i++)
					data[i] = SPI_Read();
				NRF_CSN_on;
				break;
		#endif
		#ifdef A7105_INSTALLED
			case BENCH_A7105:
				A7105_CSN_off;
				SPI_Write(reg | 0x40);				// bit 6 =1 for reading
				for(uint8_t i = 0; i < len; i++)
					data[i] = SPI_SDI_Read();
				A7105_CSN_on;
				break;
		#endif
		#ifdef CYRF6936_INSTALLED
			case BENCH_CYRF:
				CYRF_ReadRegisterMulti(reg, data, len);
				break;
		#endif
	}
}

static void bench_strobe(uint8_t cmd)
{
	switch(bench_chip)
	{
		#ifdef CC2500_INSTALLED
			case BENCH_CC2500:	CC2500_Strobe(cmd);		break;
		#endif
		#ifdef NRF24L01_INSTALLED
			case BENCH_NRF:		NRF24L01_Strobe(cmd);	break;
		#endif
		#ifdef A7105_INSTALLED
			case BENCH_A7105:	A7105_Strobe(cmd);		break;
		#endif
	}
}

#ifdef NRF24L01_INSTALLED
// NRF in RX with EN_AA and EN_ACK_PAY: payloads of the next acks (pipe 0),
// the byte at the 'seq' index is incremented on each one. The TX FIFO is kept
// full: the NRF drops a payload when the next packet comes, and refilling only
// then is too late for its ack at 1Mbps.
static void bench_nrf_load_ack()
{
	if(bench_ack_pl_len == 0)
		return;
	while(!(NRF24L01_ReadReg(NRF24L01_17_FIFO_STATUS) & 0x20))	// TX_FULL
	{
		NRF_CSN_off;
		SPI_Write(NRF24L01_A8_W_ACK_PAYLOAD0);
		for(uint8_t i = 0; i < bench_ack_pl_len; i++)
			SPI_Write(bench_ack_pl[i]);
		NRF_CSN_on;
		if(bench_seq_idx < bench_ack_pl_len)
			bench_ack_pl[bench_seq_idx]++;
	}
}
#endif

void bench_set_channel(uint8_t ch)
{
	bench_rf_ch = ch;
	switch(bench_chip)
	{
		#ifdef CC2500_INSTALLED
			case BENCH_CC2500:
				CC2500_Strobe(CC2500_SIDLE);
				CC2500_WriteReg(CC2500_0A_CHANNR, ch);
				break;
		#endif
		#ifdef NRF24L01_INSTALLED
			case BENCH_NRF:
				NRF24L01_WriteReg(NRF24L01_05_RF_CH, ch);
				break;
		#endif
		#ifdef A7105_INSTALLED
			case BENCH_A7105:
				A7105_Strobe(A7105_STANDBY);
				A7105_WriteReg(A7105_0F_PLL_I, ch);
				break;
		#endif
		#ifdef CYRF6936_INSTALLED
			case BENCH_CYRF:
				CYRF_ConfigRFChannel(ch);
				break;
		#endif
		#if defined(CC2500_INSTALLED) || defined(NRF24L01_INSTALLED)
			case BENCH_XN297:
				XN297_RFChannel(ch);
				break;
		#endif
	}
}

// Switch the current chip to TX_EN, RX_EN or TXRX_OFF and start receiving for RX_EN
void bench_set_mode(uint8_t mode)
{
	switch(bench_chip)
	{
		#ifdef CC2500_INSTALLED
			case BENCH_CC2500:
				CC2500_Strobe(CC2500_SIDLE);
				CC2500_SetTxRxMode(mode);
				if(mode == RX_EN)
				{
					CC2500_Strobe(CC2500_SFRX);
					CC2500_Strobe(CC2500_SRX);
				}
				break;
		#endif
		#ifdef NRF24L01_INSTALLED
			case BENCH_NRF:
			{ // Keep the CRC configuration of the host, only change PWR_UP/PRIM_RX
				NRF24L01_WriteReg(NRF24L01_07_STATUS, 0x70);
				NRF_CE_off;
				uint8_t config = NRF24L01_ReadReg(NRF24L01_00_CONFIG) & ~(_BV(NRF24L01_00_PWR_UP) | _BV(NRF24L01_00_PRIM_RX));
				if(mode == TXRX_OFF)
				{
					NRF24L01_WriteReg(NRF24L01_00_CONFIG, config);
					break;
				}
				if(mode == TX_EN)
				{
					NRF24L01_FlushTx();
					NRF24L01_WriteReg(NRF24L01_00_CONFIG, config | _BV(NRF24L01_00_PWR_UP));
				}
				else
				{
					NRF24L01_FlushRx();
					NRF24L01_FlushTx();
					bench_nrf_load_ack();
					NRF24L01_WriteReg(NRF24L01_00_CONFIG, config | _BV(NRF24L01_00_PWR_UP) | _BV(NRF24L01_00_PRIM_RX));
				}
				delayMicroseconds(130);
				NRF_CE_on;
				break;
			}
		#endif
		#ifdef A7105_INSTALLED
			case BENCH_A7105:
				A7105_Strobe(A7105_STANDBY);
				A7105_SetTxRxMode(mode);
				if(mode == RX_EN)
				{
					A7105_WriteReg(A7105_0F_PLL_I, bench_rf_ch);
					A7105_Strobe(A7105_RX);
					bench_rx_armed = micros();
				}
				break;
		#endif
		#ifdef CYRF6936_INSTALLED
			case BENCH_CYRF:	// as the DSM telemetry: abort, forced end state, then RX_GO
				CYRF_WriteRegister(CYRF_29_RX_ABORT, 0x20);
				CYRF_SetTxRxMode(mode);
				CYRF_WriteRegister(CYRF_29_RX_ABORT, 0x00);
				if(mode == RX_EN)
					CYRF_WriteRegister(CYRF_05_RX_CTRL, 0x87);
				break;
		#endif
		#if defined(CC2500_INSTALLED) || defined(NRF24L01_INSTALLED)
			case BENCH_XN297:
				if(mode == RX_EN)
				{
					XN297_SetRXAddr(bench_xn_addr, bench_rx_len);
					XN297_SetTxRxMode(TXRX_OFF);
				}
				XN297_SetTxRxMode((enum TXRX_State)mode);
				break;
		#endif
	}
}

// Send one packet, the chip must be in TX mode
void bench_send(uint8_t *buf, uint8_t len)
{
	switch(bench_chip)
	{
		#ifdef CC2500_INSTALLED
			case BENCH_CC2500:
				CC2500_Strobe(CC2500_SIDLE);
				CC2500_WriteData(buf, len);
				break;
		#endif
		#ifdef NRF24L01_INSTALLED
			case BENCH_NRF:
				NRF24L01_WriteReg(NRF24L01_07_STATUS, 0x70);
				NRF24L01_FlushTx();
				NRF24L01_WritePayload(buf, len);
				break;
		#endif
		#ifdef A7105_INSTALLED
			case BENCH_A7105:
				memcpy(packet, buf, len);
				A7105_WriteData(len, bench_rf_ch);
				break;
		#endif
		#ifdef CYRF6936_INSTALLED
			case BENCH_CYRF:
				CYRF_WriteDataPacketLen(buf, len);
				break;
		#endif
		#if defined(CC2500_INSTALLED) || defined(NRF24L01_INSTALLED)
			case BENCH_XN297:
				if(bench_xn_enhanced)
					XN297_WriteEnhancedPayload(buf, len, 0);
				else
					XN297_WritePayload(buf, len);
				break;
		#endif
	}
	bench_stat_tx++;
	bench_led_tx();
}

// Poll the current chip, the chip must be in RX mode.
// Returns true if a packet has been read, RX is then rearmed.
bool bench_receive(uint8_t *buf, uint8_t *len, uint8_t *flags)
{
	*flags = 0;
	switch(bench_chip)
	{
		#ifdef CC2500_INSTALLED
			case BENCH_CC2500:
			{
				// MARCSTATE first: once idle, RXBYTES is final. Read the other way round,
				// a packet ending between the two reads was taken as complete with the
				// byte count from before its end.
				bool idle = (CC2500_ReadReg(CC2500_35_MARCSTATE | CC2500_READ_BURST) & 0x1F) == 0x01;
				uint8_t n = CC2500_ReadReg(CC2500_3B_RXBYTES | CC2500_READ_BURST);
				if(n & 0x80)
				{ // RX FIFO overflow
					bench_stat_err++;
					bench_set_mode(RX_EN);
					return false;
				}
				n &= 0x7F;
				if(n == 0)
					return false;
				if(bench_rx_len)
				{
					if(n < bench_rx_len)
						return false;
					n = bench_rx_len;
				}
				else if(!idle)
					return false;			// Packet not complete yet
				if(n > BENCH_PKT_MAX)
					n = BENCH_PKT_MAX;
				CC2500_ReadData(buf, n);
				*len = n;
				if(idle)
					bench_set_mode(RX_EN);
				return true;
			}
		#endif
		#ifdef NRF24L01_INSTALLED
			case BENCH_NRF:
			{
				if(!(NRF24L01_ReadReg(NRF24L01_07_STATUS) & _BV(NRF24L01_07_RX_DR)))
					return false;
				if(NRF24L01_ReadReg(NRF24L01_09_CD) & 0x01)
					*flags |= BENCH_RX_RPD;
				uint8_t n;
				if(NRF24L01_ReadReg(NRF24L01_1C_DYNPD) & 0x01)
					n = NRF24L01_GetDynamicPayloadSize();
				else
					n = bench_rx_len ? bench_rx_len : NRF24L01_ReadReg(NRF24L01_11_RX_PW_P0);
				if(n > 32)
				{ // Corrupted payload
					NRF24L01_FlushRx();
					NRF24L01_WriteReg(NRF24L01_07_STATUS, _BV(NRF24L01_07_RX_DR));
					bench_stat_err++;
					return false;
				}
				NRF24L01_ReadPayload(buf, n);
				*len = n;
				bench_nrf_load_ack();
				// Clear RX_DR only when the FIFO is empty so the next payload gets read on the next poll
				if(NRF24L01_ReadReg(NRF24L01_17_FIFO_STATUS) & 0x01)
					NRF24L01_WriteReg(NRF24L01_07_STATUS, _BV(NRF24L01_07_RX_DR));
				*flags |= BENCH_RX_CRC | BENCH_RX_CRC_OK;	// The NRF drops packets with a bad CRC when CRC is enabled
				if(!(NRF24L01_ReadReg(NRF24L01_00_CONFIG) & _BV(NRF24L01_00_EN_CRC)))
					*flags = *flags & BENCH_RX_RPD;
				return true;
			}
		#endif
		#ifdef A7105_INSTALLED
			case BENCH_A7105:
			{
				if(micros() - bench_rx_armed < BENCH_A7105_SETTLE)
					return false;			// RX not started yet, MODE would look like a received packet
				uint8_t mode = A7105_ReadReg(A7105_00_MODE);
				if(mode & 0x01)
					return false;			// Still receiving
				uint8_t n = bench_rx_len;
				A7105_ReadData(n);
				memcpy(buf, packet, n);
				*len = n;
				*flags = BENCH_RX_CRC | ((mode & 0x20) ? 0 : BENCH_RX_CRC_OK);
				bench_set_mode(RX_EN);
				return true;
			}
		#endif
		#ifdef CYRF6936_INSTALLED
			case BENCH_CYRF:
			{
				uint8_t st = CYRF_ReadRegister(CYRF_07_RX_IRQ_STATUS);
				if((st & 0x03) == 0x02)				// RXC without RXE: read again (debouncing, as DSM)
					st |= CYRF_ReadRegister(CYRF_07_RX_IRQ_STATUS);
				if(!(st & 0x03))
					return false;					// Nothing received yet
				CYRF_WriteRegister(CYRF_07_RX_IRQ_STATUS, 0x80);	// RXOW before reading the data
				uint8_t n = CYRF_ReadRegister(CYRF_09_RX_COUNT);
				if(n > BENCH_PKT_MAX)
					n = BENCH_PKT_MAX;
				CYRF_ReadDataPacketLen(buf, n);
				*len = n;
				*flags = BENCH_RX_CRC | ((st & 0x07) == 0x02 ? BENCH_RX_CRC_OK : 0);	// RXE: CRC or other error
				bench_set_mode(RX_EN);
				return true;
			}
		#endif
		#if defined(CC2500_INSTALLED) || defined(NRF24L01_INSTALLED)
			case BENCH_XN297:
			{
				#ifdef CC2500_INSTALLED
					if(xn297_rf == XN297_CC2500)
					{ // XN297_IsRX() only accepts the exact packet length, rearm on anything else
						uint8_t n = CC2500_ReadReg(CC2500_3B_RXBYTES | CC2500_READ_BURST);
						if((n & 0x80) || (n & 0x7F) > xn297_rx_packet_len + 2)
						{
							bench_stat_err++;
							bench_set_mode(RX_EN);
							return false;
						}
					}
				#endif
				if(!XN297_IsRX())
					return false;
				bool crc_ok;
				*len = bench_rx_len;
				if(bench_xn_enhanced)
				{ // Payload size from the packet control field, 255 on errors
					uint8_t n = XN297_ReadEnhancedPayload(buf, bench_rx_len);
					crc_ok = n != 255;
					if(crc_ok)
						*len = n;
				}
				else
					crc_ok = XN297_ReadPayload(buf, bench_rx_len);
				if(xn297_crc)
					*flags = BENCH_RX_CRC | (crc_ok ? BENCH_RX_CRC_OK : 0);
				bench_set_mode(RX_EN);
				return true;
			}
		#endif
	}
	return false;
}

//**************
//***  Jobs  ***
//**************
static uint16_t bench_tx_callback()
{
	if(bench_tx_left == 0)
	{
		if(bench_seq_idx < bench_tx_len)
			bench_tx_buf[bench_seq_idx] = bench_seq++;
		bench_send(bench_tx_buf, bench_tx_len);
		if(bench_tx_count && bench_stat_tx >= bench_tx_count)
		{
			bench_printf("@ev txdone %lu\r\n", bench_stat_tx);
			bench_job = BENCH_IDLE;
			bench_callback = 0;
			return 1000;
		}
		bench_tx_left = bench_tx_period;
	}
	uint16_t wait = bench_tx_left > BENCH_MAX_CB ? BENCH_MAX_CB : bench_tx_left;
	bench_tx_left -= wait;
	return wait;
}

static uint16_t bench_rx_callback()
{
	uint8_t buf[BENCH_PKT_MAX], len, flags;
	if(bench_receive(buf, &len, &flags))
	{
		bench_stat_rx++;
		if((flags & BENCH_RX_CRC) && !(flags & BENCH_RX_CRC_OK))
			bench_stat_err++;
		bench_report_rx(buf, len, flags);
	}
	return BENCH_RX_POLL;
}

static void bench_stop()
{
	bench_callback = 0;
	if(bench_job == BENCH_PROTO)
	{ // Leave the chips in a known state
		remote_callback = 0;
		protocol = 0;
		modules_reset();
		bench_chip = BENCH_NONE;
	}
	else if(bench_job != BENCH_IDLE)
		bench_set_mode(TXRX_OFF);
	bench_job = BENCH_IDLE;
}

static void bench_start(uint8_t job, uint16_function_t callback)
{
	bench_stat_tx = bench_stat_rx = bench_stat_err = 0;
	bench_out_drop = 0;
	bench_job = job;
	bench_callback = callback;
}

//******************
//***  Commands  ***
//******************
static bool bench_need_chip()
{
	if(bench_chip != BENCH_NONE)
		return true;
	bench_printf("@err no chip selected\r\n");
	return false;
}

static void bench_cmd_info()
{
	bench_printf("@ok bench=%d chip=%s job=%s ch=%u", BENCH_VERSION, bench_chip_names[bench_chip], bench_job_names[bench_job], bench_rf_ch);
	#ifdef CC2500_INSTALLED
		bench_printf(" cc2500=%d", bench_chip_reset(BENCH_CC2500));
	#endif
	#ifdef NRF24L01_INSTALLED
		bench_printf(" nrf=%d", bench_chip_reset(BENCH_NRF));
	#endif
	#ifdef A7105_INSTALLED
		bench_printf(" a7105=%d", bench_chip_reset(BENCH_A7105));
	#endif
	#ifdef CYRF6936_INSTALLED
		bench_printf(" cyrf=%d", bench_chip_reset(BENCH_CYRF));
	#endif
	bench_puts("\r\n");
	bench_chip = BENCH_NONE;			// All chips have been reset
}

static void bench_cmd_chip(char *args)
{
	char *name = bench_token(&args);
	uint8_t chip = BENCH_NONE;
	for(uint8_t i = BENCH_CC2500; i <= BENCH_CYRF; i++)
		if(i != BENCH_XN297 && name && strcmp(name, bench_chip_names[i]) == 0)
			chip = i;
	if(chip == BENCH_NONE)
	{
		bench_printf("@err unknown chip\r\n");
		return;
	}
	bench_chip = chip;
	bench_rf_ch = 0;
	if(bench_chip_reset(chip))
		bench_printf("@ok chip=%s\r\n", name);
	else
		bench_printf("@err chip=%s not responding\r\n", name);
}

static void bench_cmd_preset(char *args)
{
	char *name = bench_token(&args);
	uint32_t arg = bench_number_or(&args, 0);
	if(name == 0)
		name = (char*)"";
	#ifdef CC2500_INSTALLED
		bool cc2500 = true;
		option = arg;					// Frequency offset used by the FrSky init
		#if defined(FRSKYD_CC2500_INO)
			if(strcmp(name, "frskyd") == 0)		{ bench_chip_reset(BENCH_CC2500); FRSKY_init_cc2500(FRSKYD_cc2500_conf); }		else
		#endif
		#if defined(FRSKYX_CC2500_INO)
			if(strcmp(name, "frskyx") == 0)		{ bench_chip_reset(BENCH_CC2500); FRSKY_init_cc2500(FRSKYX_cc2500_conf); }		else
			if(strcmp(name, "frskyxeu") == 0)	{ bench_chip_reset(BENCH_CC2500); FRSKY_init_cc2500(FRSKYXEU_cc2500_conf); }	else
		#endif
			if(strcmp(name, "250k") == 0)		{ bench_chip_reset(BENCH_CC2500); CC2500_250K_Init(); CC2500_WriteReg(CC2500_0C_FSCTRL0, arg); }	else
			cc2500 = false;
		if(cc2500)
		{
			bench_chip = BENCH_CC2500;
			bench_rf_ch = 0;
			bench_printf("@ok preset=%s chip=cc2500\r\n", name);
			return;
		}
	#endif
	#ifdef A7105_INSTALLED
		uint8_t proto = 0;
		#if defined(AFHDS2A_A7105_INO)
			if(strcmp(name, "afhds2a") == 0)	proto = PROTO_AFHDS2A;
		#endif
		#if defined(FLYSKY_A7105_INO)
			if(strcmp(name, "flysky") == 0)		proto = PROTO_FLYSKY;
		#endif
		#if defined(HUBSAN_A7105_INO)
			if(strcmp(name, "hubsan") == 0)		proto = PROTO_HUBSAN;
		#endif
		uint8_t sub = 0;
		#if defined(BUGS_A7105_INO)
			if(strcmp(name, "bugs") == 0)		proto = PROTO_BUGS;
		#endif
		#if defined(HEIGHT_A7105_INO)
			if(strcmp(name, "height") == 0)		proto = PROTO_HEIGHT;
			if(strcmp(name, "height8") == 0)	{ proto = PROTO_HEIGHT; sub = HEIGHT_8CH; }
		#endif
		#if defined(JOYSWAY_A7105_INO)
			if(strcmp(name, "joysway") == 0)	proto = PROTO_JOYSWAY;
		#endif
		#if defined(KYOSHO_A7105_INO)
			if(strcmp(name, "kyosho") == 0)		proto = PROTO_KYOSHO;
			if(strcmp(name, "kyoshohype") == 0)	{ proto = PROTO_KYOSHO; sub = KYOSHO_HYPE; }
		#endif
		#if defined(PELIKAN_A7105_INO)
			if(strcmp(name, "pelikan") == 0)	proto = PROTO_PELIKAN;
		#endif
		#if defined(WFLY2_A7105_INO)
			if(strcmp(name, "wfly2") == 0)		proto = PROTO_WFLY2;
		#endif
		if(proto)
		{
			bench_chip_reset(BENCH_A7105);
			protocol = proto;			// A7105_Init() selects its register table from the protocol
			sub_protocol = sub;
			A7105_Init();
			protocol = 0;
			sub_protocol = 0;
			bench_chip = BENCH_A7105;
			bench_rf_ch = 0;
			bench_printf("@ok preset=%s chip=a7105\r\n", name);
			return;
		}
	#endif
	#if defined(CYRF6936_INSTALLED) && defined(DSM_CYRF6936_INO)
		bool dsm = strcmp(name, "dsm") == 0, dsmbind = strcmp(name, "dsmbind") == 0;
		if(dsm || dsmbind)
		{ // DSM register setup, codes of DSM_set_sop_data_crc() for PN row 0 and SOP column 0
			uint8_t code[16];
			bench_chip_reset(BENCH_CYRF);
			DSM_cyrf_config();
			if(dsm)
			{
				DSM_cyrf_configdata();
				DSM_read_code(code, 0, 0);
				CYRF_ConfigSOPCode(code);
				DSM_read_code(code, 0, 7);
				DSM_read_code(code + 8, 0, 8);
			}
			else
				DSM_read_code(code, 0, 8);	// as DSM_initialize_bind_phase(): SDR, first 8 bytes used
			CYRF_ConfigDataCode(code);
			CYRF_ConfigCRCSeed(0);
			bench_chip = BENCH_CYRF;
			bench_rf_ch = 0;
			CYRF_ConfigRFChannel(0);
			bench_printf("@ok preset=%s chip=cyrf\r\n", name);
			return;
		}
	#endif
	#ifdef NRF24L01_INSTALLED
		if(strcmp(name, "nrf") == 0)
		{
			bench_chip_reset(BENCH_NRF);
			NRF24L01_Initialize();
			bench_chip = BENCH_NRF;
			bench_rf_ch = 0;
			bench_printf("@ok preset=nrf chip=nrf\r\n");
			return;
		}
	#endif
	bench_printf("@err unknown preset\r\n");
}

static void bench_cmd_xn(char *args)
{
	#if defined(CC2500_INSTALLED) || defined(NRF24L01_INSTALLED)
		uint32_t crc, scramble, bitrate;
		if(!bench_number(&args, &crc) || !bench_number(&args, &scramble) || !bench_number(&args, &bitrate))
		{
			bench_printf("@err usage: xn <crc> <scramble> <250k> [force_nrf] [enhanced]\r\n");
			return;
		}
		bool force_nrf = bench_number_or(&args, 0);
		bench_xn_enhanced = bench_number_or(&args, 0);
		if(!XN297_Configure(crc, scramble, bitrate ? XN297_250K : XN297_1M, force_nrf))
		{
			bench_printf("@err xn297 config not supported\r\n");
			return;
		}
		bench_chip = BENCH_XN297;
		bench_rf_ch = 0;
		bench_printf("@ok xn297 rf=%s enhanced=%d\r\n", xn297_rf == XN297_CC2500 ? "cc2500" : "nrf", bench_xn_enhanced);
	#else
		(void)args;
		bench_printf("@err no xn297 support\r\n");
	#endif
}

static void bench_cmd_xaddr(char *args)
{
	#if defined(CC2500_INSTALLED) || defined(NRF24L01_INSTALLED)
		int16_t len = bench_hex(&args, bench_xn_addr, sizeof(bench_xn_addr));
		if(bench_chip != BENCH_XN297 || len < 3)
		{
			bench_printf("@err usage: xn first, then xaddr <3..5 bytes>\r\n");
			return;
		}
		bench_xn_addr_len = len;
		XN297_SetTXAddr(bench_xn_addr, len);
		bench_printf("@ok\r\n");
	#else
		(void)args;
		bench_printf("@err no xn297 support\r\n");
	#endif
}

static void bench_cmd_write(char *args)
{
	uint32_t reg;
	uint8_t data[BENCH_PKT_MAX];
	int16_t len;
	if(!bench_need_chip()) return;
	if(bench_chip == BENCH_XN297 || !bench_number(&args, &reg) || (len = bench_hex(&args, data, sizeof(data))) <= 0)
	{
		bench_printf("@err usage: w <reg> <hex>\r\n");
		return;
	}
	bench_write_regs(reg, data, len);
	bench_printf("@ok\r\n");
}

static void bench_cmd_read(char *args)
{
	uint32_t reg;
	uint8_t data[BENCH_PKT_MAX];
	if(!bench_need_chip()) return;
	uint8_t len = 1;
	if(bench_chip == BENCH_XN297 || !bench_number(&args, &reg))
	{
		bench_printf("@err usage: r <reg> [n]\r\n");
		return;
	}
	len = bench_number_or(&args, 1);
	if(len == 0 || len > sizeof(data))
		len = 1;
	bench_read_regs(reg, data, len);
	bench_puts("@ok ");
	bench_print_hex(data, len);
	bench_puts("\r\n");
}

static void bench_cmd_tx(char *args, bool periodic)
{
	if(!bench_need_chip()) return;
	uint32_t period = 0, count = 1;
	if(periodic && (!bench_number(&args, &period) || !bench_number(&args, &count) || period < 100))
	{
		bench_printf("@err usage: txp <period_us >= 100> <count> <hex>\r\n");
		return;
	}
	int16_t len = bench_hex(&args, bench_tx_buf, sizeof(bench_tx_buf));
	if(len <= 0)
	{
		bench_printf("@err usage: tx <hex>\r\n");
		return;
	}
	bench_stop();
	bench_tx_len = len;
	bench_set_mode(TX_EN);
	if(!periodic)
	{
		bench_stat_tx = 0;
		bench_send(bench_tx_buf, bench_tx_len);
		bench_printf("@ok\r\n");
		return;
	}
	bench_tx_period = period;
	bench_tx_count = count;
	bench_tx_left = 0;
	bench_seq = 0;
	bench_start(BENCH_TX, bench_tx_callback);
	bench_printf("@ok\r\n");
}

static void bench_cmd_rx(char *args)
{
	if(!bench_need_chip()) return;
	bench_stop();
	bench_rx_len = bench_number_or(&args, 0);
	if(bench_rx_len > BENCH_PKT_MAX
		|| ((bench_chip == BENCH_XN297 || bench_chip == BENCH_A7105) && bench_rx_len == 0)
		|| (bench_chip == BENCH_XN297 && bench_xn_addr_len == 0))
	{
		bench_printf("@err bad rx length or no xaddr\r\n");
		return;
	}
	bench_set_mode(RX_EN);
	bench_start(BENCH_RX, bench_rx_callback);
	bench_printf("@ok\r\n");
}

static void bench_cmd_proto(char *args)
{
	uint32_t num;
	if(!bench_number(&args, &num) || num == 0)
	{
		bench_printf("@err usage: proto <num> [sub] [rxnum] [option] [bind]\r\n");
		return;
	}
	bench_stop();
	protocol = num;
	sub_protocol = bench_number_or(&args, 0);
	RX_num = bench_number_or(&args, 0);
	option = bench_number_or(&args, 0);
	if(bench_number_or(&args, 0))
		BIND_IN_PROGRESS;
	else
		BIND_DONE;
	POWER_FLAG_on;
	protocol_init();
	if(remote_callback == 0)
	{
		protocol = 0;
		bench_printf("@err protocol %lu not available\r\n", num);
		return;
	}
	bench_start(BENCH_PROTO, remote_callback);
	bench_printf("@ok proto=%s\r\n", multi_protocols[multi_protocols_index].ProtoString);
}

static void bench_cmd_user(char *args)
{
	uint8_t data[BENCH_PKT_MAX];
	int16_t len = bench_hex(&args, data, sizeof(data));
	if(len < 0)
	{
		bench_printf("@err usage: user [hex]\r\n");
		return;
	}
	bench_stop();
	if(!Bench_user_init(data, len))
	{
		bench_printf("@err user init failed\r\n");
		return;
	}
	bench_start(BENCH_USER, Bench_user_callback);
	bench_printf("@ok\r\n");
}

static void bench_exec(char *line)
{
	char *args = line;
	char *cmd = bench_token(&args);
	uint32_t val;
	if(cmd == 0)
		return;
	if(strcmp(cmd, "info") == 0)		{ bench_stop(); bench_cmd_info(); }
	else if(strcmp(cmd, "chip") == 0)	{ bench_stop(); bench_cmd_chip(args); }
	else if(strcmp(cmd, "preset") == 0)	{ bench_stop(); bench_cmd_preset(args); }
	else if(strcmp(cmd, "xn") == 0)		{ bench_stop(); bench_cmd_xn(args); }
	else if(strcmp(cmd, "xaddr") == 0)	bench_cmd_xaddr(args);
	else if(strcmp(cmd, "w") == 0)		bench_cmd_write(args);
	else if(strcmp(cmd, "r") == 0)		bench_cmd_read(args);
	else if(strcmp(cmd, "s") == 0)
	{
		if(!bench_need_chip()) return;
		if(bench_chip == BENCH_XN297 || !bench_number(&args, &val))
			bench_printf("@err usage: s <cmd>\r\n");
		else
		{
			bench_strobe(val);
			bench_printf("@ok\r\n");
		}
	}
	else if(strcmp(cmd, "ch") == 0)
	{
		if(!bench_need_chip()) return;
		if(!bench_number(&args, &val))
			bench_printf("@err usage: ch <n>\r\n");
		else
		{
			bench_set_channel(val);
			if(bench_job == BENCH_RX)
				bench_set_mode(RX_EN);
			bench_printf("@ok\r\n");
		}
	}
	else if(strcmp(cmd, "tx") == 0)		bench_cmd_tx(args, false);
	else if(strcmp(cmd, "txp") == 0)	bench_cmd_tx(args, true);
	else if(strcmp(cmd, "seq") == 0)
	{
		bench_seq_idx = bench_number_or(&args, 0xFF);
		bench_printf("@ok\r\n");
	}
	else if(strcmp(cmd, "rx") == 0)		bench_cmd_rx(args);
	else if(strcmp(cmd, "ackpl") == 0)
	{ // NRF ack payload, none to clear
		int16_t len = bench_hex(&args, bench_ack_pl, sizeof(bench_ack_pl));
		bench_ack_pl_len = len > 0 ? len : 0;
		bench_printf("@ok ackpl=%u\r\n", bench_ack_pl_len);
	}
	else if(strcmp(cmd, "stop") == 0)
	{
		bench_stop();
		bench_printf("@ok stop tx=%lu rx=%lu err=%lu", bench_stat_tx, bench_stat_rx, bench_stat_err);
		if(bench_out_drop)
			bench_printf(" out_drop=%lu", bench_out_drop);
		bench_puts("\r\n");
	}
	else if(strcmp(cmd, "chan") == 0)
	{
		uint32_t value;
		if(!bench_number(&args, &val) || !bench_number(&args, &value) || val >= NUM_CHN || value > 2047)
			bench_printf("@err usage: chan <0..%d> <0..2047>\r\n", NUM_CHN-1);
		else
		{
			Channel_data[val] = value;
			bench_printf("@ok\r\n");
		}
	}
	else if(strcmp(cmd, "proto") == 0)	bench_cmd_proto(args);
	else if(strcmp(cmd, "user") == 0)	bench_cmd_user(args);
	else
		bench_printf("@err unknown command %s\r\n", cmd);
}

// Execute at most one complete line per call
static void bench_poll()
{
	bench_out_flush();
	bench_led_poll();
	while(Serial.available())
	{
		char c = Serial.read();
		if(c == '\r' || c == '\n')
		{
			if(bench_line_len == 0)
				continue;
			bench_line[bench_line_len] = 0;
			bench_line_len = 0;
			bench_exec(bench_line);
			return;
		}
		if(bench_line_len < BENCH_LINE_MAX - 1)
			bench_line[bench_line_len++] = c;
	}
}

//*******************
//***  Scheduler  ***
//*******************
// Same timing scheme as loop(): TIMER2 runs at 2MHz, callbacks return the time in us to the next call.
void Bench_loop()
{
	uint32_t last_poll = millis();

	for(uint8_t ch = 0; ch < NUM_CHN; ch++)
		Channel_data[ch] = 1024;
	protocol = 0;
	remote_callback = 0;
	crc16_polynomial = 0x1021;				// Defaults set by protocol_init(), never called here (XN297 emulation CRC)
	crc8_polynomial  = 0x31;
	bench_led_init();
	bench_printf("@ev boot bench=%d\r\n", BENCH_VERSION);

	while(1)
	{
		if(bench_callback == 0)
		{
			bench_poll();
			last_poll = millis();
			cli();
			OCR1A = TCNT1;					// New sync point for the next job
			sei();
			continue;
		}
		uint16_t next_callback = bench_callback() << 1;
		cli();
		OCR1A += next_callback;
		TIMER2_BASE->SR = 0x1E5F & ~TIMER_SR_CC1IF;	// Clear Timer2/Comp1 interrupt flag
		uint16_t diff = OCR1A - TCNT1;
		sei();
		if((diff & 0x8000) && !(next_callback & 0x8000))
			continue;						// Late: call the job again right away
		if(millis() - last_poll >= BENCH_USB_POLL)
		{ // Commands must not starve when the job uses short callbacks
			bench_poll();
			last_poll = millis();
		}
		while((TIMER2_BASE->SR & TIMER_SR_CC1IF) == 0)
		{
			if(diff > 900*2)
			{ // At least 900us available
				bench_poll();
				last_poll = millis();
				if(bench_callback == 0)
					break;
				cli();
				diff = OCR1A - TCNT1;
				sei();
				if(diff & 0x8000)
					break;
			}
		}
	}
}

#endif
