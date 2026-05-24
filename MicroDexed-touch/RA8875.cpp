/*
Part of RA8875 library from https://github.com/sumotoy/RA8875
License:GNU General Public License v3.0

	RA8875 fast SPI library for RAiO SPI RA8875 drived TFT
	Copyright (C) 2014  egidio massimo costa sumotoy (a t) gmail.com

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <SPI.h>
#include "RA8875.h"
#include "config.h"
using namespace RA8875_REG;

//static volatile uint8_t _RA8875_INTS = 0b00000000;//container for INT states
RA8875* RA8875::_active_touch_objects[3] = { nullptr, nullptr, nullptr };

/*------------------------------
Bit:	Called by:		In use:
--------------------------------
0: 		isr triggered	[*]
1: 		Resistive TS	[*]
2: 		KeyScan			[*]
3: 		DMA
4: 		BTE
5: 		FT5206 TS		[*]
6: 		-na-
7: 		-na-
--------------------------------*/

RA8875::RA8875(const uint8_t CSp, const uint8_t RSTp, const uint8_t mosi_pin, const uint8_t sclk_pin, const uint8_t miso_pin)
{
	_mosi = mosi_pin;
	_miso = miso_pin;
	_sclk = sclk_pin;
	_cs = CSp;
	_rst = RSTp;
	_pspi = nullptr;
	_RA8875_INTS = 0b00000000;
	_FNTsource = INTFONT;
}

bool  skipfillrect_webremote = false;

extern bool remote_active;
extern uint16_t ColorHSV(uint16_t hue, uint8_t sat, uint8_t val);
// MIDI sysex render header for remote
const uint8_t sysexRenderHeader[] = { 0xf0, 0x43, 0x21 };
#define SYSEX_HEADER_SIZE 3

uint8_t  sysex_buffer[512];
uint16_t sysex_len = SYSEX_HEADER_SIZE;

#include "mdtfont.c"
//extern "C" const unsigned char mdtfont[];

// Transpose 5x7 mdtfont character (5 column bytes) into
// the RA8875 CGRAM format (16 row bytes, 8px wide, MSB-left).

FLASHMEM void mdtCharToCGRAM(const uint8_t* src5, uint8_t dst16[16]) {
	memset(dst16, 0, 16);
	// src5: 5 bytes, each byte is a column, bit0=top row, bit6=bottom row
	for (uint8_t row = 0; row < 7; row++) {
		uint8_t rowByte = 0;
		for (uint8_t col = 0; col < 5; col++) {
			if (src5[col] & (1 << row)) {
				rowByte |= (0x80 >> col);
			}
		}
		dst16[row] = rowByte;
	}
}

// Call this once after tft.begin() to load the full mdtfont
// into the RA8875 CGRAM at addresses matching their ASCII codes.
FLASHMEM void RA8875::loadMDTfontToCGRAM() {
	uint8_t cgBuf[16];
	// mdtfont[] starts at ASCII 0x00, 5 bytes per char
	// Upload all 128 standard ASCII chars (or as many as fit: 0..127)
	for (uint16_t ch = 0; ch < 128; ch++) {
		mdtCharToCGRAM(&mdtfont[ch * 5], cgBuf);
		uploadUserChar(cgBuf, (uint8_t)ch);
	}
}

// Make the CGRAM font the active font for all hardware text output
FLASHMEM void RA8875::setCGRAMAsDefaultFont(void) {
	// 1. Upload the font (if not already done)
	loadMDTfontToCGRAM();

	// 2. Switch to hardware text mode (MWCR0 bit7 = 1)
	_setTextMode(true);

	// 3. Select CGRAM as font source (FNCR0 bit7 = 1)
	uint8_t fncr0 = _readRegister(RA8875_FNCR0);
	fncr0 |= (1 << 7) | (1 << 5);   // 0xA0
	_writeRegister(RA8875_FNCR0, fncr0);

	uint8_t fwtset = 0x00;       // 16x16 cell (00), width offset = 0
	_writeRegister(RA8875_FWTSET, fwtset);

	// 5. Force the library to use hardware text mode for all print()
	_use_default = 1;
	_use_int_font = 0;
	_use_tfont = 0;
	font = NULL;

	// 6. Update internal cursor metrics
	_FNTwidth = 53;
	_FNTheight = 6;
	_spaceCharWidth = 3;

	_customCharAdvance = 12;

	//  Disable hardware auto‑increment after each character
    //  (MWCR0 bit1 = 1 means "no auto increment")
	uint8_t mwcr0 = _readRegister(RA8875_MWCR0);
	mwcr0 |= (1 << 1);
	_writeRegister(RA8875_MWCR0, mwcr0);

}

FLASHMEM void RA8875::fillRectRainbow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t last_h)
{
	;
}
FLASHMEM void RA8875::invertDisplay(bool)
{
	;
}

FLASHMEM void RA8875::begin(const enum RA8875sizes s, uint8_t colors, uint32_t SPIMaxSpeed, uint32_t SPIMaxReadSpeed)
{

	_errorCode = 0;
	_displaySize = s;
	_rotation = 0;
	_portrait = false;
	_inited = false;
	_sleep = false;
	_hasLayerLimits = false;
	//	_intPin = 255;
	//	_intNum = 0;
	//	_useISR = false;
	//	_enabledInterrups = 0b00000000;
#if defined(SPI_HAS_TRANSACTION)
	_SPIMaxSpeed = SPIMaxSpeed;
	_SPIMaxReadSpeed = SPIMaxReadSpeed;
#endif
	/* used to understand what causes an INT
	bit
	0:
	1:
	2: Touch (resistive)
	3:
	4:
	5:
	6:
	7:
	8:
	*/

#if defined(USE_RA8875_SEPARATE_TEXT_COLOR)
	_TXTForeColor = _RA8875_DEFAULTTXTFRGRND;
#if defined(_RA8875_DEFAULTTXTBKGRND)
	_TXTBackColor = _RA8875_DEFAULTTXTBKGRND;
#else
	_TXTBackColor = 0x0000;
#endif
	_TXTrecoverColor = false;
#endif
	_maxLayers = 2;
	_currentLayer = 0;
	_useMultiLayers = false;//starts with one layer only
	_textMode = false;
	_brightness = 255;
	_cursorX = 0; _cursorY = 0; _scrollXL = 0; _scrollXR = 0; _scrollYT = 0; _scrollYB = 0;
	_scaleX = 1; _scaleY = 1;
	_scaling = false;
	_EXTFNTsize = X16;
	_FNTspacing = 0;
	//_FNTrender = false;
	/* set-->  _TXTparameters  <--
	0:_extFontRom = false;
	1:_autoAdvance = true;
	2:_textWrap = user defined
	3:_fontFullAlig = false;
	4:_fontRotation = false;//not used
	5:_alignXToCenter = false;
	6:_alignYToCenter = false;
	7: render         = false;
	*/
	_TXTparameters = 0b00000010;
	bitWrite(_TXTparameters, 2, _DFT_RA8875_TEXTWRAP);//set _textWrap
	_relativeCenter = false;
	_absoluteCenter = false;
	_EXTFNTrom = _DFT_RA8875_EXTFONTROMTYPE;
	_EXTFNTcoding = _DFT_RA8875_EXTFONTROMCODING;
	_FNTinterline = 0;
	_EXTFNTfamily = STANDARD;
	_FNTcursorType = NOCURSOR;
	_arcAngle_max = ARC_ANGLE_MAX;
	_arcAngle_offset = ARC_ANGLE_OFFSET;
	_angle_offset = ANGLE_OFFSET;
	_color_bpp = 16;
	_colorIndex = 0;

	if (colors != 16) {
		_color_bpp = 8;
		_colorIndex = 3;
	}

	_width = 800;
	_height = 480;
	_hasLayerLimits = true;
	_maxLayers = 1;
	if (_color_bpp < 16) _maxLayers = 2;
	_initIndex = 1;
	//if (_displaySize == RA8875_800x480ALT) _initIndex = 2;


	RA8875_WIDTH = _width;
	RA8875_HEIGHT = _height;
	_activeWindowXL = 0;
	_activeWindowXR = RA8875_WIDTH;
	_activeWindowYT = 0;
	_activeWindowYB = RA8875_HEIGHT;

	//hack
	_cursorY = 0;
	_cursorY = 0;
	// textsize = 2;
	// setTextSize(2);
	wrap = true;
	font = NULL;
	setClipRect();
	setOrigin();

	/* Display Configuration Register	  [0x20]
		  7: (Layer Setting Control) 0:one Layer, 1:two Layers
		  6,5,4: (na)
		  3: (Horizontal Scan Direction) 0: SEG0 to SEG(n-1), 1: SEG(n-1) to SEG0
		  2: (Vertical Scan direction) 0: COM0 to COM(n-1), 1: COM(n-1) to COM0
		  1,0: (na) */
	_DPCR_Reg = 0b00000000;
	/*	Memory Write Control Register 0 [0x40]
		7: 0(graphic mode), 1(textx mode)
		6: 0(font-memory cursor not visible), 1(visible)
		5: 0(normal), 1(blinking)
		4: na
		3-2: 00(LR,TB), 01(RL,TB), 10(TB,LR), 11(BT,LR)
		1: 0(Auto Increase in write), 1(no)
		0: 0(Auto Increase in read), 1(no) */
	_MWCR0_Reg = 0b00000000;

	/*	Font Control Register 0 [0x21]
		7: 0(CGROM font is selected), 1(CGRAM font is selected)
		6: na
		5: 0(Internal CGROM [reg 0x2F to 00]), 1(External CGROM [0x2E reg, bit6,7 to 0)
		4-2: na
		1-0: 00(ISO/IEC 8859-1), 01(ISO/IEC 8859-2), 10(ISO/IEC 8859-3), 11(ISO/IEC 8859-4)*/

	_FNCR0_Reg = 0b00000000;
	/*	Font Control Register 1 [0x22]
		7: 0(Full Alignment off), 1(Full Alignment on)
		6: 0(no-trasparent), 1(trasparent)
		5: na
		4: 0(normal), 1(90degrees)
		3-2: 00(x1), 01(x2), 10(x3), 11(x3) Horizontal font scale
		1-0: 00(x1), 01(x2), 10(x3), 11(x3) Vertical font scale */
	_FNCR1_Reg = 0b00000000;

	/*	Font Write Type Setting Register [0x2E]
	7-6: 00(16x16,8x16,nx16), 01(24x24,12x24,nx24), 1x(32x32,16x32, nx32)
	5-0: 00...3F (font width off to 63 pixels)*/
	_FWTSET_Reg = 0b00000000;

	/*	Serial Font ROM Setting [0x2F]
	GT Serial Font ROM Select
	7-5: 000(GT21L16TW/GT21H16T1W),001(GT30L16U2W),010(GT30L24T3Y/GT30H24T3Y),011(GT30L24M1Z),111(GT30L32S4W/GT30H32S4W)
	FONT ROM Coding Setting
	4-2: 000(GB2312),001(GB12345/GB18030),010(BIG5),011(UNICODE),100(ASCII),101(UNI-Japanese),110(JIS0208),111(Latin/Greek/Cyrillic/Arabic)
	1-0: 00...11
		 bits	ASCII		Lat/Gr/Cyr		Arabic
		 00		normal		normal			na
		 01		Arial		var Wdth		Pres Forms A
		 10		Roman		na				Pres Forms B
		 11		Bold		na				na */
	_SFRSET_Reg = 0b00000000;

	/*	Interrupt Control Register1		  [0xF0]
	7,6,5: (na)
	4: KEYSCAN Interrupt Enable Bit
	3: DMA Interrupt Enable Bit
	2: TOUCH Panel Interrupt Enable Bit
	1: BTE Process Complete Interrupt Enable Bit
	0:
	When MCU-relative BTE operation is selected(*1) and BTE
	Function is Enabled(REG[50h] Bit7 = 1), this bit is used to
		Enable the BTE Interrupt for MCU R/W:
		0 : Disable BTE interrupt for MCU R/W.
		1 : Enable BTE interrupt for MCU R/W.
	When the BTE Function is Disabled, this bit is used to
		Enable the Interrupt of Font Write Function:
		0 : Disable font write interrupt.
		1 : Enable font write interrupt.
	*/
	_INTC1_Reg = 0b00000000;

	//------------------------------- Start SPI initialization ------------------------------------------


	//always uses SPI transaction 
	_pspi = &SPI1;

	// Make sure we select the right pins.  
	_pspi->setMISO(_miso);
	_pspi->setMOSI(_mosi);
	_pspi->setSCK(_sclk);
	pinMode(_cs, OUTPUT);
	_pspi->begin();
	digitalWrite(_cs, HIGH);

	//set SPI SPEED, starting at low speed, after init will raise up!
#if defined(SPI_HAS_TRANSACTION)
	_SPITransactionSpeed = 4000000UL;//we start in low speed here!
#endif

	writeCommand(RA8875_PWRR);
	_writeData(RA8875_PWRR_SOFTRESET);
	delay(20);
	_writeData(RA8875_PWRR_NORMAL);
	delay(200);

	//SPI initialization done
	if (_errorCode != 0) return;//ouch, error/s.Better stop here!
	_initialize();//----->Time to Initialize the RA8875!

	if (_displaySize == Adafruit_800x480) GPIOX(true);
}

/************************* Initialization *********************************/

/**************************************************************************/
/*!
	  Hardware initialization of RA8875 and turn on
	  [private]
*/
/**************************************************************************/
FLASHMEM void RA8875::_initialize()
{
	_inited = false;
	// HACK to setup SPI MODE 3
	/*	SPI1.beginTransaction(SPISettings(_SPIMaxSpeed, MSBFIRST, SPI_MODE3));
		SPI1.transfer(0);
		SPI1.endTransaction();
		delay(1);
	*/

	if (_rst == 255) {// soft reset time ?
		writeCommand(RA8875_PWRR);
		_writeData(RA8875_PWRR_SOFTRESET);
		delay(20);
		_writeData(RA8875_PWRR_NORMAL);
		delay(200);
	}

	//set the sysClock
	_setSysClock(initStrings[_initIndex][0], initStrings[_initIndex][1], initStrings[_initIndex][2]);

	//color space setup
	if (_color_bpp < 16) {//256
		_writeRegister(RA8875_SYSR, 0x00);//256
		_colorIndex = 3;
	}
	else {
		_writeRegister(RA8875_SYSR, 0x0C);//65K
		_colorIndex = 0;
	}

	_writeRegister(RA8875_HDWR, initStrings[_initIndex][3]);		//LCD Horizontal Display Width Register
	_writeRegister(RA8875_HNDFTR, initStrings[_initIndex][4]);	//Horizontal Non-Display Period Fine Tuning Option Register
	_writeRegister(RA8875_HNDR, initStrings[_initIndex][5]);		//LCD Horizontal Non-Display Period Register
	_writeRegister(RA8875_HSTR, initStrings[_initIndex][6]);		//HSYNC Start Position Register
	_writeRegister(RA8875_HPWR, initStrings[_initIndex][7]);		//HSYNC Pulse Width Register
	_writeRegister(RA8875_VDHR0, initStrings[_initIndex][8]);	//LCD Vertical Display Height Register0
	_writeRegister(RA8875_VDHR0 + 1, initStrings[_initIndex][9]);	//LCD Vertical Display Height Register1
	_writeRegister(RA8875_VNDR0, initStrings[_initIndex][10]);	//LCD Vertical Non-Display Period Register 0
	_writeRegister(RA8875_VNDR0 + 1, initStrings[_initIndex][11]);	//LCD Vertical Non-Display Period Register 1
	_writeRegister(RA8875_VSTR0, initStrings[_initIndex][12]);	//VSYNC Start Position Register 0
	_writeRegister(RA8875_VSTR0 + 1, initStrings[_initIndex][13]);	//VSYNC Start Position Register 1
	_writeRegister(RA8875_VPWR, initStrings[_initIndex][14]);	//VSYNC Pulse Width Register
	_updateActiveWindow(true);									//set the whole screen as active
	//clearActiveWindow();
	delay(10); //100
	// setIntFontCoding(DEFAULTINTENCODING);//set default internal font encoding
	// setFont(INTFONT);	//set internal font use
	//postburner PLL!
	_setSysClock(sysClockPar[_initIndex][0], sysClockPar[_initIndex][1], initStrings[_initIndex][2]);
	_inited = true;
	//from here we will go at high speed!
#if defined(SPI_HAS_TRANSACTION)
	if (_SPIMaxSpeed == (uint32_t)-1) {
#if defined(__MKL26Z64__)
		_SPIMaxSpeed = (_pspi != &SPI) ? MAXSPISPEED2 : MAXSPISPEED;
#else			
		_SPIMaxSpeed = MAXSPISPEED;
#endif
	}
	if (_SPIMaxReadSpeed == (uint32_t)-1) {
		_SPIMaxReadSpeed = MAXSPIREADSPEED;
	}
	_SPITransactionSpeed = _SPIMaxSpeed;
	//Serial.printf("SPI Transaction speed: %d Max Speed:%d\n", _SPITransactionSpeed, _SPIMaxSpeed);
#endif
#if defined(_FASTCPU)
	_slowDownSPI(false);
#else
#if defined(SPI_HAS_TRANSACTION)
#else
#if defined(___DUESTUFF) && defined(SPI_DUE_MODE_EXTENDED)
	SPI1.setClockDivider(_cs, SPI_SPEED_WRITE);
#else
	SPI1.setClockDivider(SPI_SPEED_WRITE);
#endif
#endif
#endif
	delay(1);
	clearMemory();//clearMemory(true);
	delay(1);
	displayOn(true);//turn On Display
	delay(1);
	fillWindow(COLOR_BACKGROUND);//set screen black
	backlight(true);
	setRotation(_RA8875_DEFAULTSCRROT);
	//_setTextMode(false);
	setActiveWindow();
#if defined(_RA8875_DEFAULTTXTBKGRND)
#if defined(USE_RA8875_SEPARATE_TEXT_COLOR)
	setForegroundColor(_TXTForeColor);
	setBackgroundColor(_TXTBackColor);
#else
	setForegroundColor(_RA8875_DEFAULTTXTFRGRND);
	setBackgroundColor(_RA8875_DEFAULTTXTBKGRND);
#endif
	_backTransparent = false;
	_FNCR1_Reg &= ~(1 << 6);//clear
#else
#if defined(USE_RA8875_SEPARATE_TEXT_COLOR)
	setForegroundColor(_TXTForeColor);
#else
	setForegroundColor(_RA8875_DEFAULTTXTFRGRND);
#endif
	_backTransparent = true;
	_FNCR1_Reg |= (1 << 6);//set
#endif
	_writeRegister(RA8875_FNCR1, _FNCR1_Reg);

	setCursor(0, 0);
}

/**************************************************************************/
/*!
	  This function set the sysClock accordly datasheet
	  Parameters:
	  pll1: PLL Control Register 1
	  pll2: PLL Control Register 2
	  pixclk: Pixel Clock Setting Register
	  [private]
*/
/**************************************************************************/
FLASHMEM void RA8875::_setSysClock(uint8_t pll1, uint8_t pll2, uint8_t pixclk)
{
	_writeRegister(RA8875_PLLC1, pll1);////PLL Control Register 1
	delay(1);
	_writeRegister(RA8875_PLLC1 + 1, pll2);////PLL Control Register 2
	delay(1);
	_writeRegister(RA8875_PCSR, pixclk);//Pixel Clock Setting Register
	delay(1);
}

/**************************************************************************/
/*!
	  This return a byte with the error code/s:
	  bit--------------------------------------------------------------------
	  0:	The display it's not supported!
	  1:	The MOSI or MISO or SCLK choosed for Teensy it's out permitted range!
	  2:	The CS pin you selected it's out permitted range!
	  3:	You have to upgrade to Teensyduino 1.22 or better to use this feature!
	  4:
	  5:
	  6:
	  7:
	 0b00000000 = no error
*/
/**************************************************************************/
uint8_t RA8875::errorCode(void)
{
	return _errorCode;
}

/**************************************************************************/
/*!
	return true when register has done the job, otherwise false.
*/
/**************************************************************************/
boolean RA8875::_waitPoll(uint8_t regname, uint8_t waitflag, uint8_t timeout)
{
	uint8_t temp;
	unsigned long start_time = millis();

	while (1) {
		temp = _readRegister(regname);
		if (!(temp & waitflag)) {
			//unsigned long delta_time = millis() - timeout;
			//if ((delta_time > 10) || (waitflag == RA8875_DCR_CIRCLE_STATUS)) Serial.printf("+_waitPoll %x %x %d\n", temp, waitflag, delta_time);
			return true;
		}
		if ((millis() - start_time) > timeout) {
			//Serial.printf("TO _waitPoll %x %x\n", temp, waitflag);
			return false;//emergency exit!
		}
	}
	return false;
}

/**************************************************************************/
/*!
	Just another specified wait routine until job it's done
	Parameters:
	res:
	0x80(for most operations),
	0x40(BTE wait),
	0x01(DMA wait)
*/
/**************************************************************************/
FLASHMEM void RA8875::_waitBusy(uint8_t res)
{
	uint8_t temp;
	unsigned long start = millis();//M.Sandercock
	do {
		if (res == 0x01) writeCommand(RA8875_DMACR);//dma
		temp = readStatus();
		if ((millis() - start) > 10) return;
	} while ((temp & res) == res);
}

/**************************************************************************/
/*!
		Clear memory (different from fillWindow!)
		Parameters:
		full: true(clear all memory), false(clear active window only)
		When in multilayer it automatically clear L1 & L1 and switch back to current layer
*/
/**************************************************************************/
/*
FLASHMEM void RA8875::clearMemory(boolean full)
{
	uint8_t temp = 0b10000000;
	if (!full && !_useMultiLayers) temp |= (1 << 6);//set 6
	_writeRegister(RA8875_MCLR,temp);
	_waitBusy(0x80);
	if (full && _useMultiLayers){
		temp = 0b10000000;
		uint8_t templ = _currentLayer;//remember current layer
		if (templ > 0){//we are in L2
			writeTo(L1);//switch to L1
		} else {
			writeTo(L2);//switch to L2
		}
		_writeRegister(RA8875_MCLR,temp);
		_waitBusy(0x80);
		if (templ > 0){//we was in L2
			writeTo(L2);//switch back to L2
		} else {
			writeTo(L1);//switch back to L1
		}
	}
}
*/

/**************************************************************************/
/*!
		Clear memory (different from fillWindow!)
		Parameters:
		stop: stop clear memory operation
*/
/**************************************************************************/
FLASHMEM void RA8875::clearMemory(bool stop)
{
	uint8_t temp;
	temp = _readRegister(RA8875_MCLR);
	stop == true ? temp &= ~(1 << 7) : temp |= (1 << 7);
	_writeData(temp);
	if (!stop) _waitBusy(0x80);
}

/**************************************************************************/
/*!
		Clear the active window
		Parameters:
		full: false(clear current window), true clear full window
*/
/**************************************************************************/
FLASHMEM void RA8875::clearActiveWindow(bool full)
{
	uint8_t temp;
	temp = _readRegister(RA8875_MCLR);
	full == true ? temp &= ~(1 << 6) : temp |= (1 << 6);
	_writeData(temp);
	//_waitBusy(0x80);
}

/**************************************************************************/
/*!
		Clear width BG color
		Parameters:
		bte: false(clear width BTE BG color), true(clear width font BG color)
*/
/**************************************************************************/
FLASHMEM void RA8875::clearWidthColor(bool bte)
{
	uint8_t temp;
	temp = _readRegister(RA8875_MCLR);
	bte == true ? temp &= ~(1 << 0) : temp |= (1 << 0);
	_writeData(temp);
	//_waitBusy(0x80);
}

/**************************************************************************/
/*!
	  turn display on/off
*/
/**************************************************************************/
FLASHMEM void RA8875::displayOn(boolean on)
{
	on == true ? _writeRegister(RA8875_PWRR, RA8875_PWRR_NORMAL | RA8875_PWRR_DISPON) : _writeRegister(RA8875_PWRR, RA8875_PWRR_NORMAL | RA8875_PWRR_DISPOFF);
}

/**************************************************************************/
/*!
		Set the Active Window
		Parameters:
		XL: Horizontal Left
		XR: Horizontal Right
		YT: Vertical TOP
		YB: Vertical Bottom
*/
/**************************************************************************/
FLASHMEM void RA8875::setActiveWindow(int16_t XL, int16_t XR, int16_t YT, int16_t YB)
{
	if (_portrait) { swapvals(XL, YT); swapvals(XR, YB); }

	if (XR >= RA8875_WIDTH) XR = RA8875_WIDTH;
	if (YB >= RA8875_HEIGHT) YB = RA8875_HEIGHT;

	_activeWindowXL = XL; _activeWindowXR = XR;
	_activeWindowYT = YT; _activeWindowYB = YB;
	_updateActiveWindow(false);
}

/**************************************************************************/
/*!
		Set the Active Window as FULL SCREEN
*/
/**************************************************************************/
FLASHMEM void RA8875::setActiveWindow(void)
{
	_activeWindowXL = 0; _activeWindowXR = RA8875_WIDTH;
	_activeWindowYT = 0; _activeWindowYB = RA8875_HEIGHT;
	if (_portrait) { swapvals(_activeWindowXL, _activeWindowYT); swapvals(_activeWindowXR, _activeWindowYB); }
	_updateActiveWindow(true);
}

/**************************************************************************/
/*!
		Set the Active Window
		Parameters:
		XL: Horizontal Left
		XR: Horizontal Right
		YT: Vertical TOP
		YB: Vertical Bottom
*/
/**************************************************************************/
FLASHMEM void RA8875::getActiveWindow(int16_t& XL, int16_t& XR, int16_t& YT, int16_t& YB)//0.69b24
{
	XL = _activeWindowXL; XR = _activeWindowXR;
	YT = _activeWindowYT; YB = _activeWindowYB;
}

/**************************************************************************/
/*!
		Return the max tft width.
		Parameters:
		absolute: if true will return the phisical width
*/
/**************************************************************************/
uint16_t RA8875::width(bool absolute) const
{
	if (absolute) return RA8875_WIDTH;
	return _width;
}

/**************************************************************************/
/*!
		Return the max tft height.
		Parameters:
		absolute: if true will return the phisical height
*/
/**************************************************************************/
uint16_t RA8875::height(bool absolute) const
{
	if (absolute) return RA8875_HEIGHT;
	return _height;
}


/**************************************************************************/
/*!
		Change the mode between graphic and text
		Parameters:
		m: can be GRAPHIC or TEXT
		[private]
*/
/**************************************************************************/
FLASHMEM void RA8875::_setTextMode(bool m)
{
	if (m == _textMode) return;
	writeCommand(RA8875_MWCR0);
	//if (m != 0){//text
	if (m) {//text
		_MWCR0_Reg |= (1 << 7);
		_textMode = true;
	}
	else {//graph
		_MWCR0_Reg &= ~(1 << 7);
		_textMode = false;
	}
	_writeData(_MWCR0_Reg);
}

/**************************************************************************/
/*!
	  Change the beam scan direction on display
	  Parameters:
	  invertH: true(inverted),false(normal) horizontal
	  invertV: true(inverted),false(normal) vertical
*/
/**************************************************************************/
FLASHMEM void RA8875::_scanDirection(boolean invertH, boolean invertV)
{
	invertH == true ? _DPCR_Reg |= (1 << 3) : _DPCR_Reg &= ~(1 << 3);
	invertV == true ? _DPCR_Reg |= (1 << 2) : _DPCR_Reg &= ~(1 << 2);
	_writeRegister(RA8875_DPCR, _DPCR_Reg);
}

/**************************************************************************/
/*!
	  Change the rotation of the screen
	  Parameters:
	  rotation:
		0 = default
		1 = 90
		2 = 180
		3 = 270
*/
/**************************************************************************/
FLASHMEM void RA8875::setRotation(uint8_t rotation)//0.69b32 - less code
{
	_rotation = rotation % 4; //limit to the range 0-3
	switch (_rotation) {
	case 0:
		//default, connector to bottom
		_portrait = false;
		_scanDirection(0, 0);

		break;
	case 1:
		//90
		_portrait = true;
		_scanDirection(1, 0);

		break;
	case 2:
		//180
		_portrait = false;
		_scanDirection(1, 1);

		break;
	case 3:
		//270
		_portrait = true;
		_scanDirection(0, 1);

		break;
	}
	if (_portrait) {
		_width = RA8875_HEIGHT;
		_height = RA8875_WIDTH;
		_FNCR1_Reg |= (1 << 4);
	}
	else {
		_width = RA8875_WIDTH;
		_height = RA8875_HEIGHT;
		_FNCR1_Reg &= ~(1 << 4);
	}
	_writeRegister(RA8875_FNCR1, _FNCR1_Reg);//0.69b21

	setClipRect();
}

/**************************************************************************/
/*!
	  Get rotation setting
*/
/**************************************************************************/
uint8_t RA8875::getRotation()
{
	return _rotation;
}

/**************************************************************************/
/*!
	  true if rotation 1 or 3
*/
/**************************************************************************/
boolean RA8875::isPortrait(void)
{
	return _portrait;
}

/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+								TEXT STUFF											 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/
/**************************************************************************/
/*!		Upload user custom char or symbol to CGRAM, max 255
		Parameters:
		symbol[]: an 8bit x 16 char in an array. Must be exact 16 bytes
		address: 0...255 the address of the CGRAM where to store the char
*/
/**************************************************************************/
FLASHMEM void RA8875::uploadUserChar(const uint8_t symbol[], uint8_t address)
{
	uint8_t tempMWCR1 = _readRegister(RA8875_MWCR1);//thanks MorganSandercock
	uint8_t i;
	if (_textMode) _setTextMode(false);//we are in text mode?
	_writeRegister(RA8875_CGSR, address);
	writeTo(CGRAM);
	writeCommand(RA8875_MRWC);
	for (i = 0;i < 16;i++) {
		_writeData(symbol[i]);
	}
	_writeRegister(RA8875_MWCR1, tempMWCR1);//restore register
}

/**************************************************************************/
/*!		Retrieve and print to screen the user custom char or symbol
		User have to store a custom char before use this function
		Parameters:
		address: 0...255 the address of the CGRAM where char it's stored
		wide:0 for single 8x16 char, if you have wider chars that use
		more than a char slot they can be showed combined (see examples)
*/
/**************************************************************************/
FLASHMEM void RA8875::showUserChar(uint8_t symbolAddrs, uint8_t wide)
{
	// // if (!_textMode) _setTextMode(true);//we are in graph mode?
	// // uint8_t oldReg1State = _FNCR0_Reg;
	// // uint8_t oldReg2State = 0;
	//  uint8_t i;
	// // oldReg1State |= (1 << 7);//set to CGRAM
	// // oldReg1State |= (1 << 5);//TODO:check this (page 19)

	// // _writeRegister(RA8875_FNCR0, oldReg1State);
	// // if (_scaling) {//reset scale (not compatible with this!)
	// // 	oldReg2State = _FNCR1_Reg;
	// // 	oldReg2State &= ~(0xF); // clear bits from 0 to 3
	// // 	_writeRegister(RA8875_FNCR1, oldReg2State);
	// // }

	// //layers?

	// if (_useMultiLayers) {
	// 	if (_currentLayer == 0) {
	// 		writeTo(L1);
	// 	}
	// 	else {
	// 		writeTo(L2);
	// 	}
	// }
	// else {
	// 	//writeTo(L1);
	// }

	// writeCommand(RA8875_MRWC);
	// _writeData(symbolAddrs);
	// if (wide > 0) {
	// 	for (i = 1;i <= wide;i++) {
	// 		_writeData(symbolAddrs + i);
	// 	}
	// }
	// // if (oldReg2State != 0) _writeRegister(RA8875_FNCR1, _FNCR1_Reg);//put back scale as it was
	// // if (oldReg1State != _FNCR0_Reg) _writeRegister(RA8875_FNCR0, _FNCR0_Reg);//put back state
}

/**************************************************************************/
/*!
		Set internal Font Encoding
		Parameters:
		f: ISO_IEC_8859_1, ISO_IEC_8859_2, ISO_IEC_8859_3, ISO_IEC_8859_4
		default: ISO_IEC_8859_1
*/
/**************************************************************************/
FLASHMEM void RA8875::setIntFontCoding(enum RA8875fontCoding f)
{
	// uint8_t temp = _FNCR0_Reg;
	// temp &= ~((1 << 1) | (1 << 0));// Clear bits 1 and 0
	// switch (f) {
	// case ISO_IEC_8859_1:
	// 	//do nothing
	// 	break;
	// case ISO_IEC_8859_2:
	// 	temp |= (1 << 0);
	// 	break;
	// case ISO_IEC_8859_3:
	// 	temp |= (1 << 1);
	// 	break;
	// case ISO_IEC_8859_4:
	// 	temp |= ((1 << 1) | (1 << 0));// Set bits 1 and 0
	// 	break;
	// default:
	// 	return;
	// }
	// _FNCR0_Reg = temp;
	// _writeRegister(RA8875_FNCR0, _FNCR0_Reg);
}

/**************************************************************************/
/*!
		External Font Rom setup
		This will not phisically change the register but should be called before setFont(EXTFONT)!
		You should use this values accordly Font ROM datasheet!
		Parameters:
		ert: ROM Type          (GT21L16T1W, GT21H16T1W, GT23L16U2W, GT30H24T3Y, GT23L24T3Y, GT23L24M1Z, GT23L32S4W, GT30H32S4W)
		erc: ROM Font Encoding (GB2312, GB12345, BIG5, UNICODE, ASCII, UNIJIS, JIS0208, LATIN)
		erf: ROM Font Family   (STANDARD, ARIAL, ROMAN, BOLD)
*/
/**************************************************************************/
FLASHMEM void RA8875::setExternalFontRom(enum RA8875extRomType ert, enum RA8875extRomCoding erc, enum RA8875extRomFamily erf)
{
	// if (!_textMode) _setTextMode(true);
	// _SFRSET_Reg = _readRegister(RA8875_FNCR0);;//just to preserve the reg in case something wrong
	// uint8_t temp = 0b00000000;
	// switch (ert) { //type of rom
	// case GT21L16T1W:
	// case GT21H16T1W:
	// 	temp &= 0x1F;
	// 	break;
	// case GT23L16U2W:
	// case GT30L16U2W:
	// case ER3301_1:
	// 	temp &= 0x1F; temp |= 0x20;
	// 	break;
	// case GT23L24T3Y:
	// case GT30H24T3Y:
	// case ER3303_1://encoding GB12345
	// 	temp &= 0x1F; temp |= 0x40;
	// 	break;
	// case GT23L24M1Z:
	// 	temp &= 0x1F; temp |= 0x60;
	// 	break;
	// case GT23L32S4W:
	// case GT30H32S4W:
	// case GT30L32S4W:
	// case ER3304_1://encoding GB2312
	// 	temp &= 0x1F; temp |= 0x80;
	// 	break;
	// default:
	// 	_TXTparameters &= ~(1 << 0);//wrong type, better avoid for future
	// 	return;//cannot continue, exit
	// }
	// _EXTFNTrom = ert;
	// switch (erc) {	//check rom font coding
	// case GB2312:
	// 	temp &= 0xE3;
	// 	break;
	// case GB12345:
	// 	temp &= 0xE3; temp |= 0x04;
	// 	break;
	// case BIG5:
	// 	temp &= 0xE3; temp |= 0x08;
	// 	break;
	// case UNICODE:
	// 	temp &= 0xE3; temp |= 0x0C;
	// 	break;
	// case ASCII:
	// 	temp &= 0xE3; temp |= 0x10;
	// 	break;
	// case UNIJIS:
	// 	temp &= 0xE3; temp |= 0x14;
	// 	break;
	// case JIS0208:
	// 	temp &= 0xE3; temp |= 0x18;
	// 	break;
	// case LATIN:
	// 	temp &= 0xE3; temp |= 0x1C;
	// 	break;
	// default:
	// 	_TXTparameters &= ~(1 << 0);//wrong coding, better avoid for future
	// 	return;//cannot continue, exit
	// }
	// _EXTFNTcoding = erc;
	// _SFRSET_Reg = temp;
	// setExtFontFamily(erf, false);
	// _TXTparameters |= (1 << 0); //bit set 0

	//_writeRegister(RA8875_SFRSET,_SFRSET_Reg);//0x2F
	//delay(4);
}

FLASHMEM void RA8875::fontRomSpeed(uint8_t sp)
{
	_writeRegister(0x28, sp);
}

/**************************************************************************/
/*!
		select the font family for the external Font Rom Chip
		Parameters:
		erf: STANDARD, ARIAL, ROMAN, BOLD
		setReg:
		true(send phisically the register, useful when you change
		family after set setExternalFontRom)
		false:(change only the register container, useful during config)
		NOTE: works only when external font rom it's active
*/
/**************************************************************************/
FLASHMEM void RA8875::setExtFontFamily(enum RA8875extRomFamily erf, boolean setReg)
{
	// if (_FNTsource == EXTFONT) {//only on EXTFONT ROM fonts!
	// 	_EXTFNTfamily = erf;
	// 	_SFRSET_Reg &= ~(0x03); // clear bits from 0 to 1
	// 	switch (erf) {	//check rom font family
	// 	case STANDARD:
	// 		_SFRSET_Reg &= 0xFC;
	// 		break;
	// 	case ARIAL:
	// 		_SFRSET_Reg &= 0xFC; _SFRSET_Reg |= 0x01;
	// 		break;
	// 	case ROMAN:
	// 		_SFRSET_Reg &= 0xFC; _SFRSET_Reg |= 0x02;
	// 		break;
	// 	case BOLD:
	// 		_SFRSET_Reg |= ((1 << 1) | (1 << 0)); // set bits 1 and 0
	// 		break;
	// 	default:
	// 		_EXTFNTfamily = STANDARD; _SFRSET_Reg &= 0xFC;
	// 		return;
	// 	}
	// 	if (setReg) _writeRegister(RA8875_SFRSET, _SFRSET_Reg);
	// }
}


/**************************************************************************/
/*!
		choose from internal/external (if exist) Font Rom
		Parameters:
		s: Font source (INTFONT,EXTFONT)
*/
/**************************************************************************/
FLASHMEM void RA8875::setFont(enum RA8875fontSource s)
{
	_use_int_font = 1;
	_use_tfont = 0;

	if (!_textMode) _setTextMode(true);//we are in graph mode?
	_TXTparameters &= ~(1 << 7);//render OFF
	if (s == INTFONT) {
		_setFNTdimensions(0);
		//check the font coding
		if (bitRead(_TXTparameters, 0) == 1) {//0.96b22 _extFontRom = true
			setFontSize(X16);
			_writeRegister(RA8875_SFRSET, 0b00000000);//_SFRSET_Reg
		}
		_FNCR0_Reg &= ~((1 << 7) | (1 << 5));// Clear bits 7 and 5 (set internal CGROM)
		_writeRegister(RA8875_FNCR0, _FNCR0_Reg);
		_FNTsource = s;
		delay(1);
	}
	else if (s == EXTFONT) {
		if (bitRead(_TXTparameters, 0) == 1) {//0.96b22 _extFontRom = true
			_FNTsource = s;
			//now switch
			_FNCR0_Reg &= ~(1 << 7);//clearBit 7
			_FNCR0_Reg |= (1 << 5);//setBit 5
			_writeRegister(RA8875_FNCR0, _FNCR0_Reg);//0x21
			delay(1);
			_writeRegister(RA8875_SFCLR, 0x03);//0x02 Serial Flash/ROM CLK frequency/2
			setFontSize(X16);
			_writeRegister(RA8875_SFRSET, _SFRSET_Reg);//at this point should be already set
			delay(4);
			_writeRegister(RA8875_SROC, 0x28);// 0x28 rom 0,24bit adrs,wave 3,1 byte dummy,font mode, single mode 00101000
			delay(4);
		}
		else {
			setFont(INTFONT);
			_setFNTdimensions(0);
		}
	}
	else {
		return;
	}
	_spaceCharWidth = _FNTwidth;

}


FLASHMEM void RA8875::_setFNTdimensions(uint8_t index)
{
	_FNTwidth = fontDimPar[index][0];
	_FNTheight = fontDimPar[index][1];
	_FNTbaselineLow = fontDimPar[index][2];
	_FNTbaselineTop = fontDimPar[index][3];
}

/**************************************************************************/
/*!
		choose an external font that will be rendered
		Of course slower that internal fonts!
		Parameters:
		*font: &myfont
*/
/**************************************************************************/
FLASHMEM void RA8875::setFont(const tFont* font)
{
	_use_tfont = 1;
	_use_int_font = 0;

	_currentFont = font;
	_FNTheight = _currentFont->font_height;
	_FNTwidth = _currentFont->font_width;//if 0 it's variable width font

	_FNTcompression = _currentFont->rle;
	//get all needed infos
	if (_FNTwidth > 0) {
		_spaceCharWidth = _FNTwidth;
	}
	else {
		//_FNTwidth will be 0 to inform other functions that this it's a variable w font
		// We just get the space width now...
		int temp = _getCharCode(0x20);
		if (temp > -1) {
#if defined(_FORCE_PROGMEM__)
#if defined(ESP8266)
			_spaceCharWidth = FPSTR(&_currentFont->chars[temp].image->image_width);
#else
			_spaceCharWidth = PROGMEM_read(&_currentFont->chars[temp].image->image_width);
#endif
#else
			_spaceCharWidth = (_currentFont->chars[temp].image->image_width);
#endif
		}
		else {
			//font malformed, doesn't have needed space parameter
			//will return to system font
			setFont(INTFONT);
			return;
		}
	}
	_scaleX = 1; _scaleY = 1;//reset font scale
	//setFontScale(0);
	_TXTparameters |= (1 << 7);//render ON
}

/**************************************************************************/
/*!
		Enable/Disable the Font Full Alignemet feature (default off)
		Parameters:
		align: true,false
		Note: not active with rendered fonts
*/
/**************************************************************************/
FLASHMEM void RA8875::setFontFullAlign(boolean align)
{
	if (bitRead(_TXTparameters, 7) == 0) {
		align == true ? _FNCR1_Reg |= (1 << 7) : _FNCR1_Reg &= ~(1 << 7);
		_writeRegister(RA8875_FNCR1, _FNCR1_Reg);
	}
}



/**************************************************************************/
/*!
		Set the Text position for write Text only.
		Parameters:
		x:horizontal in pixels or CENTER(of the screen)
		y:vertical in pixels or CENTER(of the screen)
		autocenter:center text to choosed x,y regardless text lenght
		false: |ABCD
		true:  AB|CD
		NOTE: works with any font
*/
/**************************************************************************/
FLASHMEM void RA8875::setCursor(int16_t x, int16_t y)
{
	if (x < 0) x = 0;
	if (y < 0) y = 0;

	x = x * 2;
	y = y * 2;

	_cursorX = x;
	_cursorY = y;

	//if _relativeCenter or _absoluteCenter do not apply to registers yet!
	// Have to go to _textWrite first to calculate the lenght of the entire string and recalculate the correct x,y
	if (_relativeCenter || _absoluteCenter) return;
	if (bitRead(_TXTparameters, 7) == 0) _textPosition(x, y, false);
}

/**************************************************************************/
/*!
		Set the x,y position for text only
		Parameters:
		x: horizontal pos in pixels
		y: vertical pos in pixels
		update: true track the actual text position internally
		note: not active with rendered fonts, just set x,y internal tracked param
		[private]
*/
/**************************************************************************/
FLASHMEM void RA8875::_textPosition(int16_t x, int16_t y, bool update)
{

#if defined(FORCE_RA8875_TXTREND_FOLLOW_CURS)
	_writeRegister(RA8875_F_CURXL, (x & 0xFF));
	_writeRegister(RA8875_F_CURXH, (x >> 8));
	_writeRegister(RA8875_F_CURYL, (y & 0xFF));
	_writeRegister(RA8875_F_CURYH, (y >> 8));
#else
	if (bitRead(_TXTparameters, 7) == 0) {
		_writeRegister(RA8875_F_CURXL, (x & 0xFF));
		_writeRegister(RA8875_F_CURXH, (x >> 8));
		_writeRegister(RA8875_F_CURYL, (y & 0xFF));
		_writeRegister(RA8875_F_CURYH, (y >> 8));
	}
#endif
	if (update) { _cursorX = x; _cursorY = y; }
}

/**************************************************************************/
/*!
		Give you back the current text cursor position by reading inside RA8875
		Parameters:
		x: horizontal pos in pixels
		y: vertical pos in pixels
		note: works also with rendered fonts
		USE: xxx.getCursor(myX,myY);
*/
/**************************************************************************/
// FLASHMEM void RA8875::getCursor(int16_t& x, int16_t& y)
// {
// 	if (bitRead(_TXTparameters, 7) == 1) {
// 		getCursorFast(x, y);
// 	}
// 	else {
// 		uint8_t t1, t2, t3, t4;
// 		t1 = _readRegister(RA8875_F_CURXL);
// 		t2 = _readRegister(RA8875_F_CURXH);
// 		t3 = _readRegister(RA8875_F_CURYL);
// 		t4 = _readRegister(RA8875_F_CURYH);
// 		x = (t2 << 8) | (t1 & 0xFF);
// 		y = (t4 << 8) | (t3 & 0xFF);
// 		if (_portrait && _use_default) swapvals(x, y);
// 	}
// }

/**************************************************************************/
/*!
		Give you back the current text cursor position as tracked by library (fast)
		Parameters:
		x: horizontal pos in pixels
		y: vertical pos in pixels
		note: works also with rendered fonts
		USE: xxx.getCursor(myX,myY);
*/
/**************************************************************************/
// FLASHMEM void RA8875::getCursorFast(int16_t& x, int16_t& y)
// {
// 	x = _cursorX;
// 	y = _cursorY;
// 	if (_portrait && _use_default) swapvals(x, y);
// }

int16_t RA8875::getCursorX(void)
{
	if (_portrait && _use_default) return _cursorY/2;
	return _cursorX/2;
}

int16_t RA8875::getCursorY(void)
{
	if (_portrait && _use_default) return _cursorX/2;
	return _cursorY/2;
}

/**************************************************************************/
/*!
		set the text color and his background
		Parameters:
		fcolor: 16bit foreground color (text) RGB565
		bcolor: 16bit background color RGB565
		NOTE: will set background trasparent OFF
		It also works with rendered fonts.
*/
/**************************************************************************/
FLASHMEM void RA8875::setTextColor(uint16_t fcolor, uint16_t bcolor)
{

#if defined(USE_RA8875_SEPARATE_TEXT_COLOR)
	if (fcolor != _TXTForeColor) {
		_TXTForeColor = fcolor;
		setForegroundColor(fcolor);
	}
	if (bcolor != _TXTBackColor) {
		_TXTBackColor = bcolor;
		setBackgroundColor(bcolor);
	}
	_backTransparent = false;
#else
	_backTransparent = false;
	if (fcolor != _foreColor) setForegroundColor(fcolor);
	if (bcolor != _backColor) setBackgroundColor(bcolor);
#endif
	if (bitRead(_TXTparameters, 7) == 0) {
		//_FNCR1_Reg &= ~(1 << 6);//clear
		_FNCR1_Reg |= (1 << 6);//set  no bg  - phtodo
		_writeRegister(RA8875_FNCR1, _FNCR1_Reg);
	}
}

/**************************************************************************/
/*!
		set the text color w transparent background
		Parameters:
		fColor: 16bit foreground color (text) RGB565
		NOTE: will set background trasparent ON
		It also works with rendered fonts.
*/
/**************************************************************************/

FLASHMEM void RA8875::setTextColor(uint16_t fcolor)
{
#if defined(USE_RA8875_SEPARATE_TEXT_COLOR)
	if (fcolor != _TXTForeColor) {
		_TXTForeColor = fcolor;
		setForegroundColor(fcolor);
	}
	_backTransparent = true;
#else
	_backTransparent = true;
	if (fcolor != _foreColor) setForegroundColor(fcolor);
#endif
	if (bitRead(_TXTparameters, 7) == 0) {
		_FNCR1_Reg |= (1 << 6);//set
		_writeRegister(RA8875_FNCR1, _FNCR1_Reg);
	}
}


/**************************************************************************/
/*!
		Set the Text size by it's multiple. normal should=0, max is 3 (x4) for internal fonts
		With Rendered fonts the max scale it's not limited
		Parameters:
		scale: 0..3  -> 0:normal, 1:x2, 2:x3, 3:x4
*/
/**************************************************************************/

/**************************************************************************/
/*!
		Set the Text size by it's multiple. normal should=0, max is 3 (x4) for internal fonts
		With Rendered fonts the max scale it's not limited
		This time you can specify different values for vertical and horizontal
		Parameters:
		xscale: 0..3  -> 0:normal, 1:x2, 2:x3, 3:x4 for internal fonts - 0...xxx for Rendered Fonts
		yscale: 0..3  -> 0:normal, 1:x2, 2:x3, 3:x4 for internal fonts - 0...xxx for Rendered Fonts
*/
/**************************************************************************/
FLASHMEM void RA8875::setFontScale(uint8_t xscale, uint8_t yscale)
{
	// xscale/yscale: 0=1x, 1=2x, 2=3x, 3=4x
	xscale = xscale % 4;
	yscale = yscale % 4;
	_scaling = false;
	if (bitRead(_TXTparameters, 7) == 0) {   // not in render mode
		_FNCR1_Reg &= ~0x0F;               // clear bits 3-0
		_FNCR1_Reg |= (xscale << 2) | yscale;
		_writeRegister(RA8875_FNCR1, _FNCR1_Reg);
	}
	_scaleX = xscale + 1;
	_scaleY = yscale + 1;
	if (_scaleX > 1 || _scaleY > 1) _scaling = true;
}

/**************************************************************************/
/*!
		Normally at every char the cursor advance by one
		You can stop/enable this by using this function
		Parameters:
		on: true(auto advance - default), false:(stop auto advance)
		Note: Inactive with rendered fonts
*/
/**************************************************************************/
FLASHMEM void RA8875::cursorIncrement(bool on)
{
	if (bitRead(_TXTparameters, 7) == 0) {
		on == true ? _MWCR0_Reg &= ~(1 << 1) : _MWCR0_Reg |= (1 << 1);
		bitWrite(_TXTparameters, 1, on);
		_writeRegister(RA8875_MWCR0, _MWCR0_Reg);
	}
}

/**************************************************************************/
/*!
		Choose between 16x16(8x16) - 24x24(12x24) - 32x32(16x32)
		for External Font ROM
		Parameters:
		ts: X16,X24,X32
		Note: Inactive with rendered fonts
		TODO: Modify font size variables accordly font size!
*/
/**************************************************************************/
FLASHMEM void RA8875::setFontSize(enum RA8875tsize ts)
{
	// if (_FNTsource == EXTFONT && bitRead(_TXTparameters, 7) == 0) {
	// 	switch (ts) {
	// 	case X16:
	// 		_FWTSET_Reg &= 0x3F;
	// 		_setFNTdimensions(1);
	// 		break;
	// 	case X24:
	// 		_FWTSET_Reg &= 0x3F; _FWTSET_Reg |= 0x40;
	// 		_setFNTdimensions(2);
	// 		break;
	// 	case X32:
	// 		_FWTSET_Reg &= 0x3F; _FWTSET_Reg |= 0x80;
	// 		_setFNTdimensions(3);
	// 		break;
	// 	default:
	// 		return;
	// 	}
	// 	_EXTFNTsize = ts;
	// 	_writeRegister(RA8875_FWTSET, _FWTSET_Reg);
	// }

}

/**************************************************************************/
/*!
		return the current width of the font in pixel
		If font it's scaled, it will multiply.
		It's a fast business since the register it's internally tracked
		It can also return the usable rows based on the actual fontWidth
		Parameters: inColums (true:returns max colums)
		TODO: modded for Rendered Fonts
*/
/**************************************************************************/
uint8_t RA8875::getFontWidth(boolean inColums)
{
	uint8_t temp;
	if (bitRead(_TXTparameters, 7) == 1) {
		temp = _FNTwidth;
		if (temp < 1) return 0; //variable with
	}
	else {
		temp = (((_FNCR0_Reg >> 2) & 0x3) + 1) * _FNTwidth;
	}
	if (inColums) {
		if (_scaleX < 2) return (_width / temp);
		temp = temp * _scaleX;
		return (_width / temp);
	}
	else {
		if (_scaleX < 2) return temp;
		temp = temp * _scaleX;
		return temp;
	}
}

/**************************************************************************/
/*!
		return the current heigh of the font in pixel
		If font it's scaled, it will multiply.
		It's a fast business since the register it's internally tracked
		It can also return the usable rows based on the actual fontHeight
		Parameters: inRows (true:returns max rows)
		TODO: modded for Rendered Fonts
*/
/**************************************************************************/
uint8_t RA8875::getFontHeight(boolean inRows)
{
	uint8_t temp;
	if (bitRead(_TXTparameters, 7) == 1) {
		temp = _FNTheight;
	}
	else {
		temp = (((_FNCR0_Reg >> 0) & 0x3) + 1) * _FNTheight;
	}
	if (inRows) {
		if (_scaleY < 2) return (_height / temp);
		temp = temp * _scaleY;
		return (_height / temp);
	}
	else {
		if (_scaleY < 2) return temp;
		temp = temp * _scaleY;
		return temp;
	}
}

/**************************************************************************/
/*!
		Choose space in pixels between chars
		Parameters:
		spc: 0...63pix (default 0=off)
		TODO: modded for Rendered Fonts
*/
/**************************************************************************/
FLASHMEM void RA8875::setFontSpacing(uint8_t spc)
{
	if (spc > 0x3F) spc = 0x3F;
	_FNTspacing = spc;
	if (bitRead(_TXTparameters, 7) == 0) {
		_FWTSET_Reg &= 0xC0;
		_FWTSET_Reg |= spc & 0x3F;
		_writeRegister(RA8875_FWTSET, _FWTSET_Reg);
	}

}

FLASHMEM void RA8875::_textWrite(const char* buffer, uint16_t len)
{
	// text write

	if (remote_active)
	{
		uint8_t colors[8];
		uint8_t nbBytes = fillSysexDataColor(colors, 2, _TXTForeColor, _TXTBackColor);
		uint8_t sizeMsg = 2 * 2 + 1 + nbBytes + 1 + len;
		uint8_t* sysexDrawString = (uint8_t*)malloc(sizeMsg);
		if (sysexDrawString == NULL)
		{
#ifdef DEBUG
			LOG.println("malloc failed");
#endif
		}
		else
		{
			// fillSysexData(sysexDrawString, 2, cursor_x, cursor_y , _cursorX, _cursorY);
			fillSysexData(sysexDrawString, 2, _cursorX / 2, _cursorY / 2);
			sysexDrawString[4] = nbBytes;
			memcpy(sysexDrawString + 4 + 1, colors, nbBytes);
			sysexDrawString[4 + 1 + nbBytes] = textsize_x;
			memcpy(sysexDrawString + 4 + 1 + nbBytes + 1, buffer, len);

			sendSysEx(72, sizeMsg, sysexDrawString, true);

			free(sysexDrawString);
		}
		console = false;
	}


	uint16_t i;
	if (len == 0) len = strlen(buffer);//try get the info from the buffer
	if (len == 0) return;//better stop here, the string it really empty!

	uint8_t interlineOffset = 0;

	if (remote_active)
		skipfillrect_webremote = true;

	//phtodo

	if (textsize_x == 1)
		fillRect(_cursorX / 2, _cursorY / 2, strlen(buffer) * 6, 8, _backColor);
	else
		fillRect(_cursorX / 2, _cursorY / 2, strlen(buffer) * 12, 16, _backColor);
	setColor(_TXTForeColor, _TXTBackColor);

	if (remote_active)
		skipfillrect_webremote = false;


	//Loop trough every char and write them one by one...
	for (i = 0;i < len;i++) {
		_charWrite(buffer[i], interlineOffset);					// internal,ROM fonts
	}//end loop
}

// #define SCRATCH_PHYS_X  660
// #define SCRATCH_PHYS_Y  0

// FLASHMEM void RA8875::_textWrite(const char* buffer, uint16_t len)
// {
//     if (remote_active)
//     {
//         uint8_t colors[8];
//         uint8_t nbBytes = fillSysexDataColor(colors, 2, _TXTForeColor, _TXTBackColor);
//         uint8_t sizeMsg = 2 * 2 + 1 + nbBytes + 1 + len;
//         uint8_t* sysexDrawString = (uint8_t*)malloc(sizeMsg);
//         if (sysexDrawString == NULL)
//         {
// #ifdef DEBUG
//             LOG.println("malloc failed");
// #endif
//         }
//         else
//         {
//             fillSysexData(sysexDrawString, 2, _cursorX / 2, _cursorY / 2);
//             sysexDrawString[4] = nbBytes;
//             memcpy(sysexDrawString + 4 + 1, colors, nbBytes);
//             sysexDrawString[4 + 1 + nbBytes] = textsize_x;
//             memcpy(sysexDrawString + 4 + 1 + nbBytes + 1, buffer, len);
//             sendSysEx(72, sizeMsg, sysexDrawString, true);
//             free(sysexDrawString);
//         }
//         console = false;
//     }

//     if (len == 0) len = strlen(buffer);
//     if (len == 0) return;

//     const bool    big     = (textsize_x != 1);

//     // Logical pixels (what fillRect expects)
//     const int16_t glyphH  = big ? 14 : 7;    // logical px — the real glyph rows
//     const int16_t cellW   = big ? 16 : 8;    // logical px — HW cell width (32/16 physical)
//     const int16_t charAdv = big ? (_customCharAdvance + 12) : _customCharAdvance;
//     // charAdv is already physical, convert to logical for fillRect
//     const int16_t charAdvL = charAdv / 2;

//     // _cursorX/_cursorY are physical
//     const int16_t destY = _cursorY;
//           int16_t destX = _cursorX;

//     uint8_t savedFNCR1 = _FNCR1_Reg;
//     _FNCR1_Reg |= (1 << 6);
//     _writeRegister(RA8875_FNCR1, _FNCR1_Reg);

//     if (remote_active) skipfillrect_webremote = true;

//     for (uint16_t i = 0; i < len; i++)
//     {
//         const char c = buffer[i];
//         if (c == 13) continue;

//         // No fillRect anywhere — scratch gets fully overwritten by
//         // opaque text engine, destination gets atomically overwritten
//         // by BTE. No intermediate blank state anywhere.
//         setColor(_TXTForeColor, _TXTBackColor);
//         _setTextMode(true);

// // Force opaque — setColor/setTextColor may have set bit 6
//         _FNCR1_Reg &= ~(1 << 6);
//         _writeRegister(RA8875_FNCR1, _FNCR1_Reg);

//         _textPosition(SCRATCH_PHYS_X, SCRATCH_PHYS_Y, false);
//         writeCommand(RA8875_MRWC);
//         _writeData((uint8_t)c);
//         _waitBusy(0x80);

// // Wait for vertical blank before BTE copy so the scanline
//         // never catches the destination mid-transfer
//         // RA8875 status bit 5: 0 = display in active period, 1 = non-display (vblank)
//         // uint32_t t = millis();
//         // while (!(readStatus() & (1 << 5))) {
//         //     if (millis() - t > 20) break;  // safety timeout ~1 frame
//         // }

//         BTE_move(
//             SCRATCH_PHYS_X,
//             SCRATCH_PHYS_Y,
//             cellW  * 2,
//             glyphH * 2,
//             destX,
//             destY,
//             1, 1
//         );

//         destX += charAdv;
//     }

//     if (remote_active) skipfillrect_webremote = false;

//     _FNCR1_Reg = savedFNCR1;
//     _writeRegister(RA8875_FNCR1, _FNCR1_Reg);

//     _cursorX = destX;
// }

FLASHMEM void RA8875::_charWrite(const char c, uint8_t offset) {
	if (c == 13) {   // carriage return – ignore
		return;
	}

	// Normal character: set hardware cursor to current logical position
	if (!_textMode) _setTextMode(true);
	_textPosition(_cursorX, _cursorY, false);

	// Write the character
	writeCommand(RA8875_MRWC);
	_writeData(c);
	_waitBusy(0x80);

	// Advance logical cursor 
	if (textsize_x == 1)
		_cursorX += _customCharAdvance;
	else
		_cursorX += _customCharAdvance + 12;
	//phtodo

}


/**************************************************************************/
/*!	PRIVATE
		Search for glyph char code in font array
		It return font index or -1 if not found.
*/
/**************************************************************************/
int RA8875::_getCharCode(uint8_t ch)
{
	int i;
	for (i = 0;i < _currentFont->length;i++) {//search for char code
#if defined(_FORCE_PROGMEM__)
		uint8_t ccode = _currentFont->chars[i].char_code;
		if (ccode == ch) return i;
#else
		if (_currentFont->chars[i].char_code == ch) return i;
#endif
	}//i
	return -1;
}

/**************************************************************************/
/*!	PRIVATE
		This helper loop trough a text string and return how long is (in pixel)
		NOTE: It identify correctly println and /n & /r and forget non present chars
*/
/**************************************************************************/
int16_t RA8875::_STRlen_helper(const char* buffer, uint16_t len)
{
	if (bitRead(_TXTparameters, 7) == 0) {		//_renderFont not active
		return (len * _FNTwidth);
	}
	else {									//_renderFont active
		int charIndex = -1;
		uint16_t i;
		if (len == 0) len = strlen(buffer);		//try to get data from string
		if (len == 0) return 0;					//better stop here
		if (_FNTwidth > 0) {						// fixed width font
			return ((len * _spaceCharWidth));
		}
		else {								// variable width, need to loop trough entire string!
			uint16_t totW = 0;
			for (i = 0;i < len;i++) {			//loop trough buffer
				if (buffer[i] == 32) {			//a space
					totW += _spaceCharWidth;
				}
				else if (buffer[i] != 13 && buffer[i] != 10 && buffer[i] != 32) {//avoid special char
					charIndex = _getCharCode(buffer[i]);
					if (charIndex > -1) {		//found!
#if defined(_FORCE_PROGMEM__)
#if defined(ESP8266)
						totW += (FPSTR(&_currentFont->chars[charIndex].image->image_width));
#else
						totW += (PROGMEM_read(&_currentFont->chars[charIndex].image->image_width));
#endif
#else
						totW += (_currentFont->chars[charIndex].image->image_width);
#endif
					}
				}//inside permitted chars
			}//buffer loop
			return totW;						//return data
		}//end variable w font
	}
}



#if defined(_RA8875_TXTRNDOPTIMIZER)

/**************************************************************************/
/*!	PRIVATE
		Here's the char render engine for uncompressed fonts, it actually render a single char.
		It's actually 2 functions, this one take care of every glyph line
		and perform some optimization second one paint concurrent pixels in chunks.
		To show how optimizations works try uncomment RA8875_VISPIXDEBUG in settings.
		Please do not steal this part of code!
*/
/**************************************************************************/
// FLASHMEM void RA8875::_drawChar_unc(int16_t x, int16_t y, int charW, int index, uint16_t fcolor)
// {
// 	//start by getting some glyph data...
// #if defined(_FORCE_PROGMEM__)
// #if defined(ESP8266)
// 	const uint8_t* charGlyp = FPSTR(&_currentFont->chars[index].image->data);//char data
// 	int			  totalBytes = FPSTR(&_currentFont->chars[index].image->image_datalen);
// #else
// 	const uint8_t* charGlyp = PROGMEM_read(&_currentFont->chars[index].image->data);//char data
// 	int			  totalBytes = PROGMEM_read(&_currentFont->chars[index].image->image_datalen);
// #endif
// #else
// 	const uint8_t* charGlyp = _currentFont->chars[index].image->data;
// 	int			  totalBytes = _currentFont->chars[index].image->image_datalen;
// #endif
// 	int i;
// 	uint8_t temp = 0;
// 	//some basic variable...
// 	uint8_t currentXposition = 0;//the current position of the writing cursor in the x axis, from 0 to charW
// 	uint8_t currentYposition = 1;//the current position of the writing cursor in the y axis, from 1 to _FNTheight
// 	int currentByte = 0;//the current byte in reading (from 0 to totalBytes)
// 	bool lineBuffer[charW];//the temporary line buffer (will be _FNTheight each char)
// 	int lineChecksum = 0;//part of the optimizer
// 	/*
// 	uint8_t bytesInLine = 0;
// 	//try to understand how many bytes in a line
// 	if (charW % 8 == 0) {	// divisible by 8
// 		bytesInLine = charW / 8;
// 	} else {						// when it's divisible by 8?
// 		bytesInLine = charW;
// 		while (bytesInLine % 8) { bytesInLine--;}
// 		bytesInLine = bytesInLine / 8;
// 	}
// 	*/
// 	//the main loop that will read all bytes of the glyph
// 	while (currentByte < totalBytes) {
// 		//read n byte
// #if defined(_FORCE_PROGMEM__)
// #if defined(ESP8266)
// 		temp = FPSTR(&charGlyp[currentByte]);
// #else
// 		temp = PROGMEM_read(&charGlyp[currentByte]);
// #endif
// #else
// 		temp = charGlyp[currentByte];
// #endif
// 		for (i = 7; i >= 0; i--) {
// 			//----------------------------------- exception
// 			if (currentXposition >= charW) {
// 				//line buffer has been filled!
// 				currentXposition = 0;//reset the line x position
// 				if (lineChecksum < 1) {//empty line
// #if defined(RA8875_VISPIXDEBUG)
// 					drawRect(x, y + (currentYposition * _scaleY), charW * _scaleX, _scaleY, RA8875_BLUE);
// #endif
// 				}
// 				else if (lineChecksum == charW) {//full line
// #if !defined(RA8875_VISPIXDEBUG)
// 					fillRect(
// #else
// 					drawRect(
// #endif
// 						x, y + (currentYposition * _scaleY), charW * _scaleX, _scaleY, fcolor);
// 				}
// 				else { //line render
// 					_charLineRender(lineBuffer, charW, x, y, currentYposition, fcolor);
// 				}
// 				currentYposition++;//next line
// 				lineChecksum = 0;//reset checksum
// 			}//end exception
// 			//-------------------------------------------------------
// 			lineBuffer[currentXposition] = bitRead(temp, i);//continue fill line buffer
// 			lineChecksum += lineBuffer[currentXposition];
// 			currentXposition++;
// 		}
// 		currentByte++;
// 	}
// }

/**************************************************************************/
/*!	PRIVATE
		Font Line render optimized routine
		This will render ONLY a single font line by grouping chunks of same pixels
		Version 3.0 (fixed a bug that cause xlinePos to jump of 1 pixel
*/
/**************************************************************************/
FLASHMEM void RA8875::_charLineRender(bool lineBuffer[], int charW, int16_t x, int16_t y, int16_t currentYposition, uint16_t fcolor)
{
	int xlinePos = 0;
	int px;
	uint8_t endPix = 0;
	bool refPixel = false;
	while (xlinePos < charW) {
		refPixel = lineBuffer[xlinePos];//xlinePos pix as reference value for next pixels
		//detect and render concurrent pixels
		for (px = xlinePos;px <= charW;px++) {
			if (lineBuffer[px] == lineBuffer[xlinePos] && px < charW) {//grouping pixels with same val
				endPix++;
			}
			else {
				if (refPixel) {
#if defined(RA8875_VISPIXDEBUG)
					drawRect(
#else
					fillRect(
#endif
						x, y + (currentYposition * _scaleY), endPix * _scaleX, _scaleY, fcolor);
				}
				else {
#if defined(RA8875_VISPIXDEBUG)
					drawRect(x, y + (currentYposition * _scaleY), endPix * _scaleX, _scaleY, RA8875_BLUE);
#endif
				}
				//reset and update some vals
				xlinePos += endPix;
				x += endPix * _scaleX;
				endPix = 0;
				break;//exit cycle for...
			}
		}
	}
}

#else
/**************************************************************************/
/*!
	  This is the old rendering engine, pretty basic but slow, here for an alternative.
	  Note that this can be enabled only by commenting

	  #define _RA8875_TXTRNDOPTIMIZER

	  in RA8875UserSettings.h file!
	  Parameters:
	  x:
	  Y:
	  w: the width of the font
	  data: the data glyph
	  fcolor: foreground color
	  bcolor: background color
*/
/**************************************************************************/
FLASHMEM void RA8875::_drawChar_unc(int16_t x, int16_t y, int16_t w, const uint8_t* data, uint16_t fcolor, uint16_t bcolor)
{
	// if ((x >= _width)             			||  // Clip right
		// (y >= _height)           			||  // Clip bottom
		// ((x + w * _FNTscaleX - 1) < 0) 	||  // Clip left
		// ((y + _FNTheight * _FNTscaleY - 1) < 0))   	// Clip top
	// return;
	uint16_t color;
	uint16_t bitCount = 0;
	uint8_t line = 0;
	int j;
	uint16_t i;//,idx;
	for (i = 0; i < _FNTheight; i++) {	//Y
		for (j = 0; j < w; j++) {			//X		
			if (bitCount++ % 8 == 0) {
#if defined(_FORCE_PROGMEM__)
#if defined(ESP8266)
				line = FPSTR(&*data++);
#else
				line = PROGMEM_read(&*data++);
#endif
#else
				line = *data++;
#endif
			}
#if defined(RA8875_VISPIXDEBUG)
			color = RA8875_BLUE;
#else
			color = bcolor;
#endif
			if (line & 0x80) color = fcolor;
			if (_scaling) {// big
#if defined(RA8875_VISPIXDEBUG)
				drawRect(
#else
				fillRect(
#endif
					x + (j * _scaleX),
					y + (i * _scaleY),
					_scaleX,
					_scaleY,
					color
				);
			}
			else {  // def size
				drawPixel(x + j, y + i, color);
			}
			line <<= 1;
		}
	}
}
#endif

/*
FLASHMEM void RA8875::_drawChar_com(int16_t x,int16_t y,int16_t w,const uint8_t *data)
{
}
*/

/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+								COLOR STUFF											 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/

/**************************************************************************/
/*!
	  Set the display 'Color Space'
	  Parameters:
	  Bit per Pixel color (colors): 8 or 16 bit
	  NOTE:
	  For display over 272*480 give the ability to use
	  Layers since at 16 bit it's not possible.
*/
/**************************************************************************/
FLASHMEM void RA8875::setColorBpp(uint8_t colors)
{
	if (colors != _color_bpp) {//only if necessary
		if (colors < 16) {
			_color_bpp = 8;
			_colorIndex = 3;
			_writeRegister(RA8875_SYSR, 0x00);
			if (_hasLayerLimits) _maxLayers = 2;
		}
		else if (colors > 8) {//65K
			_color_bpp = 16;
			_colorIndex = 0;
			_writeRegister(RA8875_SYSR, 0x0C);
			if (_hasLayerLimits) _maxLayers = 1;
			_currentLayer = 0;
		}
	}
}

/**************************************************************************/
/*!
	  Return current Color Space (8 or 16)
*/
/**************************************************************************/
uint8_t RA8875::getColorBpp(void)
{
	return _color_bpp;
}

/**************************************************************************/
/*!
	  Sets set the foreground color using 16bit RGB565 color
	  It handles automatically color conversion when in 8 bit!
	  Parameters:
	  color: 16bit color RGB565
*/
/**************************************************************************/
FLASHMEM void RA8875::setForegroundColor(uint16_t color)
{
	if (_color_bpp == 16) {
		// 16‑bit mode: write separate 5/6/5 values
		_writeRegister(RA8875_FGCR0, (color >> 11) & 0x1F);
		_writeRegister(RA8875_FGCR0 + 1, (color >> 5) & 0x3F);
		_writeRegister(RA8875_FGCR0 + 2, color & 0x1F);
	}
	else {
		// 8‑bit mode: write the packed 8‑bit colour to all three registers
		uint8_t c8 = _color16To8bpp(color);
		_writeRegister(RA8875_FGCR0, c8);
		_writeRegister(RA8875_FGCR0 + 1, c8);
		_writeRegister(RA8875_FGCR0 + 2, c8);
	}
	_foreColor = color;   // keep for later use (e.g., text)

}
/**************************************************************************/
/*!
	  Sets set the foreground color using 8bit R,G,B
	  Parameters:
	  R: 8bit RED
	  G: 8bit GREEN
	  B: 8bit BLUE
*/
/**************************************************************************/
FLASHMEM void RA8875::setForegroundColor(uint8_t R, uint8_t G, uint8_t B)
{
	_foreColor = Color565(R, G, B);//keep track
	_writeRegister(RA8875_FGCR0, R);
	_writeRegister(RA8875_FGCR0 + 1, G);
	_writeRegister(RA8875_FGCR0 + 2, B);
}
/**************************************************************************/
/*!
	  Sets set the background color using 16bit RGB565 color
	  It handles automatically color conversion when in 8 bit!
	  Parameters:
	  color: 16bit color RGB565
	  Note: will set background Trasparency OFF
*/
/**************************************************************************/
FLASHMEM void RA8875::setBackgroundColor(uint16_t color)
{
	// _backColor = color;//keep track
	// _writeRegister(RA8875_BGCR0, ((color & 0xF800) >> _RA8875colorMask[_colorIndex]));//11
	// _writeRegister(RA8875_BGCR0 + 1, ((color & 0x07E0) >> _RA8875colorMask[_colorIndex + 1]));//5
	// _writeRegister(RA8875_BGCR0 + 2, ((color & 0x001F) >> _RA8875colorMask[_colorIndex + 2]));//0

	if (_color_bpp == 16) {
		// 16‑bit mode: write separate 5/6/5 components
		_writeRegister(RA8875_BGCR0, (color >> 11) & 0x1F);
		_writeRegister(RA8875_BGCR0 + 1, (color >> 5) & 0x3F);
		_writeRegister(RA8875_BGCR0 + 2, color & 0x1F);
	}
	else {
		// 8‑bit mode: write the same packed 8‑bit colour to all three registers
		uint8_t c8 = _color16To8bpp(color);
		_writeRegister(RA8875_BGCR0, c8);
		_writeRegister(RA8875_BGCR0 + 1, c8);
		_writeRegister(RA8875_BGCR0 + 2, c8);
	}
	_backColor = color;   // store for later use (e.g., restoring after BTE)

}
/**************************************************************************/
/*!
	  Sets set the background color using 8bit R,G,B
	  Parameters:
	  R: 8bit RED
	  G: 8bit GREEN
	  B: 8bit BLUE
	  Note: will set background Trasparency OFF
*/
/**************************************************************************/
FLASHMEM void RA8875::setBackgroundColor(uint8_t R, uint8_t G, uint8_t B)
{
	_backColor = Color565(R, G, B);//keep track
	_writeRegister(RA8875_BGCR0, R);
	_writeRegister(RA8875_BGCR0 + 1, G);
	_writeRegister(RA8875_BGCR0 + 2, B);
}
/**************************************************************************/
/*!
	  Sets set the trasparent background color using 16bit RGB565 color
	  It handles automatically color conversion when in 8 bit!
	  Parameters:
	  color: 16bit color RGB565
	  Note: will set background Trasparency ON
*/
/**************************************************************************/
FLASHMEM void RA8875::setTransparentColor(uint16_t color)
{
	_backColor = color;
	_writeRegister(RA8875_BGTR0, ((color & 0xF800) >> _RA8875colorMask[_colorIndex]));
	_writeRegister(RA8875_BGTR0 + 1, ((color & 0x07E0) >> _RA8875colorMask[_colorIndex + 1]));
	_writeRegister(RA8875_BGTR0 + 2, ((color & 0x001F) >> _RA8875colorMask[_colorIndex + 2]));
}
/**************************************************************************/
/*!
	  Sets set the Trasparent background color using 8bit R,G,B
	  Parameters:
	  R: 8bit RED
	  G: 8bit GREEN
	  B: 8bit BLUE
	  Note: will set background Trasparency ON
*/
/**************************************************************************/
FLASHMEM void RA8875::setTransparentColor(uint8_t R, uint8_t G, uint8_t B)
{
	_backColor = Color565(R, G, B);//keep track
	_writeRegister(RA8875_BGTR0, R);
	_writeRegister(RA8875_BGTR0 + 1, G);
	_writeRegister(RA8875_BGTR0 + 2, B);
}

/**************************************************************************/
/*!
		set foreground,background color (plus transparent background)
		Parameters:
		fColor: 16bit foreground color (text) RGB565
		bColor: 16bit background color RGB565
		backTransp:if true the bColor will be transparent
*/
/**************************************************************************/
FLASHMEM void RA8875::setColor(uint16_t fcolor, uint16_t bcolor, bool bcolorTraspFlag)//0.69b30
{
	if (fcolor != _foreColor) setForegroundColor(fcolor);
	if (bcolorTraspFlag) {
		setTransparentColor(bcolor);
	}
	else {
		if (bcolor != _backColor) setBackgroundColor(bcolor);
	}
}


/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+								DRAW STUFF											 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/

/**************************************************************************/
/*!		Set graphic cursor beween 8 different ones.
		Graphic cursors has to be inserted before use!
		Parameters:
		cur: 0...7
*/
/**************************************************************************/
FLASHMEM void RA8875::setGraphicCursor(uint8_t cur)
{
	if (cur > 7) cur = 7;
	uint8_t temp = _readRegister(RA8875_MWCR1);
	temp &= ~(0x70);//clear bit 6,5,4
	temp |= cur << 4;
	temp |= cur;
	if (_useMultiLayers) {
		_currentLayer == 1 ? temp |= (1 << 0) : temp &= ~(1 << 0);
	}
	else {
		temp &= ~(1 << 0);
	}
	_writeData(temp);
}

/**************************************************************************/
/*!		Show the graphic cursor
		Graphic cursors has to be inserted before use!
		Parameters:
		cur: true,false
*/
/**************************************************************************/
FLASHMEM void RA8875::showGraphicCursor(boolean cur)
{
	// uint8_t temp = _readRegister(RA8875_MWCR1);
	// cur == true ? temp |= (1 << 7) : temp &= ~(1 << 7);
	// if (_useMultiLayers) {
	// 	_currentLayer == 1 ? temp |= (1 << 0) : temp &= ~(1 << 0);
	// }
	// else {
	// 	temp &= ~(1 << 0);
	// }
	// _writeData(temp);
}

/**************************************************************************/
/*!
		Set the position for Graphic Write
		Parameters:
		x: horizontal position
		y: vertical position
*/
/**************************************************************************/

FLASHMEM void RA8875::setXY(int16_t x, int16_t y)
{
	setX(x);
	setY(y);
}

/**************************************************************************/
/*!
		Set the x position for Graphic Write
		Parameters:
		x: horizontal position
*/
/**************************************************************************/
FLASHMEM void RA8875::setX(int16_t x)
{
	if (x < 0) x = 0;
	if (_portrait) {//fix 0.69b21
		if (x >= RA8875_HEIGHT) x = RA8875_HEIGHT - 1;
		_writeRegister(RA8875_CURV0, x & 0xFF);
		_writeRegister(RA8875_CURV0 + 1, x >> 8);
	}
	else {
		if (x >= RA8875_WIDTH) x = RA8875_WIDTH - 1;
		_writeRegister(RA8875_CURH0, x & 0xFF);
		_writeRegister(RA8875_CURH0 + 1, (x >> 8));
	}
}

/**************************************************************************/
/*!
		Set the y position for Graphic Write
		Parameters:
		y: vertical position
*/
/**************************************************************************/
FLASHMEM void RA8875::setY(int16_t y)
{
	if (y < 0) y = 0;
	if (_portrait) {//fix 0.69b21
		if (y >= RA8875_WIDTH) y = RA8875_WIDTH - 1;
		_writeRegister(RA8875_CURH0, y & 0xFF);
		_writeRegister(RA8875_CURH0 + 1, (y >> 8));
	}
	else {
		if (y >= RA8875_HEIGHT) y = RA8875_HEIGHT - 1;
		_writeRegister(RA8875_CURV0, y & 0xFF);
		_writeRegister(RA8875_CURV0 + 1, y >> 8);
	}
}


/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+								DMA STUFF											 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/
/**************************************************************************/
/*!

*/
/**************************************************************************/
FLASHMEM void RA8875::DMA_blockModeSize(int16_t BWR, int16_t BHR, int16_t SPWR)
{
	_writeRegister(RA8875_DTNR0, BWR & 0xFF);
	_writeRegister(RA8875_BWR1, BWR >> 8);

	_writeRegister(RA8875_DTNR1, BHR & 0xFF);
	_writeRegister(RA8875_BHR1, BHR >> 8);

	_writeRegister(RA8875_DTNR2, SPWR & 0xFF);
	_writeRegister(RA8875_SPWR1, SPWR >> 8);
}

/**************************************************************************/
/*!

*/
/**************************************************************************/
FLASHMEM void RA8875::DMA_startAddress(unsigned long adrs)
{
	_writeRegister(RA8875_SSAR0, adrs & 0xFF);
	_writeRegister(RA8875_SSAR0 + 1, adrs >> 8);
	_writeRegister(RA8875_SSAR0 + 2, adrs >> 16);
	//_writeRegister(0xB3,adrs >> 24);// not more in datasheet!
}

/**************************************************************************/
/*!

*/
/**************************************************************************/
FLASHMEM void RA8875::DMA_enable(void)
{
	uint8_t temp = _readRegister(RA8875_DMACR);
	temp |= 0x01;
	_writeData(temp);
	_waitBusy(0x01);
}
/**************************************************************************/
/*! (STILL IN DEVELOP, please do not complain)
		Display an image stored in Flash RAM
		Note: you should have the optional FLASH Chip connected to RA8875!
		Note: You should store some image in that chip!
		Note: Never tried!!!!!!!

*/
/**************************************************************************/
FLASHMEM void RA8875::drawFlashImage(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t picnum)
{
	if (_portrait) { swapvals(x, y); swapvals(w, h); }//0.69b21 -have to check this, not verified
	if (_textMode) _setTextMode(false);//we are in text mode?
	_writeRegister(RA8875_SFCLR, 0x00);
	_writeRegister(RA8875_SROC, 0x87);
	_writeRegister(RA8875_DMACR, 0x02);
	//setActiveWindow(0,_width-1,0,_height-1); 
	_checkLimits_helper(x, y);
	_checkLimits_helper(w, h);
	_portrait == true ? setXY(y, x) : setXY(x, y);

	DMA_startAddress(261120 * (picnum - 1));
	DMA_blockModeSize(w, h, w);
	_writeRegister(RA8875_DMACR, 0x03);
	_waitBusy(0x01);
}


/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+								BTE STUFF											 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/

/**************************************************************************/
/*
		Block Transfer Move
		Can move a rectangular block from any area of memory (eg. layer 1) to any other (eg layer 2)
		Can move with transparency - note THE TRANSPARENT COLOUR IS THE TEXT FOREGROUND COLOUR
		ReverseDir is for moving overlapping areas - may need to use reverse to prevent it double-copying the overlapping area (this option not available with transparency or monochrome)
		ROP is Raster Operation. Usually use RA8875_ROP_SOURCE but a few others are defined
		Defaults to current layer if not given or layer is zero.
		Monochrome uses the colour-expansion mode: the input is a bit map which is then converted to the current foreground and background colours, transparent background is optional
		Monochrome data is assumed to be linear, originally written to the screen memory in 16-bit chunks with drawPixels().
		Monochrome mode uses the ROP to define the offset of the first image bit within the first byte. This also depends on the width of the block you are trying to display.
		Monochrome skips 16-bit words in the input pattern - see the example for more explanation and a trick to interleave 2 characters in the space of one.

		This function returns immediately but the actual transfer can take some time
		Caller should check the busy status before issuing any more RS8875 commands.

		Basic usage:
		BTE_Move(SourceX, SourceY, Width, Height, DestX, DestY) = copy something visible on the current layer
		BTE_Move(SourceX, SourceY, Width, Height, DestX, DestY, 2) = copy something from layer 2 to the current layer
		BTE_Move(SourceX, SourceY, Width, Height, DestX, DestY, 2, 1, true) = copy from layer 2 to layer 1, with the transparency option
		BTE_Move(SourceX, SourceY, Width, Height, DestX, DestY, 0, 0, true, RA8875_BTEROP_ADD) = copy on the current layer, using transparency and the ADD/brighter operation
		BTE_Move(SourceX, SourceY, Width, Height, DestX, DestY, 0, 0, false, RA8875_BTEROP_SOURCE, false, true) = copy on the current layer using the reverse direction option for overlapping areas
*/

void  RA8875::BTE_move(int16_t SourceX, int16_t SourceY, int16_t Width, int16_t Height, int16_t DestX, int16_t DestY, uint8_t SourceLayer, uint8_t DestLayer, bool Transparent, uint8_t ROP, bool Monochrome, bool ReverseDir)
{

	if (SourceLayer == 0) SourceLayer = _currentLayer;
	if (DestLayer == 0) DestLayer = _currentLayer;
	if (SourceLayer == 2) SourceY |= 0x8000; //set the high bit of the vertical coordinate to indicate layer 2
	if (DestLayer == 2) DestY |= 0x8000; //set the high bit of the vertical coordinate to indicate layer 2
	ROP &= 0xF0; //Ensure the lower bits of ROP are zero
	if (Transparent) {
		if (Monochrome) {
			ROP |= 0x0A; //colour-expand transparent
		}
		else {
			ROP |= 0x05; //set the transparency option 
		}
	}
	else {
		if (Monochrome) {
			ROP |= 0x0B; //colour-expand normal
		}
		else {
			if (ReverseDir) {
				ROP |= 0x03; //set the reverse option
			}
			else {
				ROP |= 0x02; //standard block-move operation
			}
		}
	}

	_waitBusy(0x40); //Check that another BTE operation is not still in progress
	if (_textMode) _setTextMode(false);//we are in text mode?
	BTE_moveFrom(SourceX, SourceY);
	BTE_size(Width, Height);
	BTE_moveTo(DestX, DestY);
	BTE_ropcode(ROP);
	//Execute BTE! (This selects linear addressing mode for the monochrome source data)
	if (Monochrome) _writeRegister(RA8875_BECR0, 0xC0); else _writeRegister(RA8875_BECR0, 0x80);
	_waitBusy(0x40);
	//we are supposed to wait for the thing to become unbusy
	//caller can call _waitBusy(0x40) to check the BTE busy status (except it's private)
}

/**************************************************************************/
/*! TESTING

*/
/**************************************************************************/
FLASHMEM void RA8875::BTE_size(int16_t w, int16_t h)
{
	//0.69b21 -have to check this, not verified
	if (_portrait) swapvals(w, h);
	_writeRegister(RA8875_BEWR0, w & 0xFF);//BET area width literacy  
	_writeRegister(RA8875_BEWR0 + 1, w >> 8);//BET area width literacy	   
	_writeRegister(RA8875_BEHR0, h & 0xFF);//BET area height literacy
	_writeRegister(RA8875_BEHR0 + 1, h >> 8);//BET area height literacy	   
}

/**************************************************************************/
/*!

*/
/**************************************************************************/

FLASHMEM void RA8875::BTE_moveFrom(int16_t SX, int16_t SY)
{
	if (_portrait) swapvals(SX, SY);
	_writeRegister(RA8875_HSBE0, SX & 0xFF);
	_writeRegister(RA8875_HSBE0 + 1, SX >> 8);
	_writeRegister(RA8875_VSBE0, SY & 0xFF);
	_writeRegister(RA8875_VSBE0 + 1, SY >> 8);
}

/**************************************************************************/
/*!

*/
/**************************************************************************/

FLASHMEM void RA8875::BTE_moveTo(int16_t DX, int16_t DY)
{
	if (_portrait) swapvals(DX, DY);
	_writeRegister(RA8875_HDBE0, DX & 0xFF);
	_writeRegister(RA8875_HDBE0 + 1, DX >> 8);
	_writeRegister(RA8875_VDBE0, DY & 0xFF);
	_writeRegister(RA8875_VDBE0 + 1, DY >> 8);
}

/**************************************************************************/
/*! TESTING
	Use a ROP code EFX
*/
/**************************************************************************/
FLASHMEM void RA8875::BTE_ropcode(unsigned char setx)
{
	_writeRegister(RA8875_BECR1, setx);//BECR1	   
}

/**************************************************************************/
/*! TESTING
	Enable BTE transfer
*/
/**************************************************************************/
FLASHMEM void RA8875::BTE_enable(bool on)
{
	uint8_t temp = _readRegister(RA8875_BECR0);
	on == true ? temp &= ~(1 << 7) : temp |= (1 << 7);
	_writeData(temp);
	//_writeRegister(RA8875_BECR0,temp);  
	_waitBusy(0x40);
}


/**************************************************************************/
/*! TESTING
	Select BTE mode (CONT (continuous) or RECT)
*/
/**************************************************************************/
FLASHMEM void RA8875::BTE_dataMode(enum RA8875btedatam m)
{
	uint8_t temp = _readRegister(RA8875_BECR0);
	m == CONT ? temp &= ~(1 << 6) : temp |= (1 << 6);
	_writeData(temp);
	//_writeRegister(RA8875_BECR0,temp);  
}

/**************************************************************************/
/*! TESTING
	Select the BTE SOURCE or DEST layer (1 or 2)
*/
/**************************************************************************/

FLASHMEM void RA8875::BTE_layer(enum RA8875btelayer sd, uint8_t l)
{
	uint8_t temp;
	sd == SOURCE ? temp = _readRegister(RA8875_VSBE0 + 1) : temp = _readRegister(RA8875_VDBE0 + 1);
	l == 1 ? temp &= ~(1 << 7) : temp |= (1 << 7);
	_writeData(temp);
	//_writeRegister(RA8875_VSBE1,temp);  
}



/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+								LAYER STUFF											 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/

/**************************************************************************/
/*!
		Instruct the RA8875 chip to use 2 layers
		If resolution bring to restrictions it will switch to 8 bit
		so you can always use layers.
		Parameters:
		on: true (enable multiple layers), false (disable)

*/
/**************************************************************************/
FLASHMEM void RA8875::useLayers(boolean on)
{
	if (_useMultiLayers == on) return; //no reason to do change that it's already as desidered.
	//bool clearBuffer = false;
	if (_hasLayerLimits && _color_bpp > 8) { //try to set up 8bit color space
		setColorBpp(8);
		_waitBusy();
		_maxLayers = 2;
	}
	if (on) {
		_useMultiLayers = true;
		_DPCR_Reg |= (1 << 7);
		//clearBuffer = true;
		clearActiveWindow(true);
	}
	else {
		_useMultiLayers = false;
		_DPCR_Reg &= ~(1 << 7);
		clearActiveWindow(false);

	}

	_writeRegister(RA8875_DPCR, _DPCR_Reg);
	if (!_useMultiLayers && _color_bpp < 16) setColorBpp(16);//bring color back to 16
	/*
	if (clearBuffer) {
		clearWindow(true);
		//for some reason if you switch to multilayer the layer 2 has garbage better clear
		//writeTo(L2);//switch to layer 2
		//clearMemory(false);//clear memory of layer 2
		//clearWindow(false);
		//writeTo(L1);//switch to layer 1
	}
	*/
}


/**************************************************************************/
/*!


*/
/**************************************************************************/
FLASHMEM void RA8875::layerEffect(enum RA8875boolean efx)
{
	uint8_t	reg = 0b00000000;
	//reg &= ~(0x07);//clear bit 2,1,0
	if (!_useMultiLayers) useLayers(true);//turn on multiple layers if it's off
	switch (efx) {//                       bit 2,1,0 of LTPR0
	case LAYER1: //only layer 1 visible  [000]
		//do nothing
		break;
	case LAYER2: //only layer 2 visible  [001]
		reg |= (1 << 0);
		break;
	case TRANSPARENT: //transparent mode [011]
		reg |= (1 << 0); reg |= (1 << 1);
		break;
	case LIGHTEN: //lighten-overlay mode [010]
		reg |= (1 << 1);
		break;
	case OR: //boolean OR mode           [100]
		reg |= (1 << 2);
		break;
	case AND: //boolean AND mode         [101]
		reg |= (1 << 0); reg |= (1 << 2);
		break;
	case FLOATING: //floating windows    [110]
		reg |= (1 << 1); reg |= (1 << 2);
		break;
	default:
		//do nothing
		break;
	}
	_writeRegister(RA8875_LTPR0, reg);
}

/**************************************************************************/
/*!


*/
/**************************************************************************/
FLASHMEM void RA8875::layerTransparency(uint8_t layer1, uint8_t layer2)
{
	if (layer1 > 8) layer1 = 8;
	if (layer2 > 8) layer2 = 8;
	if (!_useMultiLayers) useLayers(true);//turn on multiple layers if it's off
	//if (_useMultiLayers) _writeRegister(RA8875_LTPR1, ((layer2 & 0x0F) << 4) | (layer1 & 0x0F));
	//uint8_t res = 0b00000000;//RA8875_LTPR1
	//reg &= ~(0x07);//clear bit 2,1,0
	_writeRegister(RA8875_LTPR1, ((layer2 & 0xF) << 4) | (layer1 & 0xF));
}


/**************************************************************************/
/*! return the current drawing layer. If layers are OFF, return 255

*/
/**************************************************************************/
uint8_t RA8875::getCurrentLayer(void)
{
	if (!_useMultiLayers) return 255;
	return _currentLayer;
}

/**************************************************************************/
/*! select pattern

*/
/**************************************************************************/
FLASHMEM void RA8875::setPattern(uint8_t num, enum RA8875pattern p)
{
	uint8_t maxLoc;
	uint8_t temp = 0b00000000;
	if (p != P16X16) {
		maxLoc = 16;//at 8x8 max 16 locations
	}
	else {
		maxLoc = 4;//at 16x16 max 4 locations
		temp |= (1 << 7);
	}
	if (num > (maxLoc - 1)) num = maxLoc - 1;
	temp = temp | num;
	writeTo(PATTERN);
	_writeRegister(RA8875_PTNO, temp);
}

/**************************************************************************/
/*! write pattern

*/
/**************************************************************************/
FLASHMEM void RA8875::writePattern(int16_t x, int16_t y, const uint8_t* data, uint8_t size, bool setAW)
{
	int16_t i;
	int16_t a, b, c, d;
	if (size < 8 || size > 16) return;
	if (setAW) getActiveWindow(a, b, c, d);
	setActiveWindow(x, x + size - 1, y, y + size - 1);
	setXY(x, y);

	if (_textMode) _setTextMode(false);//we are in text mode?
	writeCommand(RA8875_MRWC);
	for (i = 0;i < (size * size);i++) {
		_writeData(data[i * 2]);
		_writeData(data[i * 2 + 1]);
		_waitBusy(0x80);
	}
	if (setAW) setActiveWindow(a, b, c, d);//set as it was before
}

/**************************************************************************/
/*! This is the most important function to write on:
	LAYERS
	CGRAM
	PATTERN
	CURSOR
	Parameter:
	d (L1, L2, CGRAM, PATTERN, CURSOR)
	When writing on layers 0 or 1, if the layers are not enable it will enable automatically
	If the display doesn't support layers, it will automatically switch to 8bit color
	Remember that when layers are ON you need to disable manually, once that only Layer 1 will be visible

*/
/**************************************************************************/
FLASHMEM void RA8875::writeTo(enum RA8875writes d)
{
	uint8_t temp = _readRegister(RA8875_MWCR1);
	//bool trigMultilayer = false;
	switch (d) {
	case L1:
		temp &= ~((1 << 3) | (1 << 2));// Clear bits 3 and 2
		temp &= ~(1 << 0); //clear bit 0
		_currentLayer = 0;
		//trigMultilayer = true;
		_writeData(temp);
		if (!_useMultiLayers) useLayers(true);
		break;
	case L2:
		temp &= ~((1 << 3) | (1 << 2));// Clear bits 3 and 2
		temp |= (1 << 0); //bit set 0
		_currentLayer = 1;
		//trigMultilayer = true;
		_writeData(temp);
		if (!_useMultiLayers) useLayers(true);
		break;
	case CGRAM:
		temp &= ~(1 << 3); //clear bit 3
		temp |= (1 << 2); //bit set 2
		if (bitRead(_FNCR0_Reg, 7)) {//REG[0x21] bit7 must be 0
			_FNCR0_Reg &= ~(1 << 7); //clear bit 7
			_writeRegister(RA8875_FNCR0, _FNCR0_Reg);
			_writeRegister(RA8875_MWCR1, temp);
		}
		else {
			_writeData(temp);
		}
		break;
	case PATTERN:
		temp |= (1 << 3); //bit set 3
		temp |= (1 << 2); //bit set 2
		_writeData(temp);
		break;
	case CURSOR:
		temp |= (1 << 3); //bit set 3
		temp &= ~(1 << 2); //clear bit 2
		_writeData(temp);
		break;
	default:
		//break;
		return;
	}
	//if (trigMultilayer && !_useMultiLayers) useLayers(true);//turn on multiple layers if it's off
	//_writeRegister(RA8875_MWCR1,temp);   
}


/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+						GEOMETRIC PRIMITIVE  STUFF									 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/
/**************************************************************************/
/*!

*/
/**************************************************************************/
/* FLASHMEM void RA8875::fillRect(void) {
	writeCommand(RA8875_DCR);
	_writeData(RA8875_DCR_LINESQUTRI_STOP | RA8875_DCR_DRAWSQUARE);
	_writeData(RA8875_DCR_LINESQUTRI_START | RA8875_DCR_FILL | RA8875_DCR_DRAWSQUARE);
}
 */
 /**************************************************************************/
 /*!
	   Write a single pixel
	   Parameters:
	   x: horizontal pos
	   y: vertical pos
	   color: RGB565 color
	   NOTE:
	   In 8bit bpp RA8875 needs a 8bit color(332) and NOT a 16bit(565),
	   the routine deal with this...
 */
 /**************************************************************************/
void RA8875::drawPixel(int16_t x, int16_t y, uint16_t color)
{

	// Remote MIDI sync block
	if (remote_active)
	{
		static uint8_t sysexDrawPixel[9] = {
			0x0, 0x0, 0x0, 0x0,
			0x0, 0x0, 0x0, 0x0, 0x0 // color placeholder
		};
		fillSysexData(sysexDrawPixel, 2, x, y);
		uint8_t colors[4];
		uint8_t nbBytes = fillSysexDataColor(colors, 1, color);
		sysexDrawPixel[4] = nbBytes;
		memcpy(sysexDrawPixel + 5, colors, nbBytes);
		sendSysEx(90, 5 + nbBytes, sysexDrawPixel, true);
		console = false;
	}

	x = x * 2;
	y = y * 2;

	//setXY(x,y);
	if (_textMode) _setTextMode(false);//we are in text mode?
	setXY(x, y);
	writeCommand(RA8875_MRWC);
	if (_color_bpp > 8) {
		writeData16(color);
	}
	else {//TOTEST:layer bug workaround for 8bit color!
		_writeData(_color16To8bpp(color));
	}

}

/**************************************************************************/
/*!
	  Draw a series of pixels
	  Parameters:
	  p: an array of 16bit colors (pixels)
	  count: how many pixels
	  x: horizontal pos
	  y: vertical pos
	  NOTE:
	  In 8bit bpp RA8875 needs a 8bit color(332) and NOT a 16bit(565),
	  the routine deal with this...
*/
/**************************************************************************/
void RA8875::drawPixels(uint16_t p[], uint16_t count, int16_t x, int16_t y)
{
	x = x * 2;
	y = y * 2;

	//setXY(x,y);
	uint16_t temp = 0;
	uint16_t i;
	if (_textMode) _setTextMode(false);//we are in text mode?
	setXY(x, y);
	writeCommand(RA8875_MRWC);
	_startSend();
	//set data

	_pspi->transfer(RA8875_DATAWRITE);

	//the loop
	for (i = 0;i < count;i++) {
		if (_color_bpp < 16) {
			temp = _color16To8bpp(p[i]);//TOTEST:layer bug workaround for 8bit color!
		}
		else {
			temp = p[i];
		}

		if (_color_bpp > 8) {
			_pspi->transfer16(temp);
		}
		else {//TOTEST:layer bug workaround for 8bit color!
			_pspi->transfer(temp & 0xFF);
		}

	}
	_endSend();
}


/**************************************************************************/
/*!
	  Get a pixel color from screen
	  Parameters:
	  x: horizontal pos
	  y: vertical pos
*/
/**************************************************************************/
uint16_t RA8875::getPixel(int16_t x, int16_t y)
{
	uint16_t color;
	setXY(x, y);
	if (_textMode) _setTextMode(false);//we are in text mode?
	writeCommand(RA8875_MRWC);
#if defined(_FASTCPU)
	_slowDownSPI(true);
#endif
	_startSend();
	_pspi->transfer(RA8875_DATAREAD);
	_pspi->transfer(0x00);//first byte it's dummy
	color = _pspi->transfer16(0x0);

#if defined(_FASTCPU)
	_slowDownSPI(false);
#endif
	_endSend();
	return color;
}


/**************************************************************************/
/*!
	  Basic line draw
	  Parameters:
	  x0: horizontal start pos
	  y0: vertical start
	  x1: horizontal end pos
	  y1: vertical end pos
	  color: RGB565 color
	  NOTE:
	  Remember that this write from->to so: drawLine(0,0,2,0,RA8875_RED);
	  result a 3 pixel long! (0..1..2)
*/
/**************************************************************************/
void RA8875::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
	if (remote_active) {
		static uint8_t sysexDrawLine[13] = { 0 };
		fillSysexData(sysexDrawLine, 4, x0, y0, x1, y1);
		uint8_t colors[4];
		uint8_t nb = fillSysexDataColor(colors, 1, color);
		sysexDrawLine[8] = nb;
		memcpy(sysexDrawLine + 9, colors, nb);
		sendSysEx(96, 9 + nb, sysexDrawLine, true);
		console = false;
	}

	// Scale logical to physical
	x0 = x0 * 2;
	y0 = y0 * 2;
	x1 = x1 * 2;
	y1 = y1 * 2;

	if ((x0 == x1 && y0 == y1) || ((x1 - x0 == 1) && (y1 - y0 == 1))) {
		drawPixel(x0, y0, color);
		return;
	}

	if (_portrait) { swapvals(x0, y0); swapvals(x1, y1); }
	if (_textMode) _setTextMode(false);
#if defined(USE_RA8875_SEPARATE_TEXT_COLOR)
	_TXTrecoverColor = true;
#endif
	if (color != _foreColor) setForegroundColor(color);

	_line_addressing(x0, y0, x1, y1);
	_writeRegister(RA8875_DCR, 0x80);
	_waitPoll(RA8875_DCR, RA8875_DCR_LINESQUTRI_STATUS, _RA8875_WAITPOLL_TIMEOUT_DCR_LINESQUTRI_STATUS);

}


void RA8875::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
	drawLine(x, y, x, y + h - 1, color);
}
void RA8875::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
	drawLine(x, y, x + w - 1, y, color);
}

void RA8875::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
	if (console && remote_active)
	{
		static uint8_t sysexDrawRect[13] = {
			0x0,
			0x0,
			0x0,
			0x0,
			0x0,
			0x0,
			0x0,
			0x0,
			0x0,
			0x0,
			0x0,
			0x0,
			0x0, // could be unknown color
		};

		fillSysexData(sysexDrawRect, 4, x, y, w, h);
		uint8_t colors[4];
		uint8_t nbBytes = fillSysexDataColor(colors, 1, color);
		sysexDrawRect[8] = nbBytes;
		memcpy(sysexDrawRect + 8 + 1, colors, nbBytes);

		sendSysEx(98, 8 + 1 + nbBytes, sysexDrawRect, true);
		console = false;
	}

	x = x * 2;
	y = y * 2;
	w = w * 2;
	h = h * 2;

	//RA8875 it's not out-of-range tolerant so this is a workaround
	if (w < 1 || h < 1) return;//it cannot be!
	if (w < 2 && h < 2) { //render as pixel
		drawPixel(x, y, color);
	}
	else {			 //render as rect
		_rect_helper(x, y, (w + x) - 1, (h + y) - 1, color, false);//thanks the experimentalist
	}
}

/**************************************************************************/
/*!
	  draws a FILLED rectangle
	  Parameters:
	  x: horizontal start
	  y: vertical start
	  w: width
	  h: height
	  color: RGB565 color
*/
/**************************************************************************/
void RA8875::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
	if (remote_active && skipfillrect_webremote == false) {
		static uint8_t sysexFillRect[13] = { 0 };
		fillSysexData(sysexFillRect, 4, x, y, w, h);
		uint8_t colors[4];
		uint8_t nb = fillSysexDataColor(colors, 1, color);
		sysexFillRect[8] = nb;
		memcpy(sysexFillRect + 9, colors, nb);
		sendSysEx(94, 9 + nb, sysexFillRect, true);
	}

	// Scale logical to physical
	x = x * 2;
	y = y * 2;
	w = w * 2;
	h = h * 2;

	if (w < 1 || h < 1) return;
	if (w < 2 && h < 2) {
		drawPixel(x, y, color);
	}
	else {
		_rect_helper(x, y, x + w - 1, y + h - 1, color, true);
	}
}



/**************************************************************************/
/*!
	  write a filled rectangle wither user colors array
	  Parameters:
	  x: horizontal start
	  y: vertical start
	  w: width
	  h: height
	  pcolors: Array of RGB565 color of size w*h
*/
/**************************************************************************/

/**************************************************************************/
/*!
	  Fill the ActiveWindow by using a specified RGB565 color
	  Parameters:
	  color: RGB565 color (default=BLACK)
*/
/**************************************************************************/
void RA8875::fillWindow(uint16_t color)
{

	if (console && remote_active)
	{
		static uint8_t sysexFillRect[13] = {
			0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
			0x0, 0x0, 0x0, 0x0, 0x0 // could be unknown color
		};

		fillSysexData(sysexFillRect, 4, 0, 0, 320, 240);
		uint8_t colors[4];
		uint8_t nbBytes = fillSysexDataColor(colors, 1, color);
		sysexFillRect[8] = nbBytes;
		memcpy(sysexFillRect + 8 + 1, colors, nbBytes);

		sendSysEx(94, 8 + 1 + nbBytes, sysexFillRect, true);
	}

	_line_addressing(0, 0, RA8875_WIDTH - 1, RA8875_HEIGHT - 1);
	setForegroundColor(color);
	writeCommand(RA8875_DCR);
	_writeData(0xB0);
	_waitPoll(RA8875_DCR, RA8875_DCR_LINESQUTRI_STATUS, _RA8875_WAITPOLL_TIMEOUT_DCR_LINESQUTRI_STATUS);
#if defined(USE_RA8875_SEPARATE_TEXT_COLOR)
	_TXTrecoverColor = true;
#endif


}



/**************************************************************************/
/*!
	  clearScreen it's different from fillWindow because it doesn't depends
	  from the active window settings so it will clear all the screen.
	  It should be used only when needed since it's slower than fillWindow.
	  parameter:
	  color: 16bit color (default=BLACK)
*/
/**************************************************************************/
void RA8875::clearScreen(uint16_t color)//0.69b24
{

	setActiveWindow();
	fillWindow(color);
}

/**************************************************************************/
/*!
	  Draw circle
	  Parameters:
	  x0: The 0-based x location of the center of the circle
	  y0: The 0-based y location of the center of the circle
	  r: radius
	  color: RGB565 color
*/
/**************************************************************************/
void RA8875::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
	_center_helper(x0, y0);
	if (r < 1) return;
	if (r < 2) {
		drawPixel(x0, y0, color);
		return;
	}
	_circle_helper(x0, y0, r, color, false);
}



void RA8875::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
	x0 = x0 * 2;
	y0 = y0 * 2;
	r = r * 2;


	_center_helper(x0, y0);
	if (r < 1) return;
	if (r == 1) {
		drawPixel(x0, y0, color);
		return;
	}
	_circle_helper(x0, y0, r, color, true);
}

float RA8875::_check_area(int16_t Ax, int16_t Ay, int16_t Bx, int16_t By, int16_t Cx, int16_t Cy) {
	float area = abs(Ax * (By - Cy) + Bx * (Cy - Ay) + Cx * (Ay - By));     // Calc area
	float mag1 = sqrt((Bx - Ax) * (Bx - Ax) + (By - Ay) * (By - Ay));       // Calc side lengths
	float mag2 = sqrt((Cx - Ax) * (Cx - Ax) + (Cy - Ay) * (Cy - Ay));
	float mag3 = sqrt((Cx - Bx) * (Cx - Bx) + (Cy - By) * (Cy - By));
	float magmax = (mag1 > mag2 ? mag1 : mag2) > mag3 ? (mag1 > mag2 ? mag1 : mag2) : mag3;   // Find largest length
	return area / magmax;                                                     // Return area
}

/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+					GEOMETRIC PRIMITIVE HELPERS STUFF								 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/
/**************************************************************************/
/*!
	  helper function for circles
	  [private]
*/
/**************************************************************************/
void RA8875::_circle_helper(int16_t x0, int16_t y0, int16_t r, uint16_t color, bool filled)//0.69b32 fixed an undocumented hardware limit
{
	if (_portrait) swapvals(x0, y0);//0.69b21

	if (r < 1) r = 1;
	if (r < 2) {//NEW
		drawPixel(x0, y0, color);
		return;
	}
	if (r > RA8875_HEIGHT / 2) r = (RA8875_HEIGHT / 2) - 1;//this is the (undocumented) hardware limit of RA8875

	if (_textMode) _setTextMode(false);//we are in text mode?
#if defined(USE_RA8875_SEPARATE_TEXT_COLOR)
	_TXTrecoverColor = true;
#endif
	if (color != _foreColor) setForegroundColor(color);//0.69b30 avoid several SPI calls

	_writeRegister(RA8875_DCHR0, x0 & 0xFF);
	_writeRegister(RA8875_DCHR0 + 1, x0 >> 8);

	_writeRegister(RA8875_DCVR0, y0 & 0xFF);
	_writeRegister(RA8875_DCVR0 + 1, y0 >> 8);
	_writeRegister(RA8875_DCRR, r);

	writeCommand(RA8875_DCR);
#if defined(_FASTCPU)
	_slowDownSPI(true);
#endif
	filled == true ? _writeData(RA8875_DCR_CIRCLE_START | RA8875_DCR_FILL) : _writeData(RA8875_DCR_CIRCLE_START | RA8875_DCR_NOFILL);
	_waitPoll(RA8875_DCR, RA8875_DCR_CIRCLE_STATUS, _RA8875_WAITPOLL_TIMEOUT_DCR_CIRCLE_STATUS);//ZzZzz
#if defined(_FASTCPU)
	_slowDownSPI(false);
#endif
}


void RA8875::_rect_helper(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color, bool filled) {
	if (_portrait) { swapvals(x1, y1); swapvals(x2, y2); }

	// x1,y1,x2,y2 are already physical (scaled by caller)
	_checkLimits_helper(x1, y1);
	_checkLimits_helper(x2, y2);

	if (_textMode) _setTextMode(false);
#if defined(USE_RA8875_SEPARATE_TEXT_COLOR)
	_TXTrecoverColor = true;
#endif
	if (color != _foreColor) setForegroundColor(color);

	if (x1 == x2 && y1 == y2) {
		drawPixel(x1, y1, color);
	}
	else {
		_line_addressing(x1, y1, x2, y2);
		writeCommand(RA8875_DCR);
		filled == true ? _writeData(0xB0) : _writeData(0x90);
		_waitPoll(RA8875_DCR, RA8875_DCR_LINESQUTRI_STATUS, _RA8875_WAITPOLL_TIMEOUT_DCR_LINESQUTRI_STATUS);
	}
}


/**************************************************************************/
/*!
		this update the RA8875 Active Window registers
		[private]
*/
/**************************************************************************/
void RA8875::_updateActiveWindow(bool full)
{
	if (full) {
		// X
		_writeRegister(RA8875_HSAW0, 0x00);
		_writeRegister(RA8875_HSAW0 + 1, 0x00);
		_writeRegister(RA8875_HEAW0, (RA8875_WIDTH) & 0xFF);
		_writeRegister(RA8875_HEAW0 + 1, (RA8875_WIDTH) >> 8);
		// Y 
		_writeRegister(RA8875_VSAW0, 0x00);
		_writeRegister(RA8875_VSAW0 + 1, 0x00);
		_writeRegister(RA8875_VEAW0, (RA8875_HEIGHT) & 0xFF);
		_writeRegister(RA8875_VEAW0 + 1, (RA8875_HEIGHT) >> 8);
	}
	else {
		// X
		_writeRegister(RA8875_HSAW0, _activeWindowXL & 0xFF);
		_writeRegister(RA8875_HSAW0 + 1, _activeWindowXL >> 8);
		_writeRegister(RA8875_HEAW0, _activeWindowXR & 0xFF);
		_writeRegister(RA8875_HEAW0 + 1, _activeWindowXR >> 8);
		// Y 
		_writeRegister(RA8875_VSAW0, _activeWindowYT & 0xFF);
		_writeRegister(RA8875_VSAW0 + 1, _activeWindowYT >> 8);
		_writeRegister(RA8875_VEAW0, _activeWindowYB & 0xFF);
		_writeRegister(RA8875_VEAW0 + 1, _activeWindowYB >> 8);
	}
}

/**************************************************************************/
/*!
		Graphic line addressing helper
		[private]
*/
/**************************************************************************/
void RA8875::_line_addressing(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
	//X0
	_writeRegister(RA8875_DLHSR0, x0 & 0xFF);
	_writeRegister(RA8875_DLHSR0 + 1, x0 >> 8);
	//Y0
	_writeRegister(RA8875_DLVSR0, y0 & 0xFF);
	_writeRegister(RA8875_DLVSR0 + 1, y0 >> 8);
	//X1
	_writeRegister(RA8875_DLHER0, x1 & 0xFF);
	_writeRegister(RA8875_DLHER0 + 1, x1 >> 8);
	//Y1
	_writeRegister(RA8875_DLVER0, y1 & 0xFF);
	_writeRegister(RA8875_DLVER0 + 1, y1 >> 8);
}

/**************************************************************************/
/*!
		curve addressing helper
		[private]
*/
/**************************************************************************/
void RA8875::_curve_addressing(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
	//center
	_writeRegister(RA8875_DEHR0, x0 & 0xFF);
	_writeRegister(RA8875_DEHR0 + 1, x0 >> 8);
	_writeRegister(RA8875_DEVR0, y0 & 0xFF);
	_writeRegister(RA8875_DEVR0 + 1, y0 >> 8);
	//long,short ax
	_writeRegister(RA8875_ELL_A0, x1 & 0xFF);
	_writeRegister(RA8875_ELL_A0 + 1, x1 >> 8);
	_writeRegister(RA8875_ELL_B0, y1 & 0xFF);
	_writeRegister(RA8875_ELL_B0 + 1, y1 >> 8);
}


/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+								PWM STUFF											 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/

/**************************************************************************/
/*!
		on/off GPIO (basic for Adafruit module
*/
/**************************************************************************/
void RA8875::GPIOX(boolean on)
{
	_writeRegister(RA8875_GPIOX, on);
}

/**************************************************************************/
/*!
		PWM out
		Parameters:
		pw: pwm selection (1,2)
		p: 0...255 rate

*/
/**************************************************************************/
void RA8875::PWMout(uint8_t pw, uint8_t p)
{
	uint8_t reg;
	pw > 1 ? reg = RA8875_P2DCR : reg = RA8875_P1DCR;
	_writeRegister(reg, p);
}

/**************************************************************************/
/*!
		Set the brightness of the backlight (if connected to pwm)
		(basic controls pwm 1)
		Parameters:
		val: 0...255
*/
/**************************************************************************/
void RA8875::brightness(uint8_t val)
{
	_brightness = val;
	PWMout(1, _brightness);
}

/**************************************************************************/
/*!
		controls the backligh by using PWM engine.
		It handles adafruit board separately
		Parameters:
		on: true(backlight on), false(backlight off)
*/
/**************************************************************************/
void RA8875::backlight(boolean on) //0.69b31 (fixed an issue with adafruit backlight)
{
	if (_displaySize == Adafruit_800x480) GPIOX(on);
	if (on == true) {
		PWMsetup(1, true, RA8875_PWM_CLK_DIV1024);//setup PWM ch 1 for backlight
		PWMout(1, _brightness);//turn on PWM1
	}
	else {
		PWMsetup(1, false, RA8875_PWM_CLK_DIV1024);//setup PWM ch 1 for backlight
	}
}

/**************************************************************************/
/*!
		Setup PWM engine
		Parameters:
		pw: pwm selection (1,2)
		on: turn on/off
		clock: the clock setting
		[private]
*/
/**************************************************************************/
void RA8875::PWMsetup(uint8_t pw, boolean on, uint8_t clock)
{
	uint8_t reg;
	uint8_t set;
	if (pw > 1) {
		reg = RA8875_P2CR;
		on == true ? set = RA8875_PxCR_ENABLE : set = RA8875_PxCR_DISABLE;
	}
	else {
		reg = RA8875_P1CR;
		on == true ? set = RA8875_PxCR_ENABLE : set = RA8875_PxCR_DISABLE;
	}
	_writeRegister(reg, (set | (clock & 0xF)));
}

/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+								         ISR										 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/

void RA8875::setInternalInt(enum RA8875intlist b)
{
	_enabledInterrups |= (1 << b);//set
}

void RA8875::clearInternalInt(enum RA8875intlist b)
{
	_enabledInterrups &= ~(1 << b);//clear
}

/**************************************************************************/
/*!
	RA8875 can use the INT pin for many purposes.
	This function just tell the CPU to use a pin for listen to RA8875 pin,
	no matter if we will use ISR or DigitalRead
------------------------------
Bit:	Called by:		In use:
--------------------------------
0: 		isr triggered	[*]
1: 		Resistive TS	[*]
2: 		KeyScan			[*]
3: 		DMA
4: 		BTE
5: 		-na-
6: 		-na-
7: 		-na-
--------------------------------
	Parameters:
	INTpin: it's the pin where we listen to ISR
	INTnum: it's the INT related to pin. On some processor it's not needed
	This last parameter it's used only when decide to use an ISR.
*/
/**************************************************************************/




/**************************************************************************/
/*!
		Check the [interrupt register] for an interrupt,
		if found it will reset it.
		bit
		0: complicated....
		1: BTE INT
		2: TOUCH INT
		3: DMA INT
		4: Keyscan INT
*/
/**************************************************************************/
bool RA8875::_checkInterrupt(uint8_t _bit, bool _clear)
{
	if (_bit > 4) _bit = 4;
	uint8_t temp = _readRegister(RA8875_INTC2);
	if (bitRead(temp, _bit) == 1) {
		if (_clear) {
			temp |= (1 << _bit);//bitSet(temp,_bit);
			//if (bitRead(temp,0)) bitSet(temp,0);//Mmmmm...
			_writeRegister(RA8875_INTC2, temp);//clear int
		}
		return true;
	}
	return false;
}



/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+								  SLEEP STUFF										 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/

/**************************************************************************/
/*!
	Sleep mode on/off (complete sequence)
	The sleep on/off sequence it's quite tricky on RA8875 when in SPI mode!
*/
/**************************************************************************/
void RA8875::sleep(boolean sleep)
{
	if (_sleep != sleep) {//only if it's needed
		_sleep = sleep;
		if (_sleep == true) {
			//1)turn off backlight
			if (_displaySize == Adafruit_800x480/* || _displaySize == Adafruit_640x480*/) GPIOX(false);
			//2)decelerate SPI clock
#if defined(_FASTCPU)
			_slowDownSPI(true);
#else
#if defined(SPI_HAS_TRANSACTION)
			_SPITransactionSpeed = 4000000UL;
#else
#if defined(___DUESTUFF) && defined(SPI_DUE_MODE_EXTENDED)
			SPI1.setClockDivider(_cs, SPI_SPEED_READ);
#else
			SPI1.setClockDivider(SPI_SPEED_READ);
#endif
#endif
#endif
			//3)set PLL to default
			_setSysClock(0x07, 0x03, 0x02);
			//4)display off & sleep
			_writeRegister(RA8875_PWRR, RA8875_PWRR_DISPOFF | RA8875_PWRR_SLEEP);
			delay(100);
		}
		else {
			//1)wake up with display off(100ms)
			_writeRegister(RA8875_PWRR, RA8875_PWRR_DISPOFF);
			delay(100);
			//2)bring back the pll
			_setSysClock(initStrings[_initIndex][0], initStrings[_initIndex][1], initStrings[_initIndex][2]);
			//_writeRegister(RA8875_PCSR,initStrings[_initIndex][2]);//Pixel Clock Setting Register
			delay(20);
			_writeRegister(RA8875_PWRR, RA8875_PWRR_NORMAL | RA8875_PWRR_DISPON);//disp on
			delay(20);
			//4)resume SPI speed
#if defined(_FASTCPU)
			_slowDownSPI(false);
#else
#if defined(SPI_HAS_TRANSACTION)
			_SPITransactionSpeed = _SPIMaxSpeed;
#else
#if defined(___DUESTUFF) && defined(SPI_DUE_MODE_EXTENDED)
			SPI1.setClockDivider(_cs, SPI_SPEED_WRITE);
#else
			SPI1.setClockDivider(SPI_SPEED_WRITE);
#endif
#endif
#endif
			//5)PLL afterburn!
			_setSysClock(sysClockPar[_initIndex][0], sysClockPar[_initIndex][1], initStrings[_initIndex][2]);
			//5)turn on backlight
			if (_displaySize == Adafruit_800x480/* || _displaySize == Adafruit_640x480*/) GPIOX(true);
			//_writeRegister(RA8875_PWRR, RA8875_PWRR_NORMAL);
		}
	}
}



/*
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
+							SPI & LOW LEVEL STUFF									 +
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*/

/**************************************************************************/
/*! PRIVATE
		Write in a register
		Parameters:
		reg: the register
		val: the data
*/
/**************************************************************************/
void RA8875::_writeRegister(const uint8_t reg, uint8_t val)
{
	writeCommand(reg);
	_writeData(val);
}

/**************************************************************************/
/*! PRIVATE
		Returns the value inside register
		Parameters:
		reg: the register
*/
/**************************************************************************/
uint8_t RA8875::_readRegister(const uint8_t reg)
{
	writeCommand(reg);
	return _readData(false);
}

/**************************************************************************/
/*!
		Write data
		Parameters:
		d: the data
*/
/**************************************************************************/
void RA8875::_writeData(uint8_t data)
{
	_startSend();
	_pspi->transfer(RA8875_DATAWRITE);
	_pspi->transfer(data);
	_endSend();
}

/**************************************************************************/
/*!
		Write 16 bit data
		Parameters:
		d: the data (16 bit)
*/
/**************************************************************************/
void  RA8875::writeData16(uint16_t data)
{
	_startSend();
	_pspi->transfer(RA8875_DATAWRITE);
	_pspi->transfer16(data);
	SPI1.transfer(data >> 8);
	SPI1.transfer(data & 0xFF);
	_endSend();
}

/**************************************************************************/
/*!	PRIVATE

*/
/**************************************************************************/
uint8_t RA8875::_readData(bool stat)
{
	//Serial.printf("RA8875::_readData _SPIMaxSpeed: %d\n", _SPIMaxSpeed);
	if (_inited) _SPITransactionSpeed = _SPIMaxReadSpeed;
	_startSend();
	stat == true ? _pspi->transfer(RA8875_CMDREAD) : _pspi->transfer(RA8875_DATAREAD);
	uint8_t x = _pspi->transfer(0x0);
	_endSend();
	if (_inited) _SPITransactionSpeed = _SPIMaxSpeed;

	return x;

}

/**************************************************************************/
uint8_t	RA8875::readStatus(void)
{
	return _readData(true);
}

/**************************************************************************/
/*! PRIVATE
		Write a command
		Parameters:
		d: the command
*/
/**************************************************************************/
void RA8875::writeCommand(const uint8_t d)
{
	_startSend();

	_pspi->transfer(RA8875_CMDWRITE);
	_pspi->transfer(d);
	_endSend();
}

void RA8875::_fontWrite(const uint8_t* buffer, uint16_t len)
{
	
	if (_use_default) {
		
		_textWrite((const char*)buffer, len);
		//Serial.printf("Default: %c, %d, %d\n", c, _cursorX, _cursorY);
		return;
	}
}


FLASHMEM void RA8875::setFont(const ILI9341_t3_font_t& f) {
	_use_default = 0;

	_cursorY += _cursorX;
	_cursorX -= _cursorY;
	_use_int_font = 1;
	_use_tfont = 0;

	font = &f;

	fontbpp = 1;
	// Calculate additional metrics for Anti-Aliased font support (BDF extn v2.3)
	if (font && font->version == 23) {
		fontbpp = (font->reserved & 0b000011) + 1;
		fontbppindex = (fontbpp >> 2) + 1;
		fontbppmask = (1 << (fontbppindex + 1)) - 1;
		fontppb = 8 / fontbpp;
		fontalphamx = 31 / ((1 << fontbpp) - 1);
		// Ensure text and bg color are different. Note: use setTextColor to set actual bg color
		if (_TXTForeColor == _TXTBackColor) _TXTBackColor = (_TXTForeColor == 0x0000) ? 0xFFFF : 0x0000;
	}
}

void RA8875::setTextSize(uint8_t s_x, uint8_t s_y) {
	
	if (s_x == 1)
	{
		setFontScale(1, 1);
		textsize_x = 1;
	}
	else
	{
		setFontScale(3, 3);
		textsize_x = 2;
	}
}


void RA8875::Pixel(int16_t x, int16_t y, uint16_t color)
{
	x += _originx;
	y += _originy;

	// x=x*2;
	// y=y*2;

	if ((x < _displayclipx1) || (x >= _displayclipx2) || (y < _displayclipy1) || (y >= _displayclipy2)) return;

	setActiveWindow(x, y, x, y);
	writeCommand(RA8875_MRWC);
	drawPixel(x, y, color);
}


uint32_t RA8875::fetchbit(const uint8_t* p, uint32_t index)
{
	if (p[index >> 3] & (1 << (7 - (index & 7)))) return 1;
	return 0;
}

uint32_t RA8875::fetchbits_unsigned(const uint8_t* p, uint32_t index, uint32_t required)
{
	uint32_t val = 0;
	do {
		uint8_t b = p[index >> 3];
		uint32_t avail = 8 - (index & 7);
		if (avail <= required) {
			val <<= avail;
			val |= b & ((1 << avail) - 1);
			index += avail;
			required -= avail;
		}
		else {
			b >>= avail - required;
			val <<= required;
			val |= b & ((1 << required) - 1);
			break;
		}
	} while (required);
	return val;
}

uint32_t RA8875::fetchbits_signed(const uint8_t* p, uint32_t index, uint32_t required)
{
	uint32_t val = fetchbits_unsigned(p, index, required);
	if (val & (1 << (required - 1))) {
		return (int32_t)val - (1 << required);
	}
	return (int32_t)val;
}

uint32_t RA8875::fetchpixel(const uint8_t* p, uint32_t index, uint32_t x)
{
	// The byte
	uint8_t b = p[index >> 3];
	// Shift to LSB position and mask to get value
	uint8_t s = ((fontppb - (x % fontppb) - 1) * fontbpp);
	// Mask and return
	return (b >> s) & fontbppmask;
}

void RA8875::write16BitColor(uint16_t color, bool last_pixel) {

	writeData16(color);

}


// append sysex data to buffer for sending in a bundled fashion
// a size field added to split the messages on receiver side.
FLASHMEM void RA8875::sendSysEx(uint8_t renderCmd, uint8_t length, uint8_t* data, bool hasStartEnd)
{
	if (sysex_len + length > sizeof(sysex_buffer) - 2) // flush if full, keep space for end byte
		flushSysEx();

	if (hasStartEnd)
	{
		sysex_buffer[sysex_len] = length + 1; // store length (with renderCmd byte added) to slice bundled messages on receiver side
		sysex_len++;
		sysex_buffer[sysex_len] = renderCmd;
		sysex_len++;
		memcpy(&sysex_buffer[sysex_len], data, length);
		sysex_len += length;
	}
	else
	{
		// TODO IMPLEMENT or remove hasStartEnd parameter
	}
}

FLASHMEM void RA8875::fillSysexData(uint8_t arr[], uint8_t nbArg, ...)
{
	va_list args;
	va_start(args, nbArg);
	for (uint8_t x = 0; x < nbArg; x++)
	{
		int val = va_arg(args, int);
		uint8_t posInArray = x * 2;

		// Convert value to two 7bit bytes for MIDI
		arr[posInArray] = val / 128;
		arr[posInArray + 1] = val % 128;
	}
}



FLASHMEM uint8_t RA8875::fillSysexDataColor(uint8_t* arr, uint8_t nbArg, ...)
{
	va_list args;
	va_start(args, nbArg);
	int prev_val = -1;

	uint8_t pos = 1; // first byte will be the color mode
	uint8_t colorCodes[2];

	for (uint8_t i = 0; i < nbArg; i++)
	{
		int val = va_arg(args, int);

		uint8_t colorCode = 0;
		switch (val)
		{
		case COLOR_BACKGROUND:
			colorCode = 0;
			break;
		case COLOR_SYSTEXT:
			colorCode = 1;
			break;
		case COLOR_INSTR:
			colorCode = 2;
			break;
		case COLOR_CHORDS:
			colorCode = 3;
			break;
		case COLOR_ARP:
			colorCode = 4;
			break;
		case COLOR_DRUMS:
			colorCode = 5;
			break;
		case COLOR_PITCHSMP:
			colorCode = 6;
			break;

		case RED:
			colorCode = 7;
			break;
		case PINK:
			colorCode = 8;
			break;
		case YELLOW:
			colorCode = 9;
			break;

		case GREEN:
			colorCode = 10;
			break;
		case MIDDLEGREEN:
			colorCode = 11;
			break;
		case DARKGREEN:
			colorCode = 12;
			break;

		case GREY1:
			colorCode = 13;
			break;
		case GREY2:
			colorCode = 14;
			break;
		case GREY3:
			colorCode = 15;
			break;
		case GREY4:
			colorCode = 16;
			break;
			// #define GREY4 0xC638 //only for UI test
		case DX_DARKCYAN:
			colorCode = 17;
			break;
		default:
			colorCode = 255;

			if (val == prev_val)
			{
				colorCode = 0;
			}
			else
			{
				// unknown color => 16 bits color value
				// to send on three 7bits bytes
				arr[pos] = (val >> 14) & 0x7F;
				arr[pos + 1] = (val >> 7) & 0x7F;
				arr[pos + 2] = val & 0x7F;
				pos += 3;
			}
		}

		colorCodes[i] = colorCode;

		// Serial.printf("  colorCodes [%d] %d\n", i, colorCodes[i]);
		if (colorCode != 255)
		{
			// avoid sending known color on 4 bytes, just send a 1 byte code by MIDI
			arr[pos] = colorCode;
			pos++;
		}
		prev_val = val;
	}

	// set color mode
	if (colorCodes[0] < 100)
	{
		if (nbArg == 2)
		{
			arr[0] = colorCodes[1] < 100 ? 0 : 1;
		}
		else
		{
			arr[0] = 0;
		}
	}
	else
	{
		if (nbArg == 2)
		{
			arr[0] = colorCodes[1] < 100 ? 2 : 3;
		}
		else
		{
			arr[0] = 1;
		}
	}

	return pos;
}


// send sysex bundled.
FLASHMEM void RA8875::flushSysEx() {
	if (sysex_len <= SYSEX_HEADER_SIZE)
		return;

	memcpy(sysex_buffer, sysexRenderHeader, SYSEX_HEADER_SIZE); // place sysex MIDI header
	sysex_buffer[sysex_len] = 0xF7; // place sysex end byte

	usbMIDI.sendSysEx(sysex_len + 1, sysex_buffer, true);

	sysex_len = SYSEX_HEADER_SIZE;
}
