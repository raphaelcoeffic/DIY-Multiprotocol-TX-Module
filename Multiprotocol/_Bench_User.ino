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

#define PING_MAX_PAYLOAD	32

static uint8_t ping_payload[PING_MAX_PAYLOAD], ping_len, ping_seq, ping_seq_idx;
static uint16_t ping_timeout_ms, ping_period_ms;
static uint32_t ping_start;

// Return false to refuse the start, the bench then answers "@err user init failed"
bool Bench_user_init(uint8_t *args, uint8_t len)
{
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
