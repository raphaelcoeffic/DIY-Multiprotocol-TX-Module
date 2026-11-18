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
 Bench user code: started by the "user [hex]" bench command, stopped by "stop".
 Use it for tests which need tighter timing than a host can provide over USB.

 Available from _Bench.ino, on the chip selected with "chip"/"preset"/"xn":
   bench_set_channel(ch), bench_set_mode(TX_EN|RX_EN|TXRX_OFF),
   bench_send(buf, len), bench_receive(buf, &len, &flags) (RX length set by "rx" or bench_rx_len),
   bench_report_rx(buf, len, flags), bench_printf(fmt, ...) (prefix events with "@ev ").
 The low level chip functions (CC2500_*, NRF24L01_*, A7105_*, XN297_*) can be used too.

 Example below: ping. Sends a packet, then listens for an answer from the other side
 and reports the round trip time. The other side must echo the packet back.
   user <timeout_ms> <period_ms> <seq_idx> <rx_len> <payload hex>
   -> "@ev pong <seq> <rtt_us> <hex>" or "@ev timeout <seq>"
 The payload byte at seq_idx is replaced by a sequence number (keep the CC2500 length byte at 0).
 rx_len is the length given to the "rx" command for the answer (0 = auto, CC2500 and NRF only).
*/

#if defined(MULTI_BENCH)

/*
 Futaba T-FHSS Air test TX (mpm-v2 src/core/tfhss.h, doc/futaba.md: the T18SZ firmware), for the
 mpm-v2 tfhssrx air follower. CC2500 set up by the host first (preset frskyd, then the T18SZ
 registers: 128 kbps 2-FSK, 38 kHz deviation, sync D391, fixed length, no CRC).
   user a1 <row> <reply>
 row 0..30: servo frames every 15040 us along the row, channel halves alternating, ch1 sweeping;
 row ff: plain link frames on the 16-channel list. reply 1: a fake 20 byte receiver reply
 (sensor type 3, group 0) on the next channel 7 ms after each frame. TX ID 123456789, x 5A,
 receiver ID 999999998.
*/
static const uint8_t air_row0[31] = {31, 1, 3, 9, 27, 19, 26, 16, 17, 20, 29, 25, 13, 8, 24, 10,
									 30, 28, 22, 4, 12, 5, 15, 14, 11, 2, 6, 18, 23, 7, 21};
static const uint8_t air_list[16] = {1, 25, 3, 22, 4, 21, 6, 19, 7, 18, 9, 16, 10, 15, 12, 13};
static const uint8_t air_tx_id[4] = {0x07, 0x5B, 0xCD, 0x15};	// I0..I3: 123456789
static const uint8_t air_rx_id[4] = {0x3B, 0x9A, 0xC9, 0xFE};	// 999999998
#define AIR_X			0x5A
#define AIR_PERIOD		15040
#define AIR_REPLY_AT	7000
static bool air_on;
static uint8_t air_row, air_reply, air_idx;
static uint16_t air_frame;
static int16_t air_sweep;
static uint16_t air_ch1;

static uint8_t air_channel(uint8_t i)
{
	if(air_row == 0xFF) return air_list[i % 16];
	return (air_row0[i % 31] + air_row - 1) % 31 + 1;
}

// len bytes on air: len - 2 XOR the key 00 ID3 ID2 ID1 ID0 x n ID3... (byte 17: 00), then the
// CRC-16/MCRF4XX of the plain bytes, low byte first; id NULL: plain
static void air_send(uint8_t *plain, uint8_t len, const uint8_t *id, uint8_t n)
{
	uint8_t out[20], data = len - 2;
	uint16_t crc = 0xFFFF;
	for(uint8_t i = 0; i < data; i++)
	{
		crc ^= plain[i];
		for(uint8_t j = 0; j < 8; j++) crc = crc & 1 ? (crc >> 1) ^ 0x8408 : crc >> 1;
		uint8_t k = 0, m = (i - 1) % 6;
		if(id && i != 0 && i != 17) k = m < 4 ? id[3 - m] : m == 4 ? AIR_X : n;
		out[i] = plain[i] ^ k;
	}
	out[data] = crc;
	out[data + 1] = crc >> 8;
	bench_set_mode(TX_EN);
	bench_set_channel(5 * (n - 1));
	CC2500_WriteReg(CC2500_06_PKTLEN, len);	// fixed length: 17 (TX) or 20 (reply)
	bench_send(out, len);
}

static void air_tx()
{
	uint8_t p[17] = {0}, n = air_channel(air_idx++);
	if(air_row == 0xFF)
	{	// Link frame
		p[0] = 0xF8;
		for(uint8_t i = 0; i < 4; i++) p[1 + i] = air_tx_id[3 - i];
		p[5] = AIR_X;
		p[6] = n;
		air_send(p, 17, NULL, n);
		return;
	}
	if(air_idx >= 31) air_idx = 0;
	// 9 slots of 11 bits (us - 880): ch1..6 / ch11..16, ch7, ch8, ch9 / ch10
	bool half = air_frame & 1;
	if(air_ch1 + air_sweep > 1140 || air_ch1 + air_sweep < 140) air_sweep = -air_sweep;
	air_ch1 += air_sweep;
	uint16_t s[9];
	for(uint8_t i = 0; i < 9; i++) s[i] = 640;
	if(!half) s[0] = air_ch1;
	uint8_t bit = 0;
	for(uint8_t i = 0; i < 9; i++)
		for(uint8_t j = 0; j < 11; j++, bit++)
			if(s[i] >> j & 1) p[1 + bit / 8] |= 1 << (bit % 8);
	p[0] = air_row << 3 | ((air_frame & 7) == 0 ? 0x04 : 0);	// telemetry on, every 8th frame
	if(half)
	{
		p[13] |= 0x08;
		p[14] = 0x3F;
	}
	air_frame++;
	air_send(p, 17, air_tx_id, n);
}

static void air_send_reply()
{
	uint8_t p[20] = {0}, n = air_channel(air_idx);
	p[0] = 0x03;					// sensor data
	p[1] = 0x00; p[2] = 0x74;		// slot 0: receiver voltage, as some S.BUS2 value
	p[17] = 0;						// group 0
	air_send(p, 20, air_rx_id, n);
}

static bool air_init(uint8_t *args, uint8_t len)
{
	if(len < 2 || (args[0] > 30 && args[0] != 0xFF) || args[1] > 1 || bench_chip != BENCH_CC2500)
		return false;
	air_row = args[0];
	air_reply = args[1] && air_row != 0xFF;
	air_idx = 0;
	air_frame = 0;
	air_ch1 = 640;
	air_sweep = 8;
	air_on = true;
	phase = 0;
	return true;
}

static uint16_t air_callback()
{
	if(phase == 0)
	{
		air_tx();
		if(!air_reply) return AIR_PERIOD;
		phase = 1;
		return AIR_REPLY_AT;
	}
	air_send_reply();
	phase = 0;
	return AIR_PERIOD - AIR_REPLY_AT;
}

#define PING_MAX_PAYLOAD	32

static uint8_t ping_payload[PING_MAX_PAYLOAD], ping_len, ping_seq, ping_seq_idx;
static uint16_t ping_timeout_ms, ping_period_ms;
static uint32_t ping_start;

// Return false to refuse the start, the bench then answers "@err user init failed"
bool Bench_user_init(uint8_t *args, uint8_t len)
{
	air_on = false;
	if(len >= 1 && args[0] == 0xA1)
		return air_init(args + 1, len - 1);
	if(len < 5 || len - 4 > PING_MAX_PAYLOAD || args[2] >= len - 4 || args[3] > BENCH_PKT_MAX || bench_chip == BENCH_NONE)
		return false;
	ping_timeout_ms = args[0];
	ping_period_ms = args[1];
	ping_seq_idx = args[2];
	bench_rx_len = args[3];
	ping_len = len - 4;
	memcpy(ping_payload, args + 4, ping_len);
	ping_seq = 0;
	phase = 0;
	return true;
}

// Called by the bench scheduler, returns the time in us before the next call (max 30000)
uint16_t Bench_user_callback()
{
	if(air_on)
		return air_callback();
	uint8_t buf[BENCH_PKT_MAX], len, flags;

	switch(phase)
	{
		case 0:	// Send
			ping_payload[ping_seq_idx] = ping_seq;
			bench_set_mode(TX_EN);
			bench_send(ping_payload, ping_len);
			ping_start = micros();
			phase++;
			return 1500;				// Leave time for the packet to be sent
		case 1:	// Listen
			bench_set_mode(RX_EN);
			phase++;
			return 100;
		case 2:	// Wait for the answer
			if(bench_receive(buf, &len, &flags))
			{
				bench_printf("@ev pong %u %lu ", ping_seq, micros() - ping_start);
				bench_print_hex(buf, len);
				bench_puts("\r\n");
			}
			else if(micros() - ping_start < (uint32_t)ping_timeout_ms * 1000)
				return 100;
			else
				bench_printf("@ev timeout %u\r\n", ping_seq);
			ping_seq++;
			bench_set_mode(TXRX_OFF);
			phase = 3;
			return 1000;
		default: // Wait for the next period
		{
			uint32_t elapsed = micros() - ping_start;
			uint32_t period = (uint32_t)ping_period_ms * 1000;
			if(elapsed >= period)
			{
				phase = 0;
				return 100;
			}
			period -= elapsed;
			return period > BENCH_MAX_CB ? BENCH_MAX_CB : period;
		}
	}
}

#endif
