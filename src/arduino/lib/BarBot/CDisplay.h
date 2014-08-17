#ifndef CDISPLAY_H
#define CDISPLAY_H

#include "Arduino.h"

class CDisplay
{
	public:
		CDisplay(uint8_t rstPin, uint8_t clkPin);
		~CDisplay();
		void setOutput(int number);
		bool isBusy(void);

	private:
		uint8_t _rstPin;
		uint8_t _clkPin;
		bool _busy;
};

#endif

