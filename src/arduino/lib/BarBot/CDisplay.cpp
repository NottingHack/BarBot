#include "CDisplay.h"

CDisplay::CDisplay(uint8_t rstPin, uint8_t clkPin)
{
	_rstPin = rstPin;
	_clkPin = clkPin;
	_busy = false;

	pinMode(_rstPin, OUTPUT);
	pinMode(_clkPin, OUTPUT);

	digitalWrite(_rstPin, LOW);
	digitalWrite(_clkPin, LOW);
}

CDisplay::~CDisplay()
{
}

void CDisplay::setOutput(int number)
{	

	// This method assumes that digitalWrite() is slow enough that no
	// additional delay between edges is required.

	int i;

	// Some basic bounds and state checking:
	if ((number > 999) || (number < 0)) { return; }
	if (_busy) { return; }

	_busy = true;

	// Reset the display to zero
	digitalWrite(_rstPin, HIGH);
	digitalWrite(_rstPin, LOW);

	// Clock the display to the number
	digitalWrite(_clkPin, LOW);
	for(i = 0; i < number; ++i)
	{
		digitalWrite(_clkPin, HIGH);
		digitalWrite(_clkPin, LOW);
	}

	_busy = false;
}

bool CDisplay::isBusy(void)
{
	return _busy;
}

