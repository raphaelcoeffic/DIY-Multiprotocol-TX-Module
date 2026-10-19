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

/****************************/
/*** BENCH MODE OVERRIDES ***/
/****************************/
// Included after _Config.h (and _MyConfig.h) when MULTI_BENCH is defined.
// A debug build with every protocol does not fit in flash, so only the
// protocols usable as "proto" references by the bench are kept.

// No protocol selection (_Config.h: MULTI_AIR), the bench keeps surface protocols too
#undef	MULTI_AIR
#undef	MULTI_SURFACE
#undef	MULTI_EU

// Kept protocols
#define	FRSKYD_CC2500_INO
#define	FRSKYX_CC2500_INO
#define	FRSKYV_CC2500_INO
#define	FRSKY_RX_CC2500_INO
#define	AFHDS2A_A7105_INO
#define	AFHDS2A_RX_A7105_INO
#define	FLYSKY_A7105_INO
#define	HUBSAN_A7105_INO
#define	BUGS_A7105_INO
#define	HEIGHT_A7105_INO
#define	JOYSWAY_A7105_INO
#define	KYOSHO_A7105_INO
#define	PELIKAN_A7105_INO
#define	WFLY2_A7105_INO
#define	DSM_CYRF6936_INO
#define	BAYANG_NRF24L01_INO
#define	BAYANG_RX_NRF24L01_INO

// Removed protocols
#undef	ARES_CC2500_INO
#undef	ASSAN_NRF24L01_INO
#undef	BUGSMINI_NRF24L01_INO
#undef	CABELL_NRF24L01_INO
#undef	CFLIE_NRF24L01_INO
#undef	CG023_NRF24L01_INO
#undef	CORONA_CC2500_INO
#undef	CX10_NRF24L01_INO
#undef	DEVO_CYRF6936_INO
#undef	DM002_NRF24L01_INO
#undef	DSM_RX_CYRF6936_INO
#define	E010R5_CYRF6936_INO
#undef	E016H_NRF24L01_INO
#undef	E016HV2_CC2500_INO
#define	E01X_CYRF6936_INO
#define	E129_CYRF6936_INO
#undef	EAZYRC_NRF24L01_INO
#undef	ESKY_NRF24L01_INO
#undef	ESKY150_NRF24L01_INO
#undef	ESKY150V2_CC2500_INO
#undef	FQ777_NRF24L01_INO
#define	FRSKYL_CC2500_INO
#undef	FRSKYR9_SX1276_INO
#undef	FUTABA_CC2500_INO
#undef	FX_NRF24L01_INO
#undef	FY326_NRF24L01_INO
#undef	GD00X_CCNRF_INO
#undef	GW008_NRF24L01_INO
#undef	H36_NRF24L01_INO
#undef	H8_3D_NRF24L01_INO
#undef	HISKY_NRF24L01_INO
#undef	HITEC_CC2500_INO
#undef	HONTAI_NRF24L01_INO
#undef	HOTT_CC2500_INO
#undef	IKEAANSLUTA_CC2500_INO
#undef	J6PRO_CYRF6936_INO
#undef	JIABAILE_NRF24L01_INO
#undef	JJRC345_NRF24L01_INO
#undef	KAMTOM_NRF24L01_INO
#undef	KF606_CCNRF_INO
#undef	KN_NRF24L01_INO
#undef	KYOSHO2_NRF24L01_INO
#undef	KYOSHO3_CYRF6936_INO
#undef	LOLI_NRF24L01_INO
#undef	LOSI_CYRF6936_INO
#undef	MJXQ_CCNRF_INO
#undef	MLINK_CYRF6936_INO
#undef	MOULDKG_NRF24L01_INO
#undef	MT99XX_CCNRF_INO
#undef	NCC1701_NRF24L01_INO
#undef	OMP_CCNRF_INO
#undef	POTENSIC_NRF24L01_INO
#undef	PROPEL_NRF24L01_INO
#undef	Q303_CCNRF_INO
#undef	Q90C_CCNRF_INO
#undef	REALACC_NRF24L01_INO
#undef	REDPINE_CC2500_INO
#undef	RLINK_CC2500_INO
#undef	SCANNER_CC2500_INO
#undef	SCORPIO_CYRF6936_INO
#undef	SGF22_NRF24L01_INO
#undef	SHENQI_NRF24L01_INO
#undef	SHENQI2_NRF24L01_INO
#undef	SKYARTEC_CC2500_INO
#undef	SLT_CCNRF_INO
#undef	SYMAX_NRF24L01_INO
#undef	TRAXXAS_CYRF6936_INO
#undef	UDIRC_CCNRF_INO
#undef	V2X2_NRF24L01_INO
#undef	V761_NRF24L01_INO
#undef	V911S_CCNRF_INO
#undef	WFLY_CYRF6936_INO
#undef	WK2x01_CYRF6936_INO
#undef	WL91X_CCNRF_INO
#undef	WPL_NRF24L01_INO
#undef	XERALL_NRF24L01_INO
#undef	XK_CCNRF_INO
#undef	XK2_CCNRF_INO
#undef	YD717_NRF24L01_INO
#undef	YUXIANG_NRF24L01_INO
#undef	ZSX_NRF24L01_INO

// Not needed on the bench
#undef	SEND_CPPM
#undef	ENABLE_DIRECT_INPUTS
